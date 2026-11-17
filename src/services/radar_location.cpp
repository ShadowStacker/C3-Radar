#include "services/radar_location.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";
constexpr char kKeyPlaces[] = "places";
constexpr char kKeyPlaceCount[] = "placeCount";

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;

bool parseCoord(const char* text, double* out) { return parseNumber(text, out); }

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void persist(double lat, double lon) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyLat, lat);
  prefs.putDouble(kKeyLon, lon);
  prefs.end();
  s_lat = lat;
  s_lon = lon;
}

Place s_places[kMaxPlaces];
size_t s_place_count = 0;

void persistPlaces() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.putUChar(kKeyPlaceCount, static_cast<uint8_t>(s_place_count));
  prefs.putBytes(kKeyPlaces, s_places, sizeof(s_places));
  prefs.end();
}

}  // namespace

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double lat = prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double lon = prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    if (validLatLon(lat, lon)) {
      s_lat = lat;
      s_lon = lon;
    }
  }
  Place stored[kMaxPlaces];
  if (prefs.getBytes(kKeyPlaces, stored, sizeof(stored)) == sizeof(stored)) {
    const size_t count = prefs.getUChar(kKeyPlaceCount, 0);
    s_place_count = count <= kMaxPlaces ? count : 0;
    memcpy(s_places, stored, sizeof(stored));
    for (size_t i = 0; i < kMaxPlaces; ++i) {
      s_places[i].name[sizeof(s_places[i].name) - 1] = '\0';
    }
  }
  prefs.end();
}

double lat() { return s_lat; }

double lon() { return s_lon; }

bool saveFromStrings(const char* lat_str, const char* lon_str) {
  double lat = 0.0;
  double lon = 0.0;
  if (!parseCoord(lat_str, &lat) || !parseCoord(lon_str, &lon)) {
    return false;
  }
  if (!validLatLon(lat, lon)) {
    return false;
  }
  persist(lat, lon);
  Serial.printf("Radar location saved: %.6f, %.6f\n", lat, lon);
  return true;
}

bool save(double lat, double lon) {
  if (!validLatLon(lat, lon)) {
    return false;
  }
  if (lat != s_lat || lon != s_lon) {
    persist(lat, lon);
    Serial.printf("Radar location saved: %.6f, %.6f\n", lat, lon);
  }
  return true;
}

bool parseNumber(const char* text, double* out) {
  if (text == nullptr) {
    return false;
  }
  const char* p = text;
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  bool negative = false;
  if (*p == '+' || *p == '-') {
    negative = *p == '-';
    ++p;
  }
  double value = 0.0;
  int digits = 0;
  for (; *p >= '0' && *p <= '9'; ++p, ++digits) {
    value = value * 10.0 + (*p - '0');
  }
  if (*p == '.' || *p == ',') {  // some keyboards type a decimal comma
    ++p;
    double scale = 0.1;
    for (; *p >= '0' && *p <= '9'; ++p, ++digits) {
      value += (*p - '0') * scale;
      scale *= 0.1;
    }
  }
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  if (digits == 0 || digits > 18 || *p != '\0') {
    return false;
  }
  *out = negative ? -value : value;
  return true;
}

size_t placeCount() { return s_place_count; }

const Place& place(size_t index) { return s_places[index]; }

int activePlace() {
  for (size_t i = 0; i < s_place_count; ++i) {
    if (fabs(s_places[i].lat - s_lat) < 1e-5 && fabs(s_places[i].lon - s_lon) < 1e-5) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool addPlace(const char* name, double lat, double lon) {
  if (s_place_count >= kMaxPlaces || !validLatLon(lat, lon) || name == nullptr) {
    return false;
  }
  Place p = {};
  size_t n = 0;
  for (const char* c = name; *c != '\0' && n + 1 < sizeof(p.name); ++c) {
    if (*c < 0x20 || *c > 0x7E || *c == '<' || *c == '>' || *c == '"' || *c == '&') {
      continue;
    }
    if (*c == ' ' && n == 0) {
      continue;
    }
    p.name[n++] = *c;
  }
  while (n > 0 && p.name[n - 1] == ' ') {
    --n;
  }
  p.name[n] = '\0';
  if (n == 0) {
    return false;
  }
  p.lat = lat;
  p.lon = lon;
  s_places[s_place_count++] = p;
  persistPlaces();
  return true;
}

bool removePlace(size_t index) {
  if (index >= s_place_count) {
    return false;
  }
  for (size_t i = index; i + 1 < s_place_count; ++i) {
    s_places[i] = s_places[i + 1];
  }
  --s_place_count;
  persistPlaces();
  return true;
}

bool selectPlace(size_t index) {
  return index < s_place_count && save(s_places[index].lat, s_places[index].lon);
}

void clear() {
  s_place_count = 0;
  persistPlaces();
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyLat);
  prefs.remove(kKeyLon);
  prefs.end();
  s_lat = config::kDefaultRadarLat;
  s_lon = config::kDefaultRadarLon;
}

}  // namespace services::location
