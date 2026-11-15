#include "services/device_settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"

namespace services::device {

namespace {

constexpr char kNamespace[] = "device";
constexpr char kKeyPort[] = "httpPort";
constexpr char kKeyBright[] = "bright";
constexpr char kKeyRotation[] = "rot";
constexpr char kKeyInvert[] = "invert";
constexpr char kKeySwapRb[] = "swapRB";
constexpr uint8_t kMinBrightness = 5;

uint16_t s_port = config::kDefaultHttpPort;
uint8_t s_brightness = 255;
uint8_t s_rotation = 0;
bool s_invert = config::kDisplayInvert;
bool s_swap_rb = config::kDisplayRgbOrder;

template <typename Fn>
void withPrefs(Fn fn) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return;
  }
  fn(prefs);
  prefs.end();
}

}  // namespace

void init() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return;
  }
  const uint16_t port = prefs.getUShort(kKeyPort, config::kDefaultHttpPort);
  s_port = port != 0 ? port : config::kDefaultHttpPort;
  const uint8_t bright = prefs.getUChar(kKeyBright, 255);
  s_brightness = bright < kMinBrightness ? kMinBrightness : bright;
  s_rotation = prefs.getUChar(kKeyRotation, 0) & 3;
  s_invert = prefs.getBool(kKeyInvert, config::kDisplayInvert);
  s_swap_rb = prefs.getBool(kKeySwapRb, config::kDisplayRgbOrder);
  prefs.end();
}

uint16_t httpPort() { return s_port; }

bool setHttpPort(long port) {
  if (port < 1 || port > 65535) {
    return false;
  }
  if (static_cast<uint16_t>(port) != s_port) {
    s_port = static_cast<uint16_t>(port);
    withPrefs([](Preferences& p) { p.putUShort(kKeyPort, s_port); });
    Serial.printf("Web server port: %u\n", static_cast<unsigned>(s_port));
  }
  return true;
}

uint8_t brightness() { return s_brightness; }

void setBrightness(long level) {
  if (level < kMinBrightness) level = kMinBrightness;
  if (level > 255) level = 255;
  if (static_cast<uint8_t>(level) == s_brightness) {
    return;
  }
  s_brightness = static_cast<uint8_t>(level);
  withPrefs([](Preferences& p) { p.putUChar(kKeyBright, s_brightness); });
}

uint8_t rotation() { return s_rotation; }

void setRotation(long quarter_turns) {
  const uint8_t r = static_cast<uint8_t>(quarter_turns) & 3;
  if (r == s_rotation) {
    return;
  }
  s_rotation = r;
  withPrefs([](Preferences& p) { p.putUChar(kKeyRotation, s_rotation); });
}

bool invertColors() { return s_invert; }

void setInvertColors(bool on) {
  if (on == s_invert) {
    return;
  }
  s_invert = on;
  withPrefs([](Preferences& p) { p.putBool(kKeyInvert, s_invert); });
}

bool swapRedBlue() { return s_swap_rb; }

void setSwapRedBlue(bool on) {
  if (on == s_swap_rb) {
    return;
  }
  s_swap_rb = on;
  withPrefs([](Preferences& p) { p.putBool(kKeySwapRb, s_swap_rb); });
}

void reset() {
  s_port = config::kDefaultHttpPort;
  s_brightness = 255;
  s_rotation = 0;
  s_invert = config::kDisplayInvert;
  s_swap_rb = config::kDisplayRgbOrder;
  withPrefs([](Preferences& p) { p.clear(); });
}

}  // namespace services::device
