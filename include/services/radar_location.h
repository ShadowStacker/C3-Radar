#pragma once

#include <cstddef>

namespace services::location {

/** Load saved lat/lon from NVS, or use config defaults. Call once before WiFi setup. */
void init();

/** Factory defaults when nothing is stored (also used for portal field prefill). */
double lat();
double lon();

/** Parse portal strings, validate, persist to NVS, update runtime values. */
bool saveFromStrings(const char* lat_str, const char* lon_str);

/** Validate, persist to NVS and update runtime values. */
bool save(double lat, double lon);

/** A named spot the radar can be centred on. */
struct Place {
  char name[20];
  double lat;
  double lon;
};
constexpr size_t kMaxPlaces = 8;

size_t placeCount();
const Place& place(size_t index);
/** Index of the saved place the radar is currently centred on, or -1. */
int activePlace();
/** Save a place (printable ASCII name). False if the list is full or the input is bad. */
bool addPlace(const char* name, double lat, double lon);
bool removePlace(size_t index);
/** Centre the radar on a saved place. */
bool selectPlace(size_t index);

/** Clear stored coordinates (e.g. with WiFi credential reset). */
void clear();

}  // namespace services::location
