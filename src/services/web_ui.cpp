#include "services/web_ui.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WiFi.h>

#include <cmath>
#include <cstdlib>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/device_settings.h"
#include "services/map_store.h"
#include "services/radar_location.h"
#include "services/web_page.h"
#include "ui/map_layer.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/theme.h"

namespace services::web {

namespace {

WebServer* s_server = nullptr;
bool s_restart_needed = false;
bool s_map_upload_ok = false;
bool s_update_ok = false;
bool s_update_running = false;
bool s_reboot_pending = false;
const char* s_update_error = "";

constexpr uint32_t kBmpHeaderSize = 14 + 40 + 12;
constexpr uint32_t kBmpImageSize =
    static_cast<uint32_t>(ui::radar::kSize) * ui::radar::kSize * 2;

void noCache() {
  s_server->sendHeader("Cache-Control", "no-store");
}

void handlePage() {
  s_server->send_P(200, "text/html", kWebPage);
}

void handleState() {
  JsonDocument doc;
  doc["version"] = config::kFirmwareVersion;
  doc["ip"] = WiFi.localIP().toString();
  doc["name"] = device::name();
  doc["host"] = device::hostUrl();
  doc["port"] = device::httpPort();
  doc["lat"] = serialized(String(location::lat(), 6));
  doc["lon"] = serialized(String(location::lon(), 6));
  doc["range"] = ui::radar::rangeIndex();
  JsonArray ranges = doc["ranges"].to<JsonArray>();
  for (size_t i = 0; i < ui::radar::kRangePresetCount; ++i) {
    char label[12];
    ui::radar::formatRing3Label(label, sizeof(label),
                                ui::radar::kRangePresets[i].ring3_km,
                                ui::radar::useMiles());
    ranges.add(label);
  }
  doc["miles"] = ui::radar::useMiles();
  doc["runways"] = ui::radar::showRunways();
  doc["mil"] = ui::radar::militaryOnly();
  doc["ac_show"] = ui::radar::aircraftShow();
  JsonArray key = doc["alt_key"].to<JsonArray>();
  for (size_t i = 0; i < ui::theme::kAltitudeStopCount; ++i) {
    const ui::theme::AltitudeStop& stop = ui::theme::kAltitudeStops[i];
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", stop.r, stop.g, stop.b);
    JsonArray entry = key.add<JsonArray>();
    entry.add(stop.alt_ft);
    entry.add(hex);
  }
  doc["map_layers"] = ui::radar::mapLayers();
  JsonObject map = doc["map"].to<JsonObject>();
  const ui::map::Info& info = ui::map::info();
  map["present"] = info.valid;
  map["capacity"] = mapstore::capacity();
  map["free"] = mapstore::freeBytes();
  map["room"] = mapstore::roomForCurrentLocation();
  map["count"] = mapstore::count();
  if (info.valid) {
    map["lat"] = serialized(String(info.lat, 4));
    map["lon"] = serialized(String(info.lon, 4));
    map["bytes"] = info.bytes;
    map["offset_km"] = serialized(String(ui::map::centerOffsetKm(), 1));
  }
  doc["outer_km"] = serialized(String(ui::radar::rangeCurrent().outer_km, 3));
  doc["ring_px"] = ui::radar::kGridOuterRadius;
  JsonArray places = doc["places"].to<JsonArray>();
  const int active_place = location::activePlace();
  for (size_t i = 0; i < location::placeCount(); ++i) {
    const location::Place& pl = location::place(i);
    JsonObject o = places.add<JsonObject>();
    o["name"] = pl.name;
    o["lat"] = serialized(String(pl.lat, 5));
    o["lon"] = serialized(String(pl.lon, 5));
    o["active"] = static_cast<int>(i) == active_place;
    bool has_map = false;
    for (size_t m = 0; m < mapstore::count() && !has_map; ++m) {
      const mapstore::Entry& e = mapstore::entry(m);
      has_map = fabs(e.lat - pl.lat) < 0.05 && fabs(e.lon - pl.lon) < 0.07;
    }
    o["map"] = has_map;
  }
  doc["theme"] = ui::theme::current();
  JsonArray themes = doc["themes"].to<JsonArray>();
  const auto addTheme = [&themes](const ui::theme::ThemeSpec& spec, uint8_t id) {
    JsonObject t = themes.add<JsonObject>();
    t["id"] = id;
    t["name"] = spec.name;
    t["alt"] = spec.altitude_colors;
    JsonArray c = t["colors"].to<JsonArray>();
    for (size_t i = 0; i < ui::theme::kSlotCount; ++i) {
      char hex[8];
      snprintf(hex, sizeof(hex), "#%06x", static_cast<unsigned>(spec.rgb[i]));
      c.add(hex);
    }
  };
  for (size_t i = 0; i < ui::theme::kPresetCount; ++i) {
    addTheme(ui::theme::kPresets[i], static_cast<uint8_t>(i));
  }
  addTheme(ui::theme::custom(), ui::theme::kCustomId);
  doc["brightness"] = device::brightness();
  doc["rotation"] = device::rotation();
  doc["invert"] = device::invertColors();
  doc["aircraft"] = ui::radarDisplayInRangeCount();
  doc["beyond"] = ui::radarDisplayBeyondCount();
  const unsigned long last = adsb::lastUpdateMs();
  doc["data_age_s"] = last == 0 ? -1 : static_cast<long>((millis() - last) / 1000UL);
  doc["heap"] = ESP.getFreeHeap();

  String out;
  serializeJson(doc, out);
  noCache();
  s_server->send(200, "application/json", out);
}

bool argBool(const char* name) {
  const String v = s_server->arg(name);
  return v == "1" || v == "true" || v == "on";
}

void sendResult(bool ok, const char* error) {
  JsonDocument doc;
  doc["ok"] = ok;
  if (!ok) {
    doc["error"] = error;
  }
  String out;
  serializeJson(doc, out);
  noCache();
  s_server->send(ok ? 200 : 400, "application/json", out);
}

bool parseDouble(const String& text, double* out) {
  if (text.length() == 0) {
    return false;
  }
  char* end = nullptr;
  *out = strtod(text.c_str(), &end);
  return end != text.c_str() && *end == '\0';
}

/** Accepts any subset of the settings as form fields; applies them at once. */
void handleSettings() {
  WebServer& srv = *s_server;
  bool refetch = false;
  bool display_changed = false;

  if (srv.hasArg("lat") || srv.hasArg("lon")) {
    double lat = 0.0;
    double lon = 0.0;
    if (!parseDouble(srv.arg("lat"), &lat) || !parseDouble(srv.arg("lon"), &lon) ||
        !location::save(lat, lon)) {
      sendResult(false, "Latitude must be -90..90 and longitude -180..180");
      return;
    }
    refetch = true;
  }
  if (srv.hasArg("place_go")) {
    if (!location::selectPlace(static_cast<size_t>(srv.arg("place_go").toInt()))) {
      sendResult(false, "Unknown location");
      return;
    }
    refetch = true;
  }
  if (srv.hasArg("place_add")) {
    // Saves the radar's current centre (after any lat/lon in this request).
    if (!location::addPlace(srv.arg("place_add").c_str(), location::lat(), location::lon())) {
      sendResult(false, location::placeCount() >= location::kMaxPlaces
                            ? "The list is full (8 locations). Remove one first"
                            : "Give the location a name");
      return;
    }
  }
  if (srv.hasArg("place_del")) {
    location::removePlace(static_cast<size_t>(srv.arg("place_del").toInt()));
  }
  if (refetch) {
    mapstore::selectForLocation();
  }
  if (srv.hasArg("range")) {
    const long idx = srv.arg("range").toInt();
    if (idx < 0 || !ui::radar::rangeSet(static_cast<uint8_t>(idx))) {
      sendResult(false, "Unknown range");
      return;
    }
    refetch = true;
  }
  long new_port = 0;
  if (srv.hasArg("port")) {
    new_port = srv.arg("port").toInt();
    if (new_port < 1 || new_port > 65535) {
      sendResult(false, "Port must be between 1 and 65535");
      return;
    }
  }
  if (srv.hasArg("mil")) {
    ui::radar::setMilitaryOnly(argBool("mil"));
    refetch = true;
  }
  if (srv.hasArg("custom")) {
    // Comma-separated RRGGBB list, one per theme slot.
    uint32_t rgb[ui::theme::kSlotCount];
    const String list = srv.arg("custom");
    size_t count = 0;
    int start = 0;
    while (count < ui::theme::kSlotCount && start <= static_cast<int>(list.length())) {
      int comma = list.indexOf(',', start);
      if (comma < 0) {
        comma = list.length();
      }
      const String item = list.substring(start, comma);
      char* end = nullptr;
      const unsigned long v = strtoul(item.c_str(), &end, 16);
      if (item.length() != 6 || end == nullptr || *end != '\0') {
        break;
      }
      rgb[count++] = static_cast<uint32_t>(v);
      start = comma + 1;
    }
    if (count != ui::theme::kSlotCount) {
      sendResult(false, "Colours must be 6-digit hex values");
      return;
    }
    ui::theme::setCustom(rgb, argBool("custom_alt"));
  }
  if (srv.hasArg("theme")) {
    if (!ui::theme::select(static_cast<uint8_t>(srv.arg("theme").toInt()))) {
      sendResult(false, "Unknown theme");
      return;
    }
  }
  if (srv.hasArg("ac_show")) {
    ui::radar::setAircraftShow(static_cast<uint8_t>(srv.arg("ac_show").toInt()));
  }
  if (srv.hasArg("map_layers")) {
    ui::radar::setMapLayers(static_cast<uint8_t>(srv.arg("map_layers").toInt()));
  }
  if (srv.hasArg("name")) {
    const String before = device::hostname();
    device::setName(srv.arg("name").c_str());
    if (before != device::hostname()) {
      s_restart_needed = true;  // re-announce under the new network name
    }
  }
  if (srv.hasArg("miles")) {
    ui::radar::setUseMiles(argBool("miles"));
  }
  if (srv.hasArg("runways")) {
    ui::radar::setShowRunways(argBool("runways"));
  }
  if (srv.hasArg("brightness")) {
    device::setBrightness(srv.arg("brightness").toInt());
    display_changed = true;
  }
  if (srv.hasArg("rotation")) {
    device::setRotation(srv.arg("rotation").toInt());
    display_changed = true;
  }
  if (srv.hasArg("invert")) {
    device::setInvertColors(argBool("invert"));
    display_changed = true;
  }

  if (display_changed) {
    displayApplySettings();
  }
  if (refetch) {
    adsb::requestRefresh();
  }
  // Redraw right away so the next screen copy already shows the change.
  if (ui::radarDisplayStreamFrame(nullptr, nullptr)) {
    ui::radarDisplayRefreshAircraft();
  }

  if (new_port != 0 && new_port != device::httpPort()) {
    device::setHttpPort(new_port);
    s_restart_needed = true;  // the server restarts after this reply is sent
  }
  sendResult(true, nullptr);
}

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, uint32_t v) {
  put16(p, static_cast<uint16_t>(v));
  put16(p + 2, static_cast<uint16_t>(v >> 16));
}

bool sendRows(const uint8_t* rows, size_t bytes, void* ctx) {
  WebServer* srv = static_cast<WebServer*>(ctx);
  if (!srv->client().connected()) {
    return false;
  }
  srv->sendContent(reinterpret_cast<const char*>(rows), bytes);
  return true;
}

/**
 * The frame on the panel as a 16-bit (RGB565, top-down) BMP. The frame is
 * re-rendered strip by strip straight into the socket, so mirroring the
 * display costs no extra RAM.
 */
void handleScreen() {
  if (!ui::radarDisplayStreamFrame(nullptr, nullptr)) {
    noCache();
    s_server->send(503, "text/plain", "radar not on screen");
    return;
  }

  uint8_t h[kBmpHeaderSize] = {};
  h[0] = 'B';
  h[1] = 'M';
  put32(h + 2, kBmpHeaderSize + kBmpImageSize);
  put32(h + 10, kBmpHeaderSize);
  put32(h + 14, 40);                                             // BITMAPINFOHEADER
  put32(h + 18, static_cast<uint32_t>(ui::radar::kSize));        // width
  put32(h + 22, static_cast<uint32_t>(-ui::radar::kSize));       // top-down
  put16(h + 26, 1);                                              // planes
  put16(h + 28, 16);                                             // bits per pixel
  put32(h + 30, 3);                                              // BI_BITFIELDS
  put32(h + 34, kBmpImageSize);
  put32(h + 38, 2835);
  put32(h + 42, 2835);
  put32(h + 54, 0xF800);                                         // red mask
  put32(h + 58, 0x07E0);                                         // green mask
  put32(h + 62, 0x001F);                                         // blue mask

  noCache();
  s_server->setContentLength(kBmpHeaderSize + kBmpImageSize);
  s_server->send(200, "image/bmp", "");
  s_server->sendContent(reinterpret_cast<const char*>(h), sizeof(h));
  ui::radarDisplayStreamFrame(sendRows, s_server);
}

/** Multipart upload of a map blob: streamed straight into flash. */
void handleMapUploadChunk() {
  HTTPUpload& up = s_server->upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      s_map_upload_ok = mapstore::uploadBegin();
      break;
    case UPLOAD_FILE_WRITE:
      if (s_map_upload_ok) {
        s_map_upload_ok = mapstore::uploadWrite(up.buf, up.currentSize);
      }
      break;
    case UPLOAD_FILE_END:
      s_map_upload_ok = s_map_upload_ok && mapstore::uploadEnd();
      break;
    default:  // aborted
      s_map_upload_ok = false;
      mapstore::uploadEnd();  // discards the partial map
      break;
  }
}

void handleMapUploadDone() {
  const bool ok = s_map_upload_ok;
  s_map_upload_ok = false;
  sendResult(ok, "The device did not accept the map (too large or damaged)");
}

/**
 * Firmware update: the app image is streamed into the spare firmware slot.
 * Settings, Wi-Fi and the map live elsewhere in flash and are not touched.
 */
void handleUpdateChunk() {
  HTTPUpload& up = s_server->upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      s_update_ok = false;
      s_update_running = true;
      s_update_error = "";
      break;
    case UPLOAD_FILE_WRITE:
      if (up.totalSize == 0) {
        // First block: make sure this is an app image and not the full
        // (bootloader + app) image meant for USB flashing, which would brick
        // the slot. App images carry a descriptor magic at offset 0x20.
        const bool app_image =
            up.currentSize >= 0x24 && up.buf[0] == 0xE9 && up.buf[0x20] == 0x32 &&
            up.buf[0x21] == 0x54 && up.buf[0x22] == 0xCD && up.buf[0x23] == 0xAB;
        if (!app_image) {
          s_update_error = "That is not an update file. Use the file ending in -update.bin";
        } else if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          s_update_error = "No space for the update";
        } else {
          s_update_ok = true;
        }
      }
      if (s_update_ok && Update.write(up.buf, up.currentSize) != up.currentSize) {
        s_update_ok = false;
        s_update_error = "Writing the update failed";
        Update.abort();
      }
      break;
    case UPLOAD_FILE_END:
      if (s_update_ok && !Update.end(true)) {
        s_update_ok = false;
        s_update_error = "The update file is damaged";
      }
      s_update_running = false;
      break;
    default:  // aborted
      if (s_update_ok) {
        Update.abort();
      }
      s_update_ok = false;
      s_update_running = false;
      s_update_error = "Upload interrupted";
      break;
  }
}

void handleUpdateDone() {
  const bool ok = s_update_ok;
  s_update_ok = false;
  s_update_running = false;
  sendResult(ok, s_update_error[0] != '\0' ? s_update_error : "Update failed");
  if (ok) {
    s_reboot_pending = true;  // restart once this reply has gone out
  }
}

/** Removes the map in use (or map number `i` if given). */
void handleMapDelete() {
  const int index =
      s_server->hasArg("i") ? static_cast<int>(s_server->arg("i").toInt()) : mapstore::activeIndex();
  const bool ok = index >= 0 && mapstore::remove(static_cast<size_t>(index));
  sendResult(ok, "No map to remove");
}

}  // namespace

void registerRoutes(WebServer& server) {
  s_server = &server;
  server.on("/", HTTP_GET, handlePage);
  server.on("/radar", HTTP_GET, handlePage);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/screen.bmp", HTTP_GET, handleScreen);
  server.on("/api/map", HTTP_POST, handleMapUploadDone, handleMapUploadChunk);
  server.on("/api/map/delete", HTTP_POST, handleMapDelete);
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateChunk);
}

bool updateInProgress() { return s_update_running; }

bool rebootPending() { return s_reboot_pending; }

bool consumeRestartRequest() {
  const bool needed = s_restart_needed;
  s_restart_needed = false;
  return needed;
}

}  // namespace services::web
