#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Local vector map (coastline, water, borders, roads, place names) drawn
 * under the radar. The data is a compact blob prepared by the web page from
 * OpenStreetMap and stored in flash; it is read in place, never copied to RAM.
 *
 * Blob layout (little-endian), coordinates in 2 m units east / north of the
 * map centre:
 *   header (32 bytes): "C3MP", u8 version, u8 0, u16 0, u32 total_size,
 *                      u32 crc32(of everything after the header),
 *                      i32 lat_e7, i32 lon_e7, u32 line_bytes, u32 0
 *   lines:  { u8 layer, u8 0, u16 n, i16 min_x, min_y, max_x, max_y,
 *             n * { i16 x, i16 y } } ...
 *   labels: { u8 layer, u8 rank, i16 x, i16 y, u8 len, char text[len] } ...
 */
namespace ui::map {

enum Layer : uint8_t {
  kLayerCoast = 0,
  kLayerLake = 1,
  kLayerRiver = 2,
  kLayerBorder = 3,
  kLayerTrunk = 4,
  kLayerMotorway = 5,
  kLayerRunway = 6,
  kLayerTown = 7,      // label
  kLayerAirfield = 8,  // label
  kLayerCount
};

/** User toggles (bit mask, stored in NVS). */
enum Toggle : uint8_t {
  kShowWater = 1 << 0,      // coastline + lakes
  kShowRivers = 1 << 1,     // rivers and canals
  kShowBorders = 1 << 2,    // country / state borders
  kShowMotorways = 1 << 3,
  kShowMainRoads = 1 << 4,  // trunk roads
  kShowTowns = 1 << 5,      // place names
  kShowAirfields = 1 << 6,  // all runways + airfield codes from the map
  kToggleDefault = kShowWater | kShowRivers | kShowBorders | kShowMotorways |
                   kShowTowns | kShowAirfields,
  kToggleAll = 0x7F,
};

constexpr size_t kHeaderSize = 32;
constexpr uint8_t kVersion = 1;
constexpr float kMetersPerUnit = 2.0f;
/** A stored map further than this from the radar centre is not drawn. */
constexpr float kMaxCenterOffsetKm = 30.0f;

struct Info {
  bool valid = false;
  double lat = 0.0;
  double lon = 0.0;
  uint32_t bytes = 0;
};

uint32_t crc32(const uint8_t* data, size_t len);

/** Validate `blob` (magic, size, checksum) and start using it. */
bool attach(const uint8_t* blob, size_t max_len);
void detach();
const Info& info();
/** Changes whenever a map is attached or detached. */
uint32_t generation();
/** Distance from the stored map's centre to the radar centre, km. */
float centerOffsetKm();

/**
 * Work out which line records are on screen for the current location, range
 * and toggles. Call when any of those change - not per frame.
 */
void prepare(uint8_t toggles);

/** Draw the prepared lines into the strip currently being rendered. */
void drawLines();

struct LabelCandidate {
  int16_t x, y;
  uint8_t rank;      // towns: 3 city, 2 town, 1 village
  bool airfield;
  char text[20];
};

/**
 * Labels inside the scope for the last prepare(), most important first.
 * Returns how many were written to `out`.
 */
size_t collectLabels(LabelCandidate* out, size_t max_out);

}  // namespace ui::map
