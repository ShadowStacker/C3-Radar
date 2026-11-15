#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::runway {

/** Runway centre line in screen pixels, already clipped to the outer ring. */
struct Segment {
  int16_t x0, y0, x1, y1;
};

/** Airport ident anchor (bottom-centre of the text). */
struct Label {
  int16_t x, y;
  char ident[5];
};

constexpr size_t kMaxSegments = 48;
constexpr size_t kMaxLabels = 12;

/**
 * Project every large-airport runway in range for the current location and
 * range preset. Walks the whole embedded database, so call it only when the
 * location / range / runway setting changes - not per frame.
 */
void collect(Segment* segments, size_t* segment_count, Label* labels,
             size_t* label_count);

}  // namespace ui::runway
