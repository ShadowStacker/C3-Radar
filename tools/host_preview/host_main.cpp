// Renders the radar (and status screens) on a PC from a captured adsb.fi
// response, using the same drawing code as the firmware. See build.sh.
#include <Arduino.h>
#include <ArduinoJson.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_store.h"
#include "services/device_settings.h"
#include "ui/map_layer.h"
#include "services/radar_location.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"
#include "ui/theme.h"

HostSerial Serial;
static unsigned long s_now = 1000;
unsigned long millis() { return s_now; }
void hostSetMillis(unsigned long ms) { s_now = ms; }

LGFX tft;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// --- embedded-font replacement: load the .vlw files from data/ at run time
std::vector<std::string> g_font_blobs;
lgfx::PointerWrapper g_font_data[5];
lgfx::VLWfont g_fonts[5];

void put16(std::ofstream& o, uint16_t v) { o.put(v & 0xFF).put(v >> 8); }
void put32(std::ofstream& o, uint32_t v) { put16(o, v & 0xFFFF); put16(o, v >> 16); }

/** Same RGB565 BI_BITFIELDS BMP the firmware serves at /screen.bmp. */
void writeBmpHeader(std::ofstream& o, int w, int h) {
  const uint32_t image = static_cast<uint32_t>(w) * h * 2;
  o.put('B').put('M');
  put32(o, 66 + image); put32(o, 0); put32(o, 66);
  put32(o, 40); put32(o, w); put32(o, static_cast<uint32_t>(-h));
  put16(o, 1); put16(o, 16); put32(o, 3); put32(o, image);
  put32(o, 2835); put32(o, 2835); put32(o, 0); put32(o, 0);
  put32(o, 0xF800); put32(o, 0x07E0); put32(o, 0x001F);
}

void savePanel(const std::string& path) {
  std::ofstream o(path, std::ios::binary);
  writeBmpHeader(o, tft.width(), tft.height());
  for (int y = 0; y < tft.height(); ++y) {
    for (int x = 0; x < tft.width(); ++x) {
      const uint32_t c = tft.readPixel(x, y);  // RGB565
      put16(o, static_cast<uint16_t>(c));
    }
  }
}

bool streamSink(const uint8_t* rows, size_t bytes, void* ctx) {
  static_cast<std::ofstream*>(ctx)->write(reinterpret_cast<const char*>(rows),
                                          static_cast<std::streamsize>(bytes));
  return true;
}

bool feed(const std::string& json_path, bool military_only) {
  const std::string body = slurp(json_path);
  JsonDocument filter;
  services::adsb::buildFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    std::printf("bad json: %s\n", json_path.c_str());
    return false;
  }
  static services::adsb::Aircraft parsed[services::adsb::kMaxAircraft];
  const size_t n = services::adsb::parseAircraft(
      doc["ac"].as<JsonArray>(), parsed, services::adsb::kMaxAircraft, military_only);
  services::adsb::publish(parsed, n);
  std::printf("%s: %u aircraft%s\n", json_path.c_str(), static_cast<unsigned>(n),
              military_only ? " (military only)" : "");
  return true;
}

}  // namespace

// --- stand-ins for the device identity (normally derived from the chip's MAC)
namespace services::device {
uint8_t brightness() { return 255; }
const char* name() { return "plane-radar-a1b2"; }
const char* hostname() { return "plane-radar-a1b2"; }
const char* hostUrl() { return "plane-radar-a1b2.local"; }
const char* setupApName() { return "PlaneRadar-A1B2"; }
}  // namespace services::device

bool displayFontInit() { return true; }
const lgfx::IFont* uiFont(UiFont font) { return &g_fonts[static_cast<size_t>(font)]; }

// usage: host_preview <data_dir> <out_dir> <lat> <lon> <range_idx> <mil 0|1> <snapshot.json>...
int main(int argc, char** argv) {
  if (argc < 8) {
    std::printf("usage: %s data_dir out_dir lat lon range_idx mil json...\n", argv[0]);
    return 2;
  }
  const std::string data_dir = argv[1];
  const std::string out_dir = argv[2];
  const bool mil = std::atoi(argv[6]) != 0;

  const int sizes[5] = {14, 16, 20, 22, 26};
  for (int i = 0; i < 5; ++i) {
    g_font_blobs.push_back(slurp(data_dir + "/ui_font_" + std::to_string(sizes[i]) + ".vlw"));
    g_font_data[i].set(reinterpret_cast<const uint8_t*>(g_font_blobs[i].data()),
                       g_font_blobs[i].size());
    if (!g_fonts[i].loadFont(&g_font_data[i])) {
      std::printf("font %d failed\n", sizes[i]);
      return 1;
    }
  }

  // C3_THEME=<preset number> picks a colour theme.
  ui::theme::init();
  if (const char* theme = std::getenv("C3_THEME")) {
    ui::theme::select(static_cast<uint8_t>(std::atoi(theme)));
  }
  tft.init();
  tft.setTextWrap(false);

  statusScreenPortal();
  savePanel(out_dir + "/status_portal.bmp");
  statusScreenConnectingBegin("MyHomeNetwork");
  savePanel(out_dir + "/status_connecting.bmp");
  statusScreenConnected("MyHomeNetwork", "192.168.1.57", 8080);
  savePanel(out_dir + "/status_connected.bmp");
  statusScreenConnectFailed();
  savePanel(out_dir + "/status_failed.bmp");

  services::location::save(std::atof(argv[3]), std::atof(argv[4]));
  ui::radar::rangeInit();
  ui::radar::rangeSet(static_cast<uint8_t>(std::atoi(argv[5])));
  ui::radar::setMilitaryOnly(mil);
  // Optional map: C3_MAP=<file made by the web page's map builder>,
  // C3_MAP_LAYERS=<toggle bit mask>.
  static std::string map_blob;
  if (const char* map_path = std::getenv("C3_MAP")) {
    map_blob = slurp(map_path);
    const bool ok = ui::map::attach(reinterpret_cast<const uint8_t*>(map_blob.data()),
                                    map_blob.size());
    std::printf("map %s: %s (%u bytes)\n", map_path, ok ? "ok" : "REJECTED",
                static_cast<unsigned>(map_blob.size()));
  }
  if (const char* show = std::getenv("C3_AC_SHOW")) {
    ui::radar::setAircraftShow(static_cast<uint8_t>(std::atoi(show)));
  }
  if (const char* layers = std::getenv("C3_MAP_LAYERS")) {
    ui::radar::setMapLayers(static_cast<uint8_t>(std::atoi(layers)));
  }
  services::adsb::init();
  ui::radarDisplayInit();

  // Successive snapshots (5 s apart) so trails build up like on the device.
  for (int i = 7; i < argc; ++i) {
    s_now += 5000;
    if (!feed(argv[i], mil)) {
      return 1;
    }
  }
  s_now += 1200;
  if (const char* extra = std::getenv("C3_ADD_MS")) {
    s_now += static_cast<unsigned long>(std::atol(extra));  // e.g. to catch a blink phase
  }
  ui::radarDisplayDraw();
  savePanel(out_dir + "/radar.bmp");

  std::ofstream stream(out_dir + "/radar_stream.bmp", std::ios::binary);
  writeBmpHeader(stream, 360, 360);
  ui::radarDisplayStreamFrame(streamSink, &stream);
  std::printf("in range %u, beyond %u\n",
              static_cast<unsigned>(ui::radarDisplayInRangeCount()),
              static_cast<unsigned>(ui::radarDisplayBeyondCount()));
  return 0;
}
