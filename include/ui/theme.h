#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Colour themes. A theme is described by a short list of colours the user can
 * understand (ThemeSpec); every other shade the radar draws with is derived
 * from those, so presets and the custom editor share one code path.
 */
namespace ui::theme {

enum Slot : uint8_t {
  kSlotBackground = 0,  // outside the scope
  kSlotScope,           // inside the outer ring
  kSlotGrid,            // rings, crosshair, ticks, range label
  kSlotText,            // compass letters, centre mark
  kSlotAircraft,        // aircraft when not coloured by altitude
  kSlotCallsign,
  kSlotType,
  kSlotAltitude,        // altitude text
  kSlotAirports,        // runways and airport codes
  kSlotWater,
  kSlotRoads,
  kSlotBorders,
  kSlotTowns,
  kSlotMilitary,
  kSlotCount
};

struct ThemeSpec {
  const char* name;
  uint32_t rgb[kSlotCount];  // 0xRRGGBB
  bool altitude_colors;      // colour aircraft by altitude instead of kSlotAircraft
};

constexpr size_t kPresetCount = 5;
extern const ThemeSpec kPresets[kPresetCount];
/** Theme id of the user's own colours. */
constexpr uint8_t kCustomId = 255;

/** Ready-to-draw RGB565 colours derived from the active theme. */
struct Palette {
  uint16_t bezel, scope, ring, ring_outer, cross, tick;
  uint16_t label, north, scale, center;
  uint16_t tag_back, callsign, tag_type, tag_alt;
  uint16_t runway, runway_label, town_major, town_minor;
  uint16_t military, mil_callsign, badge_back, badge_text, outline;
  uint16_t aircraft, aircraft_unknown;
  uint16_t coast, lake, river, border, motorway, trunk;
  bool altitude_colors;
};

/** Altitude colour scale (feet -> colour), linearly blended between stops. */
struct AltitudeStop {
  int32_t alt_ft;
  uint8_t r, g, b;
};
constexpr AltitudeStop kAltitudeStops[] = {
    {0, 255, 120, 40},      {3000, 255, 205, 40},   {8000, 120, 232, 70},
    {15000, 40, 222, 170},  {25000, 70, 175, 255},  {35000, 160, 135, 255},
    {42000, 235, 115, 255},
};
constexpr size_t kAltitudeStopCount = sizeof(kAltitudeStops) / sizeof(kAltitudeStops[0]);

/** Load the saved choice. Call once at boot. */
void init();
const Palette& palette();

/** Active theme: 0..kPresetCount-1, or kCustomId. */
uint8_t current();
bool select(uint8_t id);

/** The user's custom colours (starts as a copy of the default theme). */
const ThemeSpec& custom();
void setCustom(const uint32_t rgb[kSlotCount], bool altitude_colors);

}  // namespace ui::theme
