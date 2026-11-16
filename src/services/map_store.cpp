#include "services/map_store.h"

#include <Arduino.h>
#include <esp_partition.h>

#include "ui/map_layer.h"

namespace services::mapstore {

namespace {

constexpr size_t kSectorSize = 4096;

const esp_partition_t* s_part = nullptr;
const uint8_t* s_mapped = nullptr;
size_t s_write_pos = 0;
size_t s_erased_to = 0;
bool s_upload_ok = false;

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
  if (ui::map::attach(s_mapped, s_part->size)) {
    Serial.printf("map: %u bytes for %.4f, %.4f\n",
                  static_cast<unsigned>(ui::map::info().bytes), ui::map::info().lat,
                  ui::map::info().lon);
  } else {
    Serial.println("map: none stored");
  }
}

size_t capacity() { return s_part != nullptr ? s_part->size : 0; }

bool uploadBegin() {
  // Stop drawing from flash before its contents change.
  ui::map::detach();
  s_write_pos = 0;
  s_erased_to = 0;
  s_upload_ok = s_part != nullptr;
  return s_upload_ok;
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
    if (esp_partition_erase_range(s_part, s_erased_to, kSectorSize) != ESP_OK) {
      s_upload_ok = false;
      return false;
    }
    s_erased_to += kSectorSize;
  }
  if (esp_partition_write(s_part, s_write_pos, data, len) != ESP_OK) {
    s_upload_ok = false;
    return false;
  }
  s_write_pos += len;
  return true;
}

bool uploadEnd() {
  if (!s_upload_ok || s_mapped == nullptr) {
    return false;
  }
  s_upload_ok = false;
  if (!ui::map::attach(s_mapped, s_write_pos)) {
    Serial.println("map: upload rejected (not a valid map)");
    // Leave nothing half-written behind.
    esp_partition_erase_range(s_part, 0, kSectorSize);
    return false;
  }
  Serial.printf("map: stored %u bytes\n", static_cast<unsigned>(s_write_pos));
  return true;
}

void clear() {
  ui::map::detach();
  if (s_part != nullptr) {
    esp_partition_erase_range(s_part, 0, kSectorSize);
  }
}

}  // namespace services::mapstore
