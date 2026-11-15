#include "services/web_ui.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include <cstdlib>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/device_settings.h"
#include "services/radar_location.h"
#include "services/web_page.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace services::web {

namespace {

WebServer* s_server = nullptr;
bool s_port_changed = false;

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
  doc["brightness"] = device::brightness();
  doc["rotation"] = device::rotation();
  doc["invert"] = device::invertColors();
  doc["swap_rb"] = device::swapRedBlue();
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
  if (srv.hasArg("swap_rb")) {
    device::setSwapRedBlue(argBool("swap_rb"));
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
    s_port_changed = true;  // the server restarts after this reply is sent
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

}  // namespace

void registerRoutes(WebServer& server) {
  s_server = &server;
  server.on("/", HTTP_GET, handlePage);
  server.on("/radar", HTTP_GET, handlePage);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/screen.bmp", HTTP_GET, handleScreen);
}

bool consumePortChange() {
  const bool changed = s_port_changed;
  s_port_changed = false;
  return changed;
}

}  // namespace services::web
