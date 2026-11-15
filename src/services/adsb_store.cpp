#include "services/adsb_store.h"

#include <Arduino.h>

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "data/aircraft_icons.h"

namespace services::adsb {

namespace {

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
unsigned long s_last_update_ms = 0;

#ifdef ARDUINO
SemaphoreHandle_t s_mutex = nullptr;
void lock() {
  if (s_mutex != nullptr) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
  }
}
void unlock() {
  if (s_mutex != nullptr) {
    xSemaphoreGive(s_mutex);
  }
}
#else
void lock() {}
void unlock() {}
#endif

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"].is<const char*>()) {
    const char* s = plane["alt_baro"].as<const char*>();
    if (strcmp(s, "ground") == 0) {
      strncpy(out, "GND", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
  }
}

uint32_t parseHex(const JsonObject& plane) {
  if (!plane["hex"].is<const char*>()) {
    return 0;
  }
  const char* s = plane["hex"].as<const char*>();
  if (*s == '~') {
    ++s;  // non-ICAO (TIS-B / anonymised) address
  }
  return static_cast<uint32_t>(strtoul(s, nullptr, 16));
}

uint8_t pickShape(const JsonObject& plane) {
  if (!plane["category"].is<const char*>()) {
    return data::aircraft_icons::kShapeJet;
  }
  const char* c = plane["category"].as<const char*>();
  if (strcmp(c, "A7") == 0) {
    return data::aircraft_icons::kShapeHeli;
  }
  // A1 = light (< 15500 lb), B1 = glider, B4 = ultralight.
  if (strcmp(c, "A1") == 0 || strcmp(c, "B1") == 0 || strcmp(c, "B4") == 0) {
    return data::aircraft_icons::kShapeLight;
  }
  return data::aircraft_icons::kShapeJet;
}

void fillAltitude(Aircraft* ac, const JsonObject& plane) {
  ac->alt_ft = kAltUnknown;
  if (isOnGround(plane)) {
    ac->alt_ft = kAltGround;
  } else {
    float alt = 0.0f;
    if (readJsonFloat(plane, "alt_baro", &alt) ||
        readJsonFloat(plane, "alt_geom", &alt)) {
      ac->alt_ft = static_cast<int32_t>(lroundf(alt));
    }
  }
  float rate = 0.0f;
  if (!readJsonFloat(plane, "baro_rate", &rate)) {
    readJsonFloat(plane, "geom_rate", &rate);
  }
  if (rate > 30000.0f) rate = 30000.0f;
  if (rate < -30000.0f) rate = -30000.0f;
  ac->vrate_fpm = static_cast<int16_t>(rate);
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}


/**
 * Give `ac` a trail: the previous fix of the same airframe becomes the newest
 * trail point and its own trail shifts back one slot. Offsets are re-based on
 * the new position so the renderer only needs the current lat/lon.
 */
void inheritTrail(Aircraft* ac, const Aircraft* prev_list, size_t prev_count) {
  ac->trail_count = 0;
  if (ac->hex == 0) {
    return;
  }
  const Aircraft* prev = nullptr;
  for (size_t i = 0; i < prev_count; ++i) {
    if (prev_list[i].hex == ac->hex) {
      prev = &prev_list[i];
      break;
    }
  }
  if (prev == nullptr) {
    return;
  }

  const long dlat = lroundf((prev->lat - ac->lat) * kTrailUnitsPerDeg);
  const long dlon = lroundf((prev->lon - ac->lon) * kTrailUnitsPerDeg);
  if (dlat == 0 && dlon == 0) {
    // Same fix as last time (no new position yet): keep the trail as it was.
    ac->trail_count = prev->trail_count;
    memcpy(ac->trail_dlat, prev->trail_dlat, sizeof(ac->trail_dlat));
    memcpy(ac->trail_dlon, prev->trail_dlon, sizeof(ac->trail_dlon));
    return;
  }

  const auto fits = [](long v) { return v >= INT16_MIN && v <= INT16_MAX; };
  if (!fits(dlat) || !fits(dlon)) {
    return;  // jumped too far to be a real track; start over
  }
  ac->trail_dlat[0] = static_cast<int16_t>(dlat);
  ac->trail_dlon[0] = static_cast<int16_t>(dlon);
  size_t n = 1;
  for (size_t i = 0; i < prev->trail_count && n < kTrailPoints; ++i) {
    const long la = dlat + prev->trail_dlat[i];
    const long lo = dlon + prev->trail_dlon[i];
    if (!fits(la) || !fits(lo)) {
      break;
    }
    ac->trail_dlat[n] = static_cast<int16_t>(la);
    ac->trail_dlon[n] = static_cast<int16_t>(lo);
    ++n;
  }
  ac->trail_count = static_cast<uint8_t>(n);
}

}  // namespace

void init() {
#ifdef ARDUINO
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
#endif
}

size_t aircraftCount() { return s_aircraft_count; }

unsigned long lastUpdateMs() { return s_last_update_ms; }

void buildFilter(JsonDocument& filter) {
  JsonObject f = filter["ac"].add<JsonObject>();
  for (const char* key :
       {"lat", "lon", "true_heading", "mag_heading", "track", "dir", "gs",
        "tas", "ias", "alt_baro", "alt_geom", "baro_rate", "geom_rate",
        "seen_pos", "flight", "hex", "t", "category", "dbFlags"}) {
    f[key] = true;
  }
}

size_t parseAircraft(JsonArray ac, Aircraft* out, size_t max_out,
                     bool military_only) {
  size_t n = 0;
  if (ac.isNull()) {
    return 0;
  }
  for (JsonObject plane : ac) {
    if (n >= max_out) {
      break;
    }
    if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
      continue;
    }
    if (isOnGround(plane) && !config::kAdsbShowGroundAircraft) {
      continue;
    }
    // dbFlags bit 0 = military (readsb / tar1090 aircraft database).
    const bool military = (plane["dbFlags"].as<unsigned>() & 1u) != 0;
    if (military_only && !military) {
      continue;
    }
    out[n].military = military;
    out[n].hex = parseHex(plane);
    out[n].shape = pickShape(plane);
    out[n].trail_count = 0;
    fillAltitude(&out[n], plane);

    out[n].lat = plane["lat"].as<float>();
    out[n].lon = plane["lon"].as<float>();
    out[n].nose_deg = pickNoseHeading(plane);
    out[n].track_deg = pickTrackHeading(plane);
    out[n].gs_knots = pickGroundSpeed(plane);

    // seen_pos: seconds since this position was measured. Use it as the
    // dead-reckoning age offset, capped so a very stale fix isn't flung far.
    float seen_pos = 0.0f;
    readJsonFloat(plane, "seen_pos", &seen_pos);
    if (seen_pos < 0.0f) seen_pos = 0.0f;
    if (seen_pos > 30.0f) seen_pos = 30.0f;
    out[n].pos_age_ms = static_cast<uint32_t>(seen_pos * 1000.0f);

    fillTagFields(&out[n], plane);
    ++n;
  }
  return n;
}

void publish(Aircraft* list, size_t count) {
  if (count > kMaxAircraft) {
    count = kMaxAircraft;
  }
  lock();
  for (size_t i = 0; i < count; ++i) {
    inheritTrail(&list[i], s_aircraft, s_aircraft_count);
  }
  for (size_t i = 0; i < count; ++i) {
    s_aircraft[i] = list[i];
  }
  s_aircraft_count = count;
  s_last_update_ms = millis();  // base time for dead-reckoning
  unlock();
}

size_t snapshotAircraft(Aircraft* out, size_t max_out,
                        unsigned long* out_last_update_ms) {
  lock();
  const size_t count =
      s_aircraft_count < max_out ? s_aircraft_count : max_out;
  for (size_t i = 0; i < count; ++i) {
    out[i] = s_aircraft[i];
  }
  if (out_last_update_ms != nullptr) {
    *out_last_update_ms = s_last_update_ms;
  }
  unlock();
  return count;
}

}  // namespace services::adsb
