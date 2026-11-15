#pragma once

#include <cstddef>
#include <cstdint>

namespace services::adsb {

/** Previous position fixes kept per aircraft (one per fetch) for the trail. */
constexpr size_t kTrailPoints = 8;
/** Trail offsets are stored in units of 1/kTrailUnitsPerDeg degrees (~5.5 m). */
constexpr float kTrailUnitsPerDeg = 20000.0f;

constexpr int32_t kAltUnknown = INT32_MIN;
constexpr int32_t kAltGround = INT32_MIN + 1;

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  /** Age of the position fix at fetch time (ms), from the feed's seen_pos.
   *  Added to the elapsed time when dead-reckoning so the drawn position
   *  reflects the estimated current location, not the already-stale fix. */
  uint32_t pos_age_ms;
  /** ICAO 24-bit address (0 if the feed gave none); matches aircraft across fetches. */
  uint32_t hex;
  /** Barometric (or geometric) altitude in feet, kAltGround or kAltUnknown. */
  int32_t alt_ft;
  /** Climb / descent rate in ft/min (0 if unknown). */
  int16_t vrate_fpm;
  /** Older fixes relative to lat/lon, newest first. */
  int16_t trail_dlat[kTrailPoints];
  int16_t trail_dlon[kTrailPoints];
  uint8_t trail_count;
  /** data::aircraft_icons::Shape, picked from the ADS-B emitter category. */
  uint8_t shape;
  /** Feed database flag (dbFlags bit 0): operated by a military. */
  bool military;
  char callsign[9];
  char type[5];
  char alt[12];
};

constexpr size_t kMaxAircraft = 64;

/** Create the internal lock. Call once before any fetch/snapshot. */
void init();

/**
 * Copy the current aircraft into `out` (up to `max_out`) and report the fetch
 * timestamp, all under a lock — safe to call from a different thread than the
 * one running fetchUpdate(). Returns the number copied.
 */
size_t snapshotAircraft(Aircraft* out, size_t max_out,
                        unsigned long* out_last_update_ms);

size_t aircraftCount();

/**
 * millis() timestamp of the last successful fetch (0 before the first). The
 * stored lat/lon are the values as of this time; callers can dead-reckon
 * newer positions from track_deg/gs_knots and the elapsed time.
 */
unsigned long lastUpdateMs();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Ask the fetch task to poll again right away (range / location / filter changed). */
void requestRefresh();
/** Fetch-task side: sleep up to interval_ms, or less if a refresh was requested. */
void waitForNextFetch(unsigned long interval_ms);

/**
 * Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi.
 * With military_only, everything not flagged military is dropped while parsing.
 */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km,
                 bool military_only);

}  // namespace services::adsb
