#pragma once

#include <cstdint>

/** Layout + palette for the 360x360 round radar. */
namespace ui::radar {

constexpr int kSize = 360;
constexpr int kCenterX = kSize / 2;
constexpr int kCenterY = kSize / 2;

/** Frames are composed in horizontal strips of this height (RGB565), so the
 *  ESP32-C3 never needs a full 360x360 frame buffer (259 KB). */
constexpr int kBandHeight = 45;
constexpr int kBandCount = kSize / kBandHeight;

/** Outermost grid ring; the bezel band outside it holds ticks and N/E/S/W. */
constexpr int kGridOuterRadius = 154;
constexpr int kRingCount = 4;

/** Compass ticks every 10 degrees (longer every 30) just outside the outer ring. */
constexpr int kTickInnerGapPx = 3;
constexpr int kTickLenPx = 5;
constexpr int kTickLenMajorPx = 9;
/** Radius of the centre of the N/E/S/W letters. */
constexpr int kCardinalRadius = 167;

constexpr int kCenterDotRadius = 3;

/** Aircraft icon half-extent used for layout (icon cell is 25 px). */
constexpr int kAircraftHalfPx = 12;
/** Distance from the aircraft position to its nose (start of the track vector). */
constexpr int kAircraftNoseLenPx = 11;
/** Track vector: ground distance covered in this many seconds at current gs. */
constexpr float kAircraftTrackHorizonSec = 60.0f;
constexpr int kAircraftSpeedLineMinPx = 3;
/** Track line length uses this outer_km, not the active range preset. */
constexpr float kAircraftTrackRefOuterKm = 13.3f;
constexpr float kAircraftTrackLengthScale = 1.5f / 5.0f;
constexpr float kAircraftTrackLineHalfWidth = 0.9f;

/** Keep the symbol inside the outer ring by at least this inset (px). */
constexpr int kAircraftInsideRingInsetPx = kAircraftHalfPx + 2;
/** Gap from the icon to its tag block (px). */
constexpr int kAircraftLabelGapPx = 2;

/** Beyond-ring traffic: bearing cues on the screen rim. */
constexpr int kBeyondRingDotRadiusPx = 4;
constexpr int kBeyondRingScreenMarginPx = 6;

constexpr float kRunwayLineHalfWidth = 1.5f;
constexpr int kRunwayLabelGapPx = 4;

struct Rgb {
  uint8_t r, g, b;
};

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t rgb565(Rgb c) { return rgb565(c.r, c.g, c.b); }

// Colours live in ui/theme.h (they depend on the selected theme).

}  // namespace ui::radar
