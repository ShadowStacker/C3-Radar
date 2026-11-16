#include "ui/radar_range.h"

#include "ui/map_layer.h"
#include "ui/radar_theme.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsRangeKey[] = "rangeIdx";
constexpr char kPrefsMilesKey[] = "useMiles";
constexpr char kPrefsRunwaysKey[] = "showRwys";
constexpr char kPrefsMilitaryKey[] = "milOnly";
constexpr char kPrefsMapLayersKey[] = "mapLayers";
constexpr uint8_t kDefaultRangeIndex = 1;  // 10 km ring
constexpr float kKmPerMile = 1.609344f;

Preferences s_prefs;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_miles = false;
bool s_show_runways = true;
bool s_military_only = false;
uint8_t s_map_layers = ui::map::kToggleDefault;

void saveMilitaryOnly() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsMilitaryKey, s_military_only);
  s_prefs.end();
}

void saveRangeIndex() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsRangeKey, s_range_index);
  s_prefs.end();
}

void saveUseMiles() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsMilesKey, s_use_miles);
  s_prefs.end();
}

void saveShowRunways() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsRunwaysKey, s_show_runways);
  s_prefs.end();
}

bool portalCheckboxChecked(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  // WiFiManager checkbox submits its value= attribute ("T", or "F" if we prefilled F).
  if ((value[0] == 'T' || value[0] == 't' || value[0] == 'F' || value[0] == 'f') &&
      value[1] == '\0') {
    return true;
  }
  return strcmp(value, "on") == 0;
}

}  // namespace

void rangeInit() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = s_prefs.getUChar(kPrefsRangeKey, kDefaultRangeIndex);
  s_range_index =
      (saved < kRangePresetCount) ? saved : kDefaultRangeIndex;
  s_use_miles = s_prefs.getBool(kPrefsMilesKey, false);
  s_show_runways = s_prefs.getBool(kPrefsRunwaysKey, true);
  s_military_only = s_prefs.getBool(kPrefsMilitaryKey, false);
  s_map_layers = s_prefs.getUChar(kPrefsMapLayersKey, ui::map::kToggleDefault) &
                 ui::map::kToggleAll;
  s_prefs.end();
}

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  saveRangeIndex();
}

bool rangeSet(uint8_t index) {
  if (index >= kRangePresetCount) {
    return false;
  }
  if (index != s_range_index) {
    s_range_index = index;
    saveRangeIndex();
  }
  return true;
}

const RangePreset& rangeCurrent() { return kRangePresets[s_range_index]; }

uint8_t rangeIndex() { return s_range_index; }

float fetchRadiusKm() {
  const float outer_km = rangeCurrent().outer_km;
  const float screen_r_px =
      static_cast<float>(kCenterX - kBeyondRingScreenMarginPx);
  return outer_km * (screen_r_px / static_cast<float>(kGridOuterRadius));
}

bool useMiles() { return s_use_miles; }

bool showRunways() { return s_show_runways; }

bool militaryOnly() { return s_military_only; }

uint8_t mapLayers() { return s_map_layers; }

void setMapLayers(uint8_t mask) {
  mask &= ui::map::kToggleAll;
  if (mask == s_map_layers) {
    return;
  }
  s_map_layers = mask;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.putUChar(kPrefsMapLayersKey, s_map_layers);
    s_prefs.end();
  }
}

void setUseMiles(bool on) {
  if (on == s_use_miles) {
    return;
  }
  s_use_miles = on;
  saveUseMiles();
  Serial.printf("Distance units: %s\n", s_use_miles ? "miles" : "km");
}

void setShowRunways(bool on) {
  if (on == s_show_runways) {
    return;
  }
  s_show_runways = on;
  saveShowRunways();
  Serial.printf("Runway overlay: %s\n", s_show_runways ? "on" : "off");
}

void setMilitaryOnly(bool on) {
  if (on == s_military_only) {
    return;
  }
  s_military_only = on;
  saveMilitaryOnly();
  Serial.printf("Aircraft filter: %s\n", s_military_only ? "military only" : "all");
}

void saveMilesFromPortal(const char* checkbox_value) {
  setUseMiles(portalCheckboxChecked(checkbox_value));
}

void saveRunwaysFromPortal(const char* checkbox_value) {
  setShowRunways(portalCheckboxChecked(checkbox_value));
}

void saveMilitaryFromPortal(const char* checkbox_value) {
  setMilitaryOnly(portalCheckboxChecked(checkbox_value));
}

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles) {
  if (use_miles) {
    const int mi = static_cast<int>(lroundf(ring3_km / kKmPerMile));
    snprintf(buf, len, "%dmi", mi);
  } else {
    const int km = static_cast<int>(lroundf(ring3_km));
    snprintf(buf, len, "%dkm", km);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().ring3_km, s_use_miles);
}

void unitsReset() {
  s_use_miles = false;
  s_show_runways = true;
  s_military_only = false;
  s_map_layers = ui::map::kToggleDefault;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.remove(kPrefsMapLayersKey);
    s_prefs.remove(kPrefsMilesKey);
    s_prefs.remove(kPrefsRunwaysKey);
    s_prefs.remove(kPrefsMilitaryKey);
    s_prefs.end();
  }
}

}  // namespace ui::radar
