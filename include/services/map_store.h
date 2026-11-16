#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Flash storage for vector maps (ui::map). Several maps - one per saved
 * location - are kept back to back in the data partition, each starting on a
 * 4 KB sector. The partition is memory-mapped, so maps cost no RAM, and it
 * survives firmware updates.
 */
namespace services::mapstore {

constexpr size_t kMaxMaps = 8;
/** A map this close to a location counts as "the map for that location". */
constexpr float kSameSpotKm = 5.0f;

struct Entry {
  uint32_t offset;
  uint32_t bytes;
  double lat;
  double lon;
};

/** Map the partition, index the stored maps and pick one for the current location. */
void init();

size_t capacity();
/** Bytes still free for another map. */
size_t freeBytes();
/** Bytes a new map for the current location may use (free space plus the map it replaces). */
size_t roomForCurrentLocation();

size_t count();
const Entry& entry(size_t index);
/** Index of the map being drawn, or -1. */
int activeIndex();

/**
 * Use the stored map nearest the current radar location (if one is close
 * enough to draw). Call after the location changes.
 */
void selectForLocation();

/** Streamed upload of a map for the current location; it replaces the old one. */
bool uploadBegin();
bool uploadWrite(const uint8_t* data, size_t len);
/** Validates what was written and starts using it. False if it is not a map. */
bool uploadEnd();

/** Remove one stored map. */
bool remove(size_t index);

}  // namespace services::mapstore
