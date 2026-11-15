#include "ui/runway_overlay.h"

#include <cmath>
#include <cstring>

#include "data/large_airports.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::runway {
namespace {

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 3.14159265f / 180.0f;

float e7ToDeg(int32_t e7) { return static_cast<float>(e7) * 1e-7f; }

struct Projection {
  float center_lat;
  float center_lon;
  float cos_lat;
  float px_per_km;

  /** Pixel offset from the screen centre (x east, y south). */
  void toPx(float lat, float lon, float* x, float* y) const {
    *x = (lon - center_lon) * kKmPerDeg * cos_lat * px_per_km;
    *y = -(lat - center_lat) * kKmPerDeg * px_per_km;
  }
};

/** Clip a centre-relative segment to a disc of radius r. False if fully outside. */
bool clipToDisc(float* x0, float* y0, float* x1, float* y1, float r) {
  const float dx = *x1 - *x0;
  const float dy = *y1 - *y0;
  const float a = dx * dx + dy * dy;
  if (a < 0.25f) {
    return false;  // shorter than half a pixel at this zoom
  }
  const float b = 2.0f * (*x0 * dx + *y0 * dy);
  const float c = *x0 * *x0 + *y0 * *y0 - r * r;
  const float disc = b * b - 4.0f * a * c;
  if (disc <= 0.0f) {
    return false;
  }
  const float root = sqrtf(disc);
  float t0 = (-b - root) / (2.0f * a);
  float t1 = (-b + root) / (2.0f * a);
  if (t1 <= 0.0f || t0 >= 1.0f) {
    return false;
  }
  if (t0 < 0.0f) t0 = 0.0f;
  if (t1 > 1.0f) t1 = 1.0f;
  const float sx = *x0;
  const float sy = *y0;
  *x0 = sx + dx * t0;
  *y0 = sy + dy * t0;
  *x1 = sx + dx * t1;
  *y1 = sy + dy * t1;
  return true;
}

void makeLabel(const Projection& proj, const data::large_airports::Airport& ap,
               Label* out) {
  float x = 0.0f;
  float y = 0.0f;
  proj.toPx(e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), &x, &y);
  float len = sqrtf(x * x + y * y);
  // Airports whose reference point is off-scope get their label pulled back
  // inside the ring so it stays readable.
  const float max_r = static_cast<float>(radar::kGridOuterRadius - 22);
  if (len > max_r) {
    x *= max_r / len;
    y *= max_r / len;
    len = max_r;
  }
  const float gap = static_cast<float>(radar::kRunwayLabelGapPx);
  if (len >= 20.0f) {
    x += x / len * gap;
    y += y / len * gap;
  } else {
    // Airport at (or next to) the scope centre: lift the label clear of the
    // centre marker instead of printing over it.
    x = 0.0f;
    y = -14.0f;
  }
  out->x = static_cast<int16_t>(radar::kCenterX + lroundf(x));
  out->y = static_cast<int16_t>(radar::kCenterY + lroundf(y));
  memcpy(out->ident, ap.ident, sizeof(out->ident));
  out->ident[sizeof(out->ident) - 1] = '\0';
}

}  // namespace

void collect(Segment* segments, size_t* segment_count, Label* labels,
             size_t* label_count) {
  *segment_count = 0;
  *label_count = 0;
  if (!radar::showRunways()) {
    return;
  }

  Projection proj;
  proj.center_lat = static_cast<float>(services::location::lat());
  proj.center_lon = static_cast<float>(services::location::lon());
  proj.cos_lat = cosf(proj.center_lat * kDegToRad);
  proj.px_per_km = static_cast<float>(radar::kGridOuterRadius) /
                   radar::rangeCurrent().outer_km;

  // Cheap pre-filter in degrees before any projection: an airport further
  // than the scope radius plus the longest runway cannot contribute.
  const float reach_km = radar::rangeCurrent().outer_km + 8.0f;
  const float reach_lat_deg = reach_km / kKmPerDeg;
  const float reach_lon_deg =
      reach_km / (kKmPerDeg * (proj.cos_lat > 0.05f ? proj.cos_lat : 0.05f));
  const float ring_px = static_cast<float>(radar::kGridOuterRadius - 1);

  uint16_t labelled[kMaxLabels];

  for (size_t i = 0; i < data::large_airports::kRunwayCount; ++i) {
    const auto& rw = data::large_airports::kRunways[i];
    const auto& ap = data::large_airports::kAirports[rw.airport_idx];
    if (fabsf(e7ToDeg(ap.lat_e7) - proj.center_lat) > reach_lat_deg ||
        fabsf(e7ToDeg(ap.lon_e7) - proj.center_lon) > reach_lon_deg) {
      continue;
    }

    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    proj.toPx(e7ToDeg(rw.le_lat_e7), e7ToDeg(rw.le_lon_e7), &x0, &y0);
    proj.toPx(e7ToDeg(rw.he_lat_e7), e7ToDeg(rw.he_lon_e7), &x1, &y1);
    if (!clipToDisc(&x0, &y0, &x1, &y1, ring_px)) {
      continue;
    }
    if (*segment_count >= kMaxSegments) {
      break;
    }
    Segment& seg = segments[(*segment_count)++];
    seg.x0 = static_cast<int16_t>(radar::kCenterX + lroundf(x0));
    seg.y0 = static_cast<int16_t>(radar::kCenterY + lroundf(y0));
    seg.x1 = static_cast<int16_t>(radar::kCenterX + lroundf(x1));
    seg.y1 = static_cast<int16_t>(radar::kCenterY + lroundf(y1));

    bool have_label = false;
    for (size_t l = 0; l < *label_count; ++l) {
      if (labelled[l] == rw.airport_idx) {
        have_label = true;
        break;
      }
    }
    if (!have_label && *label_count < kMaxLabels) {
      labelled[*label_count] = rw.airport_idx;
      makeLabel(proj, ap, &labels[*label_count]);
      ++(*label_count);
    }
  }
}

}  // namespace ui::runway
