#include "services/device_settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "config.h"

namespace services::device {

namespace {

constexpr char kNamespace[] = "device";
constexpr char kKeyPort[] = "httpPort";
constexpr char kKeyName[] = "name";
constexpr char kKeyBright[] = "bright";
constexpr char kKeyRotation[] = "rot";
constexpr char kKeyInvert[] = "invert";
constexpr uint8_t kMinBrightness = 5;

char s_name[kMaxNameLen + 1] = "";
char s_hostname[40] = "";
char s_host_url[48] = "";
char s_ap_name[24] = "";
char s_chip_tag[5] = "0000";

/** Keep printable ASCII (the display fonts cover nothing else), trim spaces. */
void sanitizeName(const char* text, char* out, size_t out_len) {
  size_t n = 0;
  for (const char* p = text; p != nullptr && *p != '\0' && n + 1 < out_len; ++p) {
    const char c = *p;
    if (c < 0x20 || c > 0x7E || c == '<' || c == '>' || c == '"' || c == '&') {
      continue;
    }
    if (c == ' ' && n == 0) {
      continue;
    }
    out[n++] = c;
  }
  while (n > 0 && out[n - 1] == ' ') {
    --n;
  }
  out[n] = '\0';
}

/** Rebuild the hostname from the current name. */
void deriveHostname() {
  size_t n = 0;
  bool dash = false;
  for (const char* p = s_name; *p != '\0' && n + 1 < 32; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (keep) {
      if (dash && n > 0) {
        s_hostname[n++] = '-';
      }
      dash = false;
      s_hostname[n++] = c;
    } else {
      dash = true;
    }
  }
  s_hostname[n] = '\0';
  if (n == 0) {
    snprintf(s_hostname, sizeof(s_hostname), "%s-%s", config::kPortalHostname, s_chip_tag);
  }
  snprintf(s_host_url, sizeof(s_host_url), "%s.local", s_hostname);
}

void applyName(const char* text) {
  sanitizeName(text, s_name, sizeof(s_name));
  if (s_name[0] == '\0') {
    snprintf(s_name, sizeof(s_name), "%s-%s", config::kPortalHostname, s_chip_tag);
  }
  deriveHostname();
}

uint16_t s_port = config::kDefaultHttpPort;
uint8_t s_brightness = 255;
uint8_t s_rotation = 0;
bool s_invert = config::kDisplayInvert;

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
  // Last two bytes of the factory MAC make names unique per chip.
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(s_chip_tag, sizeof(s_chip_tag), "%02x%02x",
           static_cast<unsigned>((mac >> 32) & 0xFF),
           static_cast<unsigned>((mac >> 40) & 0xFF));
  snprintf(s_ap_name, sizeof(s_ap_name), "PlaneRadar-%c%c%c%c", toupper(s_chip_tag[0]),
           toupper(s_chip_tag[1]), toupper(s_chip_tag[2]), toupper(s_chip_tag[3]));
  applyName("");

  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return;
  }
  char stored[kMaxNameLen + 1] = "";
  prefs.getString(kKeyName, stored, sizeof(stored));
  applyName(stored);
  const uint16_t port = prefs.getUShort(kKeyPort, config::kDefaultHttpPort);
  s_port = port != 0 ? port : config::kDefaultHttpPort;
  const uint8_t bright = prefs.getUChar(kKeyBright, 255);
  s_brightness = bright < kMinBrightness ? kMinBrightness : bright;
  s_rotation = prefs.getUChar(kKeyRotation, 0) & 3;
  s_invert = prefs.getBool(kKeyInvert, config::kDisplayInvert);
  prefs.end();
}

const char* name() { return s_name; }

void setName(const char* text) {
  char previous[kMaxNameLen + 1];
  strncpy(previous, s_name, sizeof(previous));
  applyName(text);
  if (strcmp(previous, s_name) == 0) {
    return;
  }
  withPrefs([](Preferences& p) { p.putString(kKeyName, s_name); });
  Serial.printf("Device name: %s (%s)\n", s_name, s_host_url);
}

const char* hostname() { return s_hostname; }

const char* hostUrl() { return s_host_url; }

const char* setupApName() { return s_ap_name; }

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

void reset() {
  s_port = config::kDefaultHttpPort;
  s_brightness = 255;
  s_rotation = 0;
  s_invert = config::kDisplayInvert;
  withPrefs([](Preferences& p) { p.clear(); });
  applyName("");
}

}  // namespace services::device
