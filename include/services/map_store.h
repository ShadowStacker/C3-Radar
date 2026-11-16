#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Flash storage for the vector map (ui::map). The blob lives in the otherwise
 * unused data partition and is memory-mapped, so it costs no RAM. It survives
 * firmware updates.
 */
namespace services::mapstore {

/** Map the partition and attach the stored map if it is valid. Call once at boot. */
void init();

/** Bytes available for a map. */
size_t capacity();

/** Streamed upload: begin, any number of writes in order, then end. */
bool uploadBegin();
bool uploadWrite(const uint8_t* data, size_t len);
/** Validates what was written and starts using it. False if it is not a map. */
bool uploadEnd();

/** Remove the stored map. */
void clear();

}  // namespace services::mapstore
