#include "ui/radar_display.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "data/aircraft_icons.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/device_settings.h"
#include "services/radar_location.h"
#include "ui/map_layer.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"
#include "ui/strip_canvas.h"
#include "ui/theme.h"

/*
 * Rendering model
 * ---------------
 * A full 360x360 RGB565 frame is 259 KB - more than an ESP32-C3 can spare next
 * to Wi-Fi and TLS. So each frame is described once (buildFrame: positions,
 * colours, tag layout) and then rasterised in kBandCount horizontal strips
 * that share one small sprite. Every strip is complete before it is pushed, so
 * the panel never shows an erase/redraw gap.
 *
 * The C3 has no FPU, so the per-pixel work (rings, dots, icons, blending) is
 * integer only; floats are confined to the per-aircraft projection.
 */

namespace ui {
namespace {

using services::adsb::Aircraft;
namespace icons = data::aircraft_icons;

constexpr int kCx = radar::kCenterX;
constexpr int kCy = radar::kCenterY;
constexpr int kRingR = radar::kGridOuterRadius;

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.01745329252f;

/** Scale label / status badge sit midway between ring 3 and the outer ring. */
constexpr int kScaleLabelRadius = kRingR * 7 / 8;
constexpr UiFont kTagFont = UiFont::kSmall14;
constexpr int kTagPadX = 3;
constexpr int kTagPadY = 1;
constexpr int kTagArrowW = 10;
/** Tags must stay inside this radius so the round glass does not clip them. */
constexpr int kTagSafeRadius = kCx - 4;

using namespace canvas;

inline const theme::Palette& pal() { return theme::palette(); }

bool s_band_ready = false;

void wideLine(int x0, int y0, int x1, int y1, float half_w, uint16_t color) {
  const int pad = static_cast<int>(half_w) + 2;
  if (!rowsVisible(std::min(y0, y1) - pad, std::max(y0, y1) + pad)) {
    return;
  }
  s_band.drawWideLine(x0, y0 - s_by, x1, y1 - s_by, half_w, color);
}

void drawIcon(int cx, int cy, uint8_t shape, uint8_t heading_idx, uint16_t color) {
  constexpr int kHalf = icons::kIconSize / 2;
  if (!rowsVisible(cy - kHalf, cy + kHalf)) {
    return;
  }
  const uint8_t* src = icons::kIcons[shape][heading_idx];
  for (int iy = 0; iy < icons::kIconSize; ++iy) {
    const int ly = cy - kHalf + iy - s_by;
    if (static_cast<unsigned>(ly) >= static_cast<unsigned>(kBandH)) {
      continue;
    }
    const uint8_t* row = src + iy * icons::kIconSize;
    for (int ix = 0; ix < icons::kIconSize; ++ix) {
      const uint8_t v = row[ix];
      const int x = cx - kHalf + ix;
      if (v == 0 || static_cast<unsigned>(x) >= static_cast<unsigned>(kW)) {
        continue;
      }
      uint16_t* p = &s_px[ly * kW + x];
      uint16_t px = swap16(*p);
      const uint32_t outline = v >> 4;
      const uint32_t fill = v & 0x0F;
      if (outline != 0) {
        px = mix565(px, pal().outline, (outline * 32u + 7u) / 15u);
      }
      if (fill != 0) {
        px = mix565(px, color, (fill * 32u + 7u) / 15u);
      }
      *p = swap16(px);
    }
  }
}

void selectFont(UiFont font) { s_band.setFont(uiFont(font)); }

int textWidth(const char* s, UiFont font) {
  selectFont(font);
  return s_band.textWidth(s);
}

int fontHeight(UiFont font) {
  selectFont(font);
  return s_band.fontHeight();
}

/** Anti-aliased text blended over whatever is already in the strip. */
void drawText(const char* s, int x, int y, UiFont font, uint16_t color,
              textdatum_t datum) {
  if (!rowsVisible(y - 30, y + 30)) {
    return;
  }
  selectFont(font);
  s_band.setTextDatum(datum);
  s_band.setTextColor(color);  // no background colour = alpha blend
  s_band.drawString(s, x, y - s_by);
}

// ------------------------------------------------------------------ frame model

struct Rect {
  int16_t x, y, w, h;
};

struct PlaneItem {
  uint8_t index;  // into s_planes
  uint8_t heading_idx;
  uint8_t trail_count;
  bool has_vector;
  bool has_tag;
  bool show_type;
  bool show_alt;
  int8_t climb;  // -1 descending, 0 level/unknown, +1 climbing
  uint16_t color;
  int16_t x, y;
  int16_t nose_x, nose_y;
  int16_t vec_x, vec_y;
  int16_t trail_x[services::adsb::kTrailPoints];
  int16_t trail_y[services::adsb::kTrailPoints];
  Rect tag;
  int32_t dist_sq;
};

struct RimDot {
  int16_t x, y;
  uint16_t color;
};

struct TickMark {
  int16_t x0, y0, x1, y1;
};

struct StaticKey {
  double lat = 1000.0;
  double lon = 1000.0;
  uint8_t range_index = 0xFF;
  bool runways = false;
  bool miles = false;
  uint8_t map_layers = 0;
  uint32_t map_generation = 0xFFFFFFFFu;

  bool operator==(const StaticKey& o) const {
    return lat == o.lat && lon == o.lon && range_index == o.range_index &&
           runways == o.runways && miles == o.miles &&
           map_layers == o.map_layers && map_generation == o.map_generation;
  }
};

Aircraft s_planes[services::adsb::kMaxAircraft];
PlaneItem s_items[services::adsb::kMaxAircraft];
RimDot s_dots[services::adsb::kMaxAircraft];
size_t s_item_count = 0;
size_t s_dot_count = 0;

runway::Segment s_runways[runway::kMaxSegments];
runway::Label s_runway_labels[runway::kMaxLabels];
size_t s_runway_count = 0;
size_t s_runway_label_count = 0;
StaticKey s_static_key;

/** Place names / airfield codes from the map that made it onto the screen. */
constexpr size_t kMaxMapLabels = 14;
map::LabelCandidate s_map_labels[kMaxMapLabels];
size_t s_map_label_count = 0;

constexpr int kTickCount = 36;
TickMark s_ticks[kTickCount];
bool s_ticks_ready = false;

char s_scale_label[12] = "";
const char* s_badge = nullptr;
bool s_frame_valid = false;
unsigned long s_first_frame_ms = 0;

int s_tag_line_h = 15;

/** Fixed furniture (compass letters, labels, centre mark) that tags keep clear of. */
constexpr size_t kMaxObstacles = 8 + runway::kMaxLabels;
Rect s_obstacles[kMaxObstacles];
size_t s_obstacle_count = 0;

// -------------------------------------------------------------------- colours

uint16_t altitudeColor(int32_t alt_ft) {
  struct Stop {
    int32_t alt;
    uint8_t r, g, b;
  };
  static constexpr Stop kStops[] = {
      {0, 255, 120, 40},      {3000, 255, 205, 40},   {8000, 120, 232, 70},
      {15000, 40, 222, 170},  {25000, 70, 175, 255},  {35000, 160, 135, 255},
      {42000, 235, 115, 255},
  };
  constexpr size_t kN = sizeof(kStops) / sizeof(kStops[0]);
  if (!pal().altitude_colors) {
    return pal().aircraft;
  }
  if (alt_ft == services::adsb::kAltUnknown || alt_ft == services::adsb::kAltGround) {
    return pal().aircraft_unknown;
  }
  if (alt_ft <= kStops[0].alt) {
    return radar::rgb565(kStops[0].r, kStops[0].g, kStops[0].b);
  }
  for (size_t i = 1; i < kN; ++i) {
    if (alt_ft <= kStops[i].alt) {
      const Stop& a = kStops[i - 1];
      const Stop& b = kStops[i];
      const int32_t span = b.alt - a.alt;
      const int32_t t = alt_ft - a.alt;
      const auto lerp = [&](int32_t from, int32_t to) {
        return static_cast<uint8_t>(from + (to - from) * t / span);
      };
      return radar::rgb565(lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b));
    }
  }
  return radar::rgb565(kStops[kN - 1].r, kStops[kN - 1].g, kStops[kN - 1].b);
}

// ----------------------------------------------------------------- projection

struct Projection {
  float center_lat;
  float center_lon;
  float cos_lat;
  float px_per_km;

  void toKm(float lat, float lon, float* dx_km, float* dy_km) const {
    // Longitude degrees shrink toward the poles; scale by cos(latitude).
    *dx_km = (lon - center_lon) * kKmPerDeg * cos_lat;
    *dy_km = (lat - center_lat) * kKmPerDeg;
  }

  void toScreen(float lat, float lon, int* x, int* y) const {
    float dx_km = 0.0f;
    float dy_km = 0.0f;
    toKm(lat, lon, &dx_km, &dy_km);
    *x = kCx + static_cast<int>(lroundf(dx_km * px_per_km));
    *y = kCy - static_cast<int>(lroundf(dy_km * px_per_km));
  }
};

Projection currentProjection() {
  Projection p;
  p.center_lat = static_cast<float>(services::location::lat());
  p.center_lon = static_cast<float>(services::location::lon());
  p.cos_lat = cosf(p.center_lat * kDegToRad);
  p.px_per_km = static_cast<float>(kRingR) / radar::rangeCurrent().outer_km;
  return p;
}

/**
 * Dead-reckon an aircraft's position from its last-fetched fix along its ground
 * track, so it moves smoothly between ADS-B updates.
 */
void extrapolatedLatLon(const Aircraft& plane, unsigned long base_ms, float* lat,
                        float* lon) {
  *lat = plane.lat;
  *lon = plane.lon;
  if (base_ms == 0 || plane.gs_knots <= 0.0f) {
    return;
  }
  // Elapsed since the fix was measured = time since fetch + the fix's own age.
  // Capped so a dropped connection does not fling targets across the scope.
  unsigned long elapsed_ms = (millis() - base_ms) + plane.pos_age_ms;
  if (elapsed_ms > 60000UL) {
    elapsed_ms = 60000UL;
  }
  const float elapsed_h = static_cast<float>(elapsed_ms) / 3600000.0f;
  const float dist_km = plane.gs_knots * 1.852f * elapsed_h;  // knots -> km
  if (dist_km <= 0.0f) {
    return;
  }
  const float rad = plane.track_deg * kDegToRad;  // track: 0 = N, 90 = E
  const float lat_rad = plane.lat * kDegToRad;
  float cos_lat = cosf(lat_rad);
  if (cos_lat < 0.05f) {
    cos_lat = 0.05f;
  }
  *lat = plane.lat + (dist_km * cosf(rad)) / kKmPerDeg;
  *lon = plane.lon + (dist_km * sinf(rad)) / (kKmPerDeg * cos_lat);
}

int distSqFromCenter(int x, int y) {
  const int dx = x - kCx;
  const int dy = y - kCy;
  return dx * dx + dy * dy;
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) {
    return 0;
  }
  // Fixed screen scale: 60 s horizon at gs, not tied to current range zoom.
  constexpr float kKmPerKnotPerHorizon =
      1.852f * radar::kAircraftTrackHorizonSec / 3600.0f;
  const float px = gs_knots * kKmPerKnotPerHorizon * kRingR /
                   radar::kAircraftTrackRefOuterKm * radar::kAircraftTrackLengthScale;
  const int len = static_cast<int>(px + 0.5f);
  return std::max(len, radar::kAircraftSpeedLineMinPx);
}

/** Pull (x1,y1) back along the segment from (x0,y0) until it is inside radius r. */
void clipEndToRadius(int x0, int y0, int* x1, int* y1, int r) {
  const int r_sq = r * r;
  if (distSqFromCenter(*x1, *y1) <= r_sq) {
    return;
  }
  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  for (int step = 15; step >= 0; --step) {
    const int px = x0 + dx * step / 16;
    const int py = y0 + dy * step / 16;
    if (distSqFromCenter(px, py) <= r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
  }
  *x1 = x0;
  *y1 = y0;
}

// ------------------------------------------------------------------ tag layout

bool rectsOverlap(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool rectInsideGlass(const Rect& r) {
  const int limit = kTagSafeRadius * kTagSafeRadius;
  return distSqFromCenter(r.x, r.y) <= limit &&
         distSqFromCenter(r.x + r.w, r.y) <= limit &&
         distSqFromCenter(r.x, r.y + r.h) <= limit &&
         distSqFromCenter(r.x + r.w, r.y + r.h) <= limit;
}

Rect iconRect(const PlaneItem& item) {
  constexpr int16_t kHalf = radar::kAircraftHalfPx;
  return Rect{static_cast<int16_t>(item.x - kHalf), static_cast<int16_t>(item.y - kHalf),
              static_cast<int16_t>(kHalf * 2), static_cast<int16_t>(kHalf * 2)};
}

void addObstacle(int cx, int cy, int w, int h) {
  if (s_obstacle_count < kMaxObstacles) {
    s_obstacles[s_obstacle_count++] =
        Rect{static_cast<int16_t>(cx - w / 2), static_cast<int16_t>(cy - h / 2),
             static_cast<int16_t>(w), static_cast<int16_t>(h)};
  }
}

/** True if a candidate tag rectangle is clear of every symbol, tag and label. */
bool tagSpotFree(const Rect& r, size_t self) {
  for (size_t i = 0; i < s_obstacle_count; ++i) {
    if (rectsOverlap(r, s_obstacles[i])) {
      return false;
    }
  }
  for (size_t i = 0; i < s_item_count; ++i) {
    if (i == self) {
      continue;
    }
    if (rectsOverlap(r, iconRect(s_items[i]))) {
      return false;
    }
    if (i < self && s_items[i].has_tag && rectsOverlap(r, s_items[i].tag)) {
      return false;
    }
  }
  return true;
}

void measureTag(const Aircraft& plane, const PlaneItem& item, int* w, int* h) {
  int lines = 0;
  int max_w = 0;
  if (plane.callsign[0] != '\0') {
    max_w = std::max(max_w, textWidth(plane.callsign, kTagFont));
    ++lines;
  }
  if (item.show_type) {
    max_w = std::max(max_w, textWidth(plane.type, kTagFont));
    ++lines;
  }
  if (item.show_alt) {
    const int arrow = item.climb != 0 ? kTagArrowW : 0;
    max_w = std::max(max_w, textWidth(plane.alt, kTagFont) + arrow);
    ++lines;
  }
  *w = lines > 0 ? max_w + kTagPadX * 2 : 0;
  *h = lines * s_tag_line_h + kTagPadY * 2;
}

/** Try the eight spots around the symbol; true if one is free. */
bool placeTag(size_t self, int w, int h) {
  PlaneItem& item = s_items[self];
  const int off = radar::kAircraftHalfPx + radar::kAircraftLabelGapPx;
  const int diag = off - 5;  // diagonal spots tuck in toward the symbol
  const bool west = item.x < kCx;
  const bool north = item.y < kCy;

  // Offsets of the tag's top-left corner for each side of the symbol.
  const int x_right = item.x + off;
  const int x_left = item.x - off - w;
  const int x_mid = item.x - w / 2;
  const int y_below = item.y + off;
  const int y_above = item.y - off - h;
  const int y_mid = item.y - h / 2;
  const int xd_right = item.x + diag;
  const int xd_left = item.x - diag - w;
  const int yd_below = item.y + diag;
  const int yd_above = item.y - diag - h;

  // Toward the scope centre first (the classic layout), then the rest.
  const int spots[8][2] = {
      {west ? x_right : x_left, y_mid},
      {x_mid, north ? y_below : y_above},
      {west ? xd_right : xd_left, north ? yd_below : yd_above},
      {west ? x_left : x_right, y_mid},
      {x_mid, north ? y_above : y_below},
      {west ? xd_right : xd_left, north ? yd_above : yd_below},
      {west ? xd_left : xd_right, north ? yd_below : yd_above},
      {west ? xd_left : xd_right, north ? yd_above : yd_below},
  };
  for (const auto& spot_xy : spots) {
    const Rect r{static_cast<int16_t>(spot_xy[0]), static_cast<int16_t>(spot_xy[1]),
                 static_cast<int16_t>(w), static_cast<int16_t>(h)};
    if (rectInsideGlass(r) && tagSpotFree(r, self)) {
      item.tag = r;
      return true;
    }
  }
  return false;
}

/**
 * Decide what tag item `self` gets. Items are laid out nearest-first, so close
 * traffic gets the full callsign / type / altitude block; when the scope is
 * busy, later aircraft fall back to a callsign-only tag and finally to no tag
 * at all rather than drawing text over each other.
 */
void layoutTag(size_t self) {
  PlaneItem& item = s_items[self];
  const Aircraft& plane = s_planes[item.index];
  item.has_tag = false;
  if (plane.callsign[0] == '\0' && plane.type[0] == '\0' && plane.alt[0] == '\0') {
    return;
  }

  int w = 0;
  int h = 0;
  item.show_type = plane.type[0] != '\0';
  item.show_alt = plane.alt[0] != '\0';
  measureTag(plane, item, &w, &h);
  if (placeTag(self, w, h)) {
    item.has_tag = true;
    return;
  }
  if (plane.callsign[0] == '\0' || (!item.show_type && !item.show_alt)) {
    return;
  }
  item.show_type = false;
  item.show_alt = false;
  measureTag(plane, item, &w, &h);
  item.has_tag = placeTag(self, w, h);
}

// ------------------------------------------------------------------ frame build

void buildTicks() {
  if (s_ticks_ready) {
    return;
  }
  for (int i = 0; i < kTickCount; ++i) {
    const int deg = i * 10;
    const float rad = static_cast<float>(deg) * kDegToRad;
    const float s = sinf(rad);
    const float c = cosf(rad);
    const int r0 = kRingR + radar::kTickInnerGapPx;
    const int r1 = r0 + ((deg % 30 == 0) ? radar::kTickLenMajorPx : radar::kTickLenPx);
    s_ticks[i].x0 = static_cast<int16_t>(kCx + lroundf(s * r0));
    s_ticks[i].y0 = static_cast<int16_t>(kCy - lroundf(c * r0));
    s_ticks[i].x1 = static_cast<int16_t>(kCx + lroundf(s * r1));
    s_ticks[i].y1 = static_cast<int16_t>(kCy - lroundf(c * r1));
  }
  s_ticks_ready = true;
}

Rect centredRect(int cx, int cy, int w, int h) {
  return Rect{static_cast<int16_t>(cx - w / 2), static_cast<int16_t>(cy - h / 2),
              static_cast<int16_t>(w), static_cast<int16_t>(h)};
}

/**
 * Choose which map labels to show: most important first, skipping any that
 * would print over another label, the centre mark or the range label.
 */
void placeMapLabels() {
  s_map_label_count = 0;
  static map::LabelCandidate candidates[48];
  const size_t n = map::collectLabels(candidates, 48);
  if (n == 0) {
    return;
  }

  const int text_h = fontHeight(UiFont::kSmall14);
  Rect taken[kMaxMapLabels + runway::kMaxLabels + 2];
  size_t taken_count = 0;
  taken[taken_count++] = centredRect(kCx, kCy, 18, 18);
  taken[taken_count++] = centredRect(kCx + kScaleLabelRadius, kCy, 56, text_h + 4);
  for (size_t i = 0; i < s_runway_label_count; ++i) {
    const runway::Label& l = s_runway_labels[i];
    taken[taken_count++] = centredRect(
        l.x, l.y - text_h / 2, textWidth(l.ident, UiFont::kSmall14) + 4, text_h);
  }

  for (size_t i = 0; i < n && s_map_label_count < kMaxMapLabels; ++i) {
    const map::LabelCandidate& c = candidates[i];
    if (c.airfield) {
      // The built-in database already labels the large airports.
      bool duplicate = false;
      for (size_t r = 0; r < s_runway_label_count; ++r) {
        duplicate = duplicate || strcmp(s_runway_labels[r].ident, c.text) == 0;
      }
      if (duplicate) {
        continue;
      }
    }
    const int w = textWidth(c.text, UiFont::kSmall14) + 6;
    const Rect r = centredRect(c.x, c.y - 3 - text_h / 2, w, text_h + 4);
    const int reach = kRingR - 4;
    if (distSqFromCenter(r.x, r.y) > reach * reach ||
        distSqFromCenter(r.x + r.w, r.y) > reach * reach ||
        distSqFromCenter(r.x, r.y + r.h) > reach * reach ||
        distSqFromCenter(r.x + r.w, r.y + r.h) > reach * reach) {
      continue;
    }
    bool clash = false;
    for (size_t t = 0; t < taken_count && !clash; ++t) {
      clash = rectsOverlap(r, taken[t]);
    }
    if (clash) {
      continue;
    }
    taken[taken_count++] = r;
    s_map_labels[s_map_label_count++] = c;
  }
}

void refreshStaticLayer() {
  StaticKey key;
  key.lat = services::location::lat();
  key.lon = services::location::lon();
  key.range_index = radar::rangeIndex();
  key.runways = radar::showRunways();
  key.miles = radar::useMiles();
  key.map_layers = radar::mapLayers();
  key.map_generation = map::generation();
  if (key == s_static_key) {
    return;
  }
  s_static_key = key;
  runway::collect(s_runways, &s_runway_count, s_runway_labels, &s_runway_label_count);
  map::prepare(key.map_layers);
  placeMapLabels();
}

void sortItemsNearFirst() {
  for (size_t i = 1; i < s_item_count; ++i) {
    const PlaneItem key = s_items[i];
    size_t j = i;
    while (j > 0 && s_items[j - 1].dist_sq > key.dist_sq) {
      s_items[j] = s_items[j - 1];
      --j;
    }
    s_items[j] = key;
  }
}

void buildFrame() {
  buildTicks();
  refreshStaticLayer();
  s_tag_line_h = fontHeight(kTagFont) - 2;

  unsigned long base_ms = 0;
  const size_t n = services::adsb::snapshotAircraft(
      s_planes, services::adsb::kMaxAircraft, &base_ms);
  const bool military_only = radar::militaryOnly();
  const Projection proj = currentProjection();

  const float outer_km = radar::rangeCurrent().outer_km;
  const float inside_km =
      outer_km * static_cast<float>(kRingR - radar::kAircraftInsideRingInsetPx) /
      static_cast<float>(kRingR);
  const int rim_r = kCx - radar::kBeyondRingScreenMarginPx;

  s_item_count = 0;
  s_dot_count = 0;

  for (size_t i = 0; i < n; ++i) {
    const Aircraft& plane = s_planes[i];
    if (military_only && !plane.military) {
      continue;  // filter toggled since this list was fetched
    }

    float lat = 0.0f;
    float lon = 0.0f;
    extrapolatedLatLon(plane, base_ms, &lat, &lon);
    float dx_km = 0.0f;
    float dy_km = 0.0f;
    proj.toKm(lat, lon, &dx_km, &dy_km);
    const float dist_km = sqrtf(dx_km * dx_km + dy_km * dy_km);
    const uint16_t color =
        plane.military ? pal().military : altitudeColor(plane.alt_ft);

    if (dist_km > inside_km) {
      if (dist_km < 0.01f) {
        continue;
      }
      // Rim marker: correct bearing, fixed radius.
      RimDot& dot = s_dots[s_dot_count++];
      dot.x = static_cast<int16_t>(kCx + lroundf(dx_km / dist_km * rim_r));
      dot.y = static_cast<int16_t>(kCy - lroundf(dy_km / dist_km * rim_r));
      dot.color = color;
      continue;
    }

    PlaneItem& item = s_items[s_item_count++];
    item.index = static_cast<uint8_t>(i);
    item.color = color;
    item.x = static_cast<int16_t>(kCx + lroundf(dx_km * proj.px_per_km));
    item.y = static_cast<int16_t>(kCy - lroundf(dy_km * proj.px_per_km));
    item.dist_sq = distSqFromCenter(item.x, item.y);
    item.has_tag = false;
    item.climb = plane.vrate_fpm >= 250 ? 1 : (plane.vrate_fpm <= -250 ? -1 : 0);

    int heading = static_cast<int>(lroundf(plane.nose_deg / 5.0f)) % icons::kHeadingCount;
    if (heading < 0) {
      heading += icons::kHeadingCount;
    }
    item.heading_idx = static_cast<uint8_t>(heading);

    const float nose_rad = static_cast<float>(heading * 5) * kDegToRad;
    item.nose_x = static_cast<int16_t>(
        item.x + lroundf(sinf(nose_rad) * radar::kAircraftNoseLenPx));
    item.nose_y = static_cast<int16_t>(
        item.y - lroundf(cosf(nose_rad) * radar::kAircraftNoseLenPx));

    const int len = speedLineLengthPx(plane.gs_knots);
    item.has_vector = false;
    if (len > 0) {
      const float track_rad = plane.track_deg * kDegToRad;
      int ex = item.nose_x + static_cast<int>(lroundf(sinf(track_rad) * len));
      int ey = item.nose_y - static_cast<int>(lroundf(cosf(track_rad) * len));
      clipEndToRadius(item.nose_x, item.nose_y, &ex, &ey, kRingR - 2);
      item.vec_x = static_cast<int16_t>(ex);
      item.vec_y = static_cast<int16_t>(ey);
      item.has_vector = ex != item.nose_x || ey != item.nose_y;
    }

    // Trail: the last reported fix, then older fixes.
    item.trail_count = 0;
    const int trail_limit = (kRingR - 2) * (kRingR - 2);
    for (size_t t = 0; t <= plane.trail_count && t <= services::adsb::kTrailPoints; ++t) {
      float t_lat = plane.lat;
      float t_lon = plane.lon;
      if (t > 0) {
        t_lat += plane.trail_dlat[t - 1] / services::adsb::kTrailUnitsPerDeg;
        t_lon += plane.trail_dlon[t - 1] / services::adsb::kTrailUnitsPerDeg;
      }
      int tx = 0;
      int ty = 0;
      proj.toScreen(t_lat, t_lon, &tx, &ty);
      if (distSqFromCenter(tx, ty) > trail_limit ||
          item.trail_count >= services::adsb::kTrailPoints) {
        break;
      }
      item.trail_x[item.trail_count] = static_cast<int16_t>(tx);
      item.trail_y[item.trail_count] = static_cast<int16_t>(ty);
      ++item.trail_count;
    }
  }

  radar::formatCurrentRing3Label(s_scale_label, sizeof(s_scale_label));

  const unsigned long now = millis();
  if (s_first_frame_ms == 0) {
    s_first_frame_ms = now != 0 ? now : 1;
  }
  const unsigned long since_data = now - (base_ms != 0 ? base_ms : s_first_frame_ms);
  if (since_data > config::kAdsbStaleMs) {
    s_badge = "NO DATA";
  } else if (military_only) {
    s_badge = "MIL ONLY";
  } else {
    s_badge = nullptr;
  }

  // Tag layout: note what must stay readable, then place tags nearest-first.
  s_obstacle_count = 0;
  const int small_h = fontHeight(UiFont::kSmall14);
  for (const int sign : {-1, 1}) {
    addObstacle(kCx, kCy + sign * radar::kCardinalRadius, 24, 24);
    addObstacle(kCx + sign * radar::kCardinalRadius, kCy, 26, 24);
  }
  addObstacle(kCx, kCy, 14, 14);
  addObstacle(kCx + kScaleLabelRadius, kCy,
              textWidth(s_scale_label, UiFont::kSmall14) + 8, small_h);
  if (s_badge != nullptr) {
    addObstacle(kCx, kCy + kScaleLabelRadius,
                textWidth(s_badge, UiFont::kSmall14) + 12, small_h + 2);
  }
  for (size_t i = 0; i < s_runway_label_count; ++i) {
    const runway::Label& l = s_runway_labels[i];
    addObstacle(l.x, l.y - small_h / 2, textWidth(l.ident, UiFont::kSmall14), small_h - 4);
  }

  sortItemsNearFirst();
  for (size_t i = 0; i < s_item_count; ++i) {
    layoutTag(i);
  }

  s_frame_valid = true;
}

// --------------------------------------------------------------- strip drawing

void drawGrid() {
  s_band.fillScreen(pal().bezel);
  fillDisc(kCx, kCy, kRingR, pal().scope);
  map::drawLines();

  // Crosshair first so the rings cross over it cleanly.
  if (rowsVisible(kCy, kCy)) {
    fillSpan(kCx - kRingR, kCx + kRingR, kCy, pal().cross);
  }
  {
    const int y0 = std::max(kCy - kRingR, s_by);
    const int y1 = std::min(kCy + kRingR, s_by + kBandH - 1);
    for (int y = y0; y <= y1; ++y) {
      blendPx(kCx, y, pal().cross, 32);
    }
  }

  for (int i = 1; i < radar::kRingCount; ++i) {
    ring(kCx, kCy, kRingR * i / radar::kRingCount, 8, pal().ring);
  }
  ring(kCx, kCy, kRingR, 12, pal().ring_outer);

  for (int i = 0; i < kTickCount; ++i) {
    if (i % 9 == 0) {
      continue;  // N / E / S / W letters sit here
    }
    const TickMark& t = s_ticks[i];
    wideLine(t.x0, t.y0, t.x1, t.y1, 0.8f, pal().tick);
  }
}

void drawRunways() {
  for (size_t i = 0; i < s_runway_count; ++i) {
    const runway::Segment& s = s_runways[i];
    wideLine(s.x0, s.y0, s.x1, s.y1, radar::kRunwayLineHalfWidth, pal().runway);
  }
  for (size_t i = 0; i < s_runway_label_count; ++i) {
    const runway::Label& l = s_runway_labels[i];
    drawText(l.ident, l.x, l.y, UiFont::kSmall14, pal().runway_label,
             textdatum_t::bottom_center);
  }
}

void drawMapLabels() {
  for (size_t i = 0; i < s_map_label_count; ++i) {
    const map::LabelCandidate& l = s_map_labels[i];
    if (l.airfield) {
      drawText(l.text, l.x, l.y - 3, UiFont::kSmall14, pal().runway_label,
               textdatum_t::bottom_center);
      continue;
    }
    const uint16_t color = l.rank >= 2 ? pal().town_major : pal().town_minor;
    spot(l.x, l.y, 24, color, 26);
    drawText(l.text, l.x, l.y - 3, UiFont::kSmall14, color, textdatum_t::bottom_center);
  }
}

void drawCompassLabels() {
  const int r = radar::kCardinalRadius;
  drawText("N", kCx, kCy - r, UiFont::kCardinal20, pal().north,
           textdatum_t::middle_center);
  drawText("S", kCx, kCy + r, UiFont::kCardinal20, pal().label,
           textdatum_t::middle_center);
  drawText("W", kCx - r, kCy, UiFont::kCardinal20, pal().label,
           textdatum_t::middle_center);
  drawText("E", kCx + r, kCy, UiFont::kCardinal20, pal().label,
           textdatum_t::middle_center);
}

/** Small rounded label centred on (x, y). */
void drawPill(const char* text, int x, int y, uint16_t back, uint16_t fore) {
  const int tw = textWidth(text, UiFont::kSmall14);
  const int th = fontHeight(UiFont::kSmall14);
  const int w = tw + 12;
  const int h = th + 2;
  if (!rowsVisible(y - h, y + h)) {
    return;
  }
  s_band.fillSmoothRoundRect(x - w / 2, y - h / 2 - s_by, w, h, h / 2, back);
  drawText(text, x, y, UiFont::kSmall14, fore, textdatum_t::middle_center);
}

void drawScaleAndBadge() {
  // Range label on the east spoke, between ring 3 and the outer ring.
  drawPill(s_scale_label, kCx + kScaleLabelRadius, kCy, pal().scope,
           pal().scale);
  if (s_badge != nullptr) {
    drawPill(s_badge, kCx, kCy + kScaleLabelRadius, pal().badge_back,
             pal().badge_text);
  }
}

void drawCenterMarker() {
  ring(kCx, kCy, 7, 8, pal().center, 12);
  spot(kCx, kCy, radar::kCenterDotRadius * 16, pal().center);
}

void drawTrails() {
  for (size_t i = 0; i < s_item_count; ++i) {
    const PlaneItem& item = s_items[i];
    for (int t = 0; t < item.trail_count; ++t) {
      const uint32_t alpha = static_cast<uint32_t>(std::max(7, 28 - t * 3));
      spot(item.trail_x[t], item.trail_y[t], 22, item.color, alpha);
    }
  }
}

void drawRimDots() {
  for (size_t i = 0; i < s_dot_count; ++i) {
    const RimDot& d = s_dots[i];
    spot(d.x, d.y, radar::kBeyondRingDotRadiusPx * 16 + 16, pal().outline);
    spot(d.x, d.y, radar::kBeyondRingDotRadiusPx * 16, d.color);
  }
}

void drawSymbols() {
  // Far to near, so the closest traffic ends up on top.
  for (size_t d = s_item_count; d-- > 0;) {
    const PlaneItem& item = s_items[d];
    const Aircraft& plane = s_planes[item.index];
    if (item.has_vector) {
      wideLine(item.nose_x, item.nose_y, item.vec_x, item.vec_y,
               radar::kAircraftTrackLineHalfWidth, item.color);
    }
    drawIcon(item.x, item.y, plane.shape, item.heading_idx, item.color);
    if (plane.military) {
      ring(item.x, item.y, radar::kAircraftHalfPx + 3, 10, pal().military, 26);
    }
  }
}

void drawClimbArrow(int x, int y_top, int8_t climb, uint16_t color) {
  // 7 px tall triangle, vertically centred in the tag line.
  const int cy = y_top + s_tag_line_h / 2 - s_by;
  if (climb > 0) {
    s_band.fillTriangle(x, cy + 3, x + 6, cy + 3, x + 3, cy - 4, color);
  } else {
    s_band.fillTriangle(x, cy - 3, x + 6, cy - 3, x + 3, cy + 4, color);
  }
}

void drawTags() {
  for (size_t d = s_item_count; d-- > 0;) {
    const PlaneItem& item = s_items[d];
    if (!item.has_tag || !rowsVisible(item.tag.y - 4, item.tag.y + item.tag.h + 4)) {
      continue;
    }
    const Aircraft& plane = s_planes[item.index];
    const Rect& r = item.tag;
    blendRect(r.x, r.y, r.w, r.h, pal().tag_back, 20);

    // Text hugs the symbol: right-aligned when the tag sits to its left.
    const bool align_right = r.x + r.w / 2 < item.x - radar::kAircraftHalfPx;
    const auto lineX = [&](int line_w) {
      return align_right ? r.x + r.w - kTagPadX - line_w : r.x + kTagPadX;
    };

    int y = r.y + kTagPadY;
    if (plane.callsign[0] != '\0') {
      const uint16_t color = plane.military ? pal().mil_callsign : pal().callsign;
      drawText(plane.callsign, lineX(textWidth(plane.callsign, kTagFont)), y,
               kTagFont, color, textdatum_t::top_left);
      y += s_tag_line_h;
    }
    if (item.show_type) {
      drawText(plane.type, lineX(textWidth(plane.type, kTagFont)), y,
               kTagFont, pal().tag_type, textdatum_t::top_left);
      y += s_tag_line_h;
    }
    if (item.show_alt) {
      const int tw = textWidth(plane.alt, kTagFont);
      const int arrow = item.climb != 0 ? kTagArrowW : 0;
      const int x = lineX(tw + arrow);
      drawText(plane.alt, x, y, kTagFont, pal().tag_alt,
               textdatum_t::top_left);
      if (item.climb != 0 && rowsVisible(y, y + s_tag_line_h)) {
        drawClimbArrow(x + tw + 3, y, item.climb, pal().tag_alt);
      }
    }
  }
}

void renderBand(int band) {
  s_by = band * kBandH;
  drawGrid();
  drawRunways();
  drawMapLabels();
  drawTrails();
  drawCenterMarker();
  drawScaleAndBadge();
  drawRimDots();
  drawCompassLabels();
  drawSymbols();
  drawTags();
}

/**
 * Software brightness: scale every pixel of the finished strip. Works with or
 * without the backlight wire (the web mirror is not dimmed).
 */
void dimBand(uint32_t level) {  // level 1..31 of 32
  const size_t pixels = static_cast<size_t>(kW) * kBandH;
  for (size_t i = 0; i < pixels; ++i) {
    const uint32_t v = swap16(s_px[i]);
    const uint32_t rb = (((v & 0xF81Fu) * level) >> 5) & 0xF81Fu;
    const uint32_t g = (((v & 0x07E0u) * level) >> 5) & 0x07E0u;
    s_px[i] = swap16(static_cast<uint16_t>(rb | g));
  }
}

void presentFrame() {
  uint32_t level = (static_cast<uint32_t>(services::device::brightness()) * 32u + 127u) / 255u;
  if (level < 1) {
    level = 1;
  }
  tft.startWrite();
  for (int band = 0; band < radar::kBandCount; ++band) {
    renderBand(band);
    if (level < 32) {
      dimBand(level);
    }
    s_band.pushSprite(&tft, 0, band * kBandH);
    tft.waitDMA();  // the strip buffer is reused for the next band
  }
  tft.endWrite();
}

}  // namespace

bool radarDisplayInit() {
  if (s_band_ready) {
    return true;
  }
  s_band.setColorDepth(lgfx::color_depth_t::rgb565_2Byte);
  s_px = static_cast<uint16_t*>(s_band.createSprite(kW, kBandH));
  if (s_px == nullptr) {
    Serial.println("radar: strip buffer alloc failed");
    return false;
  }
  s_band.setTextWrap(false);
  s_band_ready = true;
  return true;
}

void radarDisplayHidden() { s_frame_valid = false; }

void radarDisplayDraw() {
  if (!radarDisplayInit()) {
    return;
  }
  buildFrame();
  presentFrame();
}

void radarDisplayRefreshAircraft() { radarDisplayDraw(); }

size_t radarDisplayInRangeCount() { return s_item_count; }

size_t radarDisplayBeyondCount() { return s_dot_count; }

bool radarDisplayStreamFrame(FrameSink sink, void* ctx) {
  if (!s_band_ready || !s_frame_valid) {
    return false;
  }
  if (sink == nullptr) {
    return true;  // probe: a frame is available
  }
  const size_t pixels = static_cast<size_t>(kW) * kBandH;
  for (int band = 0; band < radar::kBandCount; ++band) {
    renderBand(band);
    for (size_t i = 0; i < pixels; ++i) {
      s_px[i] = swap16(s_px[i]);  // panel byte order -> little-endian for BMP
    }
    if (!sink(reinterpret_cast<const uint8_t*>(s_px), pixels * 2, ctx)) {
      return false;
    }
  }
  return true;
}

}  // namespace ui
