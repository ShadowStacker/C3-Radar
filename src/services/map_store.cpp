#include "services/map_store.h"

#include <Arduino.h>
#include <esp_partition.h>

#include <cmath>

#include "services/boot_guard.h"
#include "services/radar_location.h"
#include "ui/map_layer.h"

namespace services::mapstore {

namespace {

constexpr size_t kSector = 4096;

const esp_partition_t* s_part = nullptr;
const uint8_t* s_mapped = nullptr;
Entry s_entries[kMaxMaps];
size_t s_count = 0;
int s_active = -1;

size_t s_write_start = 0;
size_t s_write_pos = 0;
size_t s_erased_to = 0;
bool s_upload_ok = false;

size_t roundUp(size_t n) { return (n + kSector - 1) / kSector * kSector; }

/** First free byte (sector aligned) after the last stored map. */
size_t endOfMaps() {
  return s_count == 0 ? 0 : roundUp(s_entries[s_count - 1].offset + s_entries[s_count - 1].bytes);
}

float distanceKm(double lat, double lon) {
  const float here_lat = static_cast<float>(location::lat());
  const float dx = static_cast<float>(lon - location::lon()) * 111.0f *
                   cosf(here_lat * 0.01745329252f);
  const float dy = static_cast<float>(lat - location::lat()) * 111.0f;
  return sqrtf(dx * dx + dy * dy);
}

/** Rebuild the index by walking the partition from the start. */
void scan() {
  s_count = 0;
  size_t off = 0;
  while (s_count < kMaxMaps && off + ui::map::kHeaderSize <= s_part->size) {
    ui::map::Info info;
    if (!ui::map::validate(s_mapped + off, s_part->size - off, &info)) {
      break;
    }
    s_entries[s_count++] = Entry{static_cast<uint32_t>(off), info.bytes, info.lat, info.lon};
    off = roundUp(off + info.bytes);
  }
}

int mapNearCurrentLocation(float within_km) {
  int best = -1;
  float best_km = within_km;
  for (size_t i = 0; i < s_count; ++i) {
    const float km = distanceKm(s_entries[i].lat, s_entries[i].lon);
    if (km <= best_km) {
      best_km = km;
      best = static_cast<int>(i);
    }
  }
  return best;
}

/**
 * Close the gap left by map `index`: move every later sector down. Each
 * destination sector is read in full before it is erased, and data only moves
 * toward lower addresses, so nothing is overwritten before it has been copied.
 */
bool removeAndCompact(size_t index) {
  const size_t gap_start = s_entries[index].offset;
  const size_t gap_end = roundUp(gap_start + s_entries[index].bytes);
  const size_t data_end = endOfMaps();
  static uint8_t sector[kSector];

  size_t dst = gap_start;
  for (size_t src = gap_end; src < data_end; src += kSector, dst += kSector) {
    memcpy(sector, s_mapped + src, kSector);
    if (esp_partition_erase_range(s_part, dst, kSector) != ESP_OK ||
        esp_partition_write(s_part, dst, sector, kSector) != ESP_OK) {
      return false;
    }
  }
  // Whatever follows the moved data must not look like a map any more.
  if (dst < s_part->size) {
    esp_partition_erase_range(s_part, dst, kSector);
  }
  return true;
}

}  // namespace

void init() {
  s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (s_part == nullptr) {
    Serial.println("map: no storage partition");
    return;
  }
  const void* ptr = nullptr;
  spi_flash_mmap_handle_t handle = 0;
  if (esp_partition_mmap(s_part, 0, s_part->size, SPI_FLASH_MMAP_DATA, &ptr, &handle) !=
      ESP_OK) {
    Serial.println("map: mmap failed");
    s_part = nullptr;
    return;
  }
  s_mapped = static_cast<const uint8_t*>(ptr);
  scan();
  Serial.printf("map: %u stored, %u KB free\n", static_cast<unsigned>(s_count),
                static_cast<unsigned>(freeBytes() / 1024));
  selectForLocation();
}

size_t capacity() { return s_part != nullptr ? s_part->size : 0; }

size_t freeBytes() {
  if (s_part == nullptr || s_count >= kMaxMaps) {
    return 0;
  }
  return s_part->size - endOfMaps();
}

size_t roomForCurrentLocation() {
  if (s_part == nullptr) {
    return 0;
  }
  const int same = mapNearCurrentLocation(kSameSpotKm);
  if (same < 0) {
    return freeBytes();
  }
  return s_part->size - endOfMaps() + roundUp(s_entries[same].bytes);
}

size_t count() { return s_count; }

const Entry& entry(size_t index) { return s_entries[index]; }

int activeIndex() { return s_active; }

void selectForLocation() {
  if (s_mapped == nullptr) {
    return;
  }
  if (boot::skipMaps()) {
    // Safe mode: a stored map is the first suspect, so none is loaded.
    ui::map::detach();
    s_active = -1;
    return;
  }
  const int best = mapNearCurrentLocation(ui::map::kMaxCenterOffsetKm);
  if (best == s_active && best >= 0 &&
      ui::map::info().valid) {
    return;
  }
  s_active = best;
  if (best < 0) {
    ui::map::detach();
    return;
  }
  ui::map::attach(s_mapped + s_entries[best].offset, s_entries[best].bytes);
}

bool uploadBegin() {
  s_upload_ok = false;
  if (s_part == nullptr) {
    return false;
  }
  // Stop drawing from flash before its contents move.
  ui::map::detach();
  s_active = -1;
  const int same = mapNearCurrentLocation(kSameSpotKm);
  if (same >= 0) {
    removeAndCompact(static_cast<size_t>(same));
    scan();
  }
  if (s_count >= kMaxMaps) {
    return false;
  }
  s_write_start = endOfMaps();
  s_write_pos = s_write_start;
  s_erased_to = s_write_start;
  s_upload_ok = true;
  return true;
}

bool uploadWrite(const uint8_t* data, size_t len) {
  if (!s_upload_ok) {
    return false;
  }
  if (s_write_pos + len > s_part->size) {
    s_upload_ok = false;
    return false;
  }
  // Erase just ahead of the data, one sector at a time, so a long erase never
  // stalls the connection.
  while (s_erased_to < s_write_pos + len) {
    if (esp_partition_erase_range(s_part, s_erased_to, kSector) != ESP_OK) {
      s_upload_ok = false;
      return false;
    }
    s_erased_to += kSector;
  }
  if (esp_partition_write(s_part, s_write_pos, data, len) != ESP_OK) {
    s_upload_ok = false;
    return false;
  }
  s_write_pos += len;
  return true;
}

bool uploadEnd() {
  const bool wrote = s_upload_ok && s_mapped != nullptr;
  s_upload_ok = false;
  bool ok = wrote && ui::map::validate(s_mapped + s_write_start,
                                       s_write_pos - s_write_start, nullptr);
  if (!ok && s_part != nullptr && s_write_start < s_part->size) {
    // Leave nothing half-written behind.
    esp_partition_erase_range(s_part, s_write_start, kSector);
    Serial.println("map: upload rejected (too large or not a valid map)");
  }
  // The sector after the new map may hold stale data from an older layout.
  const size_t after = roundUp(s_write_pos);
  if (ok && after < s_part->size && after >= s_erased_to) {
    esp_partition_erase_range(s_part, after, kSector);
  }
  scan();
  selectForLocation();
  return ok;
}

bool remove(size_t index) {
  if (s_part == nullptr || index >= s_count) {
    return false;
  }
  ui::map::detach();
  s_active = -1;
  const bool ok = removeAndCompact(index);
  scan();
  selectForLocation();
  return ok;
}

void removeAll() {
  ui::map::detach();
  s_active = -1;
  if (s_part != nullptr) {
    for (size_t i = 0; i < s_count; ++i) {
      esp_partition_erase_range(s_part, s_entries[i].offset, kSector);
    }
  }
  s_count = 0;
}

}  // namespace services::mapstore
