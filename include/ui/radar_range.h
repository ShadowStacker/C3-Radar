#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::radar {

/**
 * Range presets (label on ring 3 = ¾ of outer radius).
 *
 * Recommended for ADS-B on a small round display:
 *   5 km  — pattern / very local (airfield vicinity)
 *  10 km  — default; neighborhood spotting
 *  15 km  — wider local area
 *  25 km  — metro / regional picture
 *
 * Outer radius (for aircraft math) is ring-3 distance ÷ 0.75.
 */
struct RangePreset {
  /** Distance shown on ring 3 (¾ of outer radius), always stored in km. */
  float ring3_km;
  float outer_km;
};

constexpr float kRing3ToOuterKm = 4.0f / 3.0f;

constexpr RangePreset kRangePresets[] = {
    {5.0f, 5.0f * kRing3ToOuterKm},
    {10.0f, 10.0f * kRing3ToOuterKm},
    {15.0f, 15.0f * kRing3ToOuterKm},
    {25.0f, 25.0f * kRing3ToOuterKm},
};

constexpr size_t kRangePresetCount =
    sizeof(kRangePresets) / sizeof(kRangePresets[0]);

/** Load saved range and distance units from flash. Call once after boot. */
void rangeInit();
/** Cycle preset and save to flash. */
void rangeNext();
/** Select a preset by index and save to flash; false if out of range. */
bool rangeSet(uint8_t index);
const RangePreset& rangeCurrent();
uint8_t rangeIndex();
/** ADSB fetch radius (km): scaled to screen edge so beyond-ring dots have data. */
float fetchRadiusKm();

bool useMiles();
bool showRunways();
/** true = draw (and fetch) only aircraft flagged military by the ADS-B feed. */
bool militaryOnly();
void setUseMiles(bool on);
void setShowRunways(bool on);
void setMilitaryOnly(bool on);
/** What is drawn for each aircraft (bit mask, stored in NVS). */
enum AircraftShow : uint8_t {
  kShowCallsign = 1 << 0,
  kShowType = 1 << 1,
  kShowAltitude = 1 << 2,
  kShowSpeedLine = 1 << 3,  // the line ahead of the aircraft
  kShowTrail = 1 << 4,      // the dots behind it
  kAircraftShowAll = 0x1F,
};
uint8_t aircraftShow();
void setAircraftShow(uint8_t mask);

/** Map layer switches: bit mask of ui::map::Toggle. */
uint8_t mapLayers();
void setMapLayers(uint8_t mask);
void saveMilitaryFromPortal(const char* checkbox_value);
/** WiFi portal checkbox: "T" = miles, otherwise km. */
void saveMilesFromPortal(const char* checkbox_value);
void saveRunwaysFromPortal(const char* checkbox_value);
void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles);
void formatCurrentRing3Label(char* buf, size_t len);
/** Reset distance units to km (e.g. with WiFi credential wipe). */
void unitsReset();

}  // namespace ui::radar
