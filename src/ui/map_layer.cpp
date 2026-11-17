#include "ui/map_layer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/strip_canvas.h"
#include "ui/theme.h"

namespace ui::map {

namespace {

constexpr int kCx = radar::kCenterX;
constexpr int kCy = radar::kCenterY;
constexpr int kClipR = radar::kGridOuterRadius - 1;
constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.01745329252f;
constexpr size_t kLineHeader = 12;

const uint8_t* s_blob = nullptr;
uint32_t s_line_bytes = 0;
uint32_t s_total = 0;
Info s_info;
uint32_t s_generation = 0;

// Transform for the last prepare(): screen = centre + (unit + shift) * q / 65536
int32_t s_q16 = 0;
int32_t s_shift_x = 0;
int32_t s_shift_y = 0;
bool s_drawable = false;
uint8_t s_toggles = 0;

/** A line record that is on screen, with its screen-row span for strip culling. */
struct Visible {
  uint32_t offset;
  int16_t y0, y1;
};
constexpr size_t kMaxVisible = 1200;
Visible s_visible[kMaxVisible];
size_t s_visible_count = 0;
/** More visible records than fit the cache: draw straight from the map instead. */
bool s_overflow = false;

uint16_t rd16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
int16_t rdS16(const uint8_t* p) { return static_cast<int16_t>(rd16(p)); }
uint32_t rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline int toScreenX(int32_t ux) {
  return kCx + static_cast<int>(((ux + s_shift_x) * s_q16 + 0x8000) >> 16);
}
inline int toScreenY(int32_t uy) {
  return kCy - static_cast<int>(((uy + s_shift_y) * s_q16 + 0x8000) >> 16);
}

bool layerEnabled(uint8_t layer, uint8_t toggles) {
  switch (layer) {
    case kLayerCoast:
    case kLayerLake:
      return (toggles & kShowWater) != 0;
    case kLayerRiver:
      return (toggles & kShowRivers) != 0;
    case kLayerBorder:
      return (toggles & kShowBorders) != 0;
    case kLayerTrunk:
      return (toggles & kShowMainRoads) != 0;
    case kLayerMotorway:
      return (toggles & kShowMotorways) != 0;
    case kLayerRunway:
    case kLayerAirfield:
      return (toggles & kShowAirfields) != 0;
    case kLayerTown:
      return (toggles & kShowTowns) != 0;
    default:
      return false;
  }
}

canvas::LineStyle styleFor(uint8_t layer) {
  const theme::Palette& pal = theme::palette();
  switch (layer) {
    case kLayerCoast:
      return {pal.coast, 26, false};
    case kLayerLake:
      return {pal.lake, 22, false};
    case kLayerRiver:
      return {pal.river, 22, false};
    case kLayerBorder:
      return {pal.border, 24, true};
    case kLayerTrunk:
      return {pal.trunk, 20, false};
    case kLayerMotorway:
      return {pal.motorway, 24, false};
    default:  // runways
      return {pal.runway, 32, false};
  }
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len) {
  // Standard CRC-32 (zlib), nibble table.
  static const uint32_t kTable[16] = {
      0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
      0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
      0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    crc = kTable[crc & 0x0F] ^ (crc >> 4);
    crc = kTable[crc & 0x0F] ^ (crc >> 4);
  }
  return ~crc;
}

bool validate(const uint8_t* blob, size_t max_len, Info* out) {
  if (blob == nullptr || max_len < kHeaderSize || memcmp(blob, "C3MP", 4) != 0 ||
      blob[4] != kVersion) {
    return false;
  }
  const uint32_t total = rd32(blob + 8);
  const uint32_t line_bytes = rd32(blob + 24);
  if (total < kHeaderSize || total > max_len || line_bytes > total - kHeaderSize) {
    return false;
  }
  if (crc32(blob + kHeaderSize, total - kHeaderSize) != rd32(blob + 12)) {
    return false;
  }
  // Walk the records once so a damaged blob can never run off the end later.
  uint32_t off = kHeaderSize;
  const uint32_t lines_end = kHeaderSize + line_bytes;
  while (off < lines_end) {
    if (lines_end - off < kLineHeader) {
      return false;
    }
    const uint32_t n = rd16(blob + off + 2);
    const uint32_t size = kLineHeader + n * 4;
    if (size > lines_end - off) {
      return false;
    }
    off += size;
  }
  while (off < total) {
    if (total - off < 7) {
      return false;
    }
    const uint32_t size = 7u + blob[off + 6];
    if (size > total - off) {
      return false;
    }
    off += size;
  }

  if (out != nullptr) {
    out->valid = true;
    out->lat = static_cast<int32_t>(rd32(blob + 16)) * 1e-7;
    out->lon = static_cast<int32_t>(rd32(blob + 20)) * 1e-7;
    out->bytes = total;
  }
  return true;
}

bool attach(const uint8_t* blob, size_t max_len) {
  detach();
  Info info;
  if (!validate(blob, max_len, &info)) {
    return false;
  }
  const uint32_t total = info.bytes;
  const uint32_t line_bytes = rd32(blob + 24);
  s_blob = blob;
  s_total = total;
  s_line_bytes = line_bytes;
  s_info = info;
  ++s_generation;
  return true;
}

void detach() {
  if (s_blob != nullptr) {
    ++s_generation;
  }
  s_blob = nullptr;
  s_total = 0;
  s_line_bytes = 0;
  s_info = Info{};
  s_visible_count = 0;
  s_drawable = false;
}

const Info& info() { return s_info; }

uint32_t generation() { return s_generation; }

float centerOffsetKm() {
  if (s_blob == nullptr) {
    return 0.0f;
  }
  const float lat = static_cast<float>(services::location::lat());
  const float dx = static_cast<float>(s_info.lon - services::location::lon()) *
                   kKmPerDeg * cosf(lat * kDegToRad);
  const float dy = static_cast<float>(s_info.lat - services::location::lat()) * kKmPerDeg;
  return sqrtf(dx * dx + dy * dy);
}

void prepare(uint8_t toggles) {
  s_visible_count = 0;
  s_overflow = false;
  s_drawable = false;
  s_toggles = toggles;
  if (s_blob == nullptr || toggles == 0 || centerOffsetKm() > kMaxCenterOffsetKm) {
    return;
  }

  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) /
                          radar::rangeCurrent().outer_km;
  const float px_per_unit = px_per_km * kMetersPerUnit / 1000.0f;
  s_q16 = static_cast<int32_t>(lroundf(px_per_unit * 65536.0f));
  // Map centre relative to the radar centre, in map units.
  const float lat = static_cast<float>(services::location::lat());
  const float units_per_deg = kKmPerDeg * 1000.0f / kMetersPerUnit;
  s_shift_x = static_cast<int32_t>(
      lroundf(static_cast<float>(s_info.lon - services::location::lon()) *
              units_per_deg * cosf(lat * kDegToRad)));
  s_shift_y = static_cast<int32_t>(lroundf(
      static_cast<float>(s_info.lat - services::location::lat()) * units_per_deg));
  s_drawable = true;

  const int lo = kCx - kClipR;
  const int hi = kCx + kClipR;
  uint32_t off = kHeaderSize;
  const uint32_t end = kHeaderSize + s_line_bytes;
  while (off < end) {
    const uint8_t* rec = s_blob + off;
    const uint32_t n = rd16(rec + 2);
    const uint32_t this_off = off;
    off += kLineHeader + n * 4;
    if (n < 2 || !layerEnabled(rec[0], toggles)) {
      continue;
    }
    const int x0 = toScreenX(rdS16(rec + 4));
    const int x1 = toScreenX(rdS16(rec + 8));
    const int y_top = toScreenY(rdS16(rec + 10));     // max north = smallest y
    const int y_bottom = toScreenY(rdS16(rec + 6));
    if (x1 < lo || x0 > hi || y_bottom < lo || y_top > hi) {
      continue;
    }
    if (s_visible_count >= kMaxVisible) {
      // Stopping here would silently drop the layers stored last in the file
      // (roads, runways), so draw from the whole map instead.
      s_overflow = true;
      s_visible_count = 0;
      break;
    }
    Visible& v = s_visible[s_visible_count++];
    v.offset = this_off;
    v.y0 = static_cast<int16_t>(std::max(y_top, 0));
    v.y1 = static_cast<int16_t>(std::min(y_bottom, radar::kSize - 1));
  }
}

void drawRecord(const uint8_t* rec) {
  const uint32_t n = rd16(rec + 2);
  const canvas::LineStyle style = styleFor(rec[0]);
  const uint8_t* pt = rec + kLineHeader;
  int px = toScreenX(rdS16(pt));
  int py = toScreenY(rdS16(pt + 2));
  for (uint32_t k = 1; k < n; ++k) {
    pt += 4;
    const int x = toScreenX(rdS16(pt));
    const int y = toScreenY(rdS16(pt + 2));
    // Zoomed out, many points land within a pixel of each other: skip them
    // (but always close the line at its last point).
    if (k + 1 < n && std::abs(x - px) + std::abs(y - py) < 2) {
      continue;
    }
    canvas::thinLine(px, py, x, y, style, kClipR);
    px = x;
    py = y;
  }
}

void drawLines() {
  if (!s_drawable) {
    return;
  }
  const int band_top = canvas::s_by - 1;
  const int band_bottom = canvas::s_by + canvas::kBandH;
  if (!s_overflow) {
    for (size_t i = 0; i < s_visible_count; ++i) {
      const Visible& v = s_visible[i];
      if (v.y1 >= band_top && v.y0 <= band_bottom) {
        drawRecord(s_blob + v.offset);
      }
    }
    return;
  }
  // Very dense map at a wide range: walk every record, culling by its bounds.
  const int lo = kCx - kClipR;
  const int hi = kCx + kClipR;
  uint32_t off = kHeaderSize;
  const uint32_t end = kHeaderSize + s_line_bytes;
  while (off < end) {
    const uint8_t* rec = s_blob + off;
    const uint32_t n = rd16(rec + 2);
    off += kLineHeader + n * 4;
    if (n < 2 || !layerEnabled(rec[0], s_toggles)) {
      continue;
    }
    const int y_top = toScreenY(rdS16(rec + 10));
    const int y_bottom = toScreenY(rdS16(rec + 6));
    if (y_bottom < band_top || y_top > band_bottom) {
      continue;
    }
    const int x0 = toScreenX(rdS16(rec + 4));
    const int x1 = toScreenX(rdS16(rec + 8));
    if (x1 < lo || x0 > hi || y_bottom < lo || y_top > hi) {
      continue;
    }
    drawRecord(rec);
  }
}

size_t collectLabels(LabelCandidate* out, size_t max_out) {
  if (!s_drawable || max_out == 0) {
    return 0;
  }
  // Villages only when zoomed in enough for them to be useful.
  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) /
                          radar::rangeCurrent().outer_km;
  const uint8_t min_town_rank = px_per_km >= 9.0f ? 1 : 2;
  const int limit = radar::kGridOuterRadius - 14;

  size_t count = 0;
  uint32_t off = kHeaderSize + s_line_bytes;
  while (off < s_total && count < max_out) {
    const uint8_t* rec = s_blob + off;
    const uint8_t len = rec[6];
    off += 7u + len;
    const uint8_t layer = rec[0];
    if (len == 0 || !layerEnabled(layer, s_toggles)) {
      continue;
    }
    if (layer == kLayerTown && rec[1] < min_town_rank) {
      continue;
    }
    const int x = toScreenX(rdS16(rec + 2));
    const int y = toScreenY(rdS16(rec + 4));
    const int dx = x - kCx;
    const int dy = y - kCy;
    if (dx * dx + dy * dy > limit * limit) {
      continue;
    }
    LabelCandidate& c = out[count++];
    c.x = static_cast<int16_t>(x);
    c.y = static_cast<int16_t>(y);
    c.rank = rec[1];
    c.airfield = layer == kLayerAirfield;
    const size_t copy = std::min<size_t>(len, sizeof(c.text) - 1);
    memcpy(c.text, rec + 7, copy);
    c.text[copy] = '\0';
  }
  return count;
}

}  // namespace ui::map
