#pragma once

#include <cstddef>
#include <cstdint>

/** Pre-rotated aircraft silhouettes (see scripts/build_aircraft_icons.py). */
namespace data::aircraft_icons {

constexpr int kIconSize = 25;
constexpr int kHeadingCount = 72;  // 5 degree steps
constexpr int kShapeCount = 3;

enum Shape : uint8_t { kShapeJet = 0, kShapeLight = 1, kShapeHeli = 2 };

/** One byte per pixel: high nibble = outline coverage, low nibble = fill (0..15). */
extern const uint8_t kIcons[kShapeCount][kHeadingCount][kIconSize * kIconSize];

}  // namespace data::aircraft_icons
