#pragma once

#include <ArduinoJson.h>

#include "services/adsb_client.h"

/** Feed parsing + shared aircraft buffer (no network code; also built on a PC). */
namespace services::adsb {

/** Fill `filter` so deserializeJson keeps only the fields the radar uses. */
void buildFilter(JsonDocument& filter);

/** Convert the feed's "ac" array into Aircraft records. Returns the count. */
size_t parseAircraft(JsonArray ac, Aircraft* out, size_t max_out,
                     bool military_only);

/** Publish a parsed list atomically, carrying trails over from the last one. */
void publish(Aircraft* list, size_t count);

}  // namespace services::adsb
