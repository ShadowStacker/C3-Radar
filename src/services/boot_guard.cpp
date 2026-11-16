#include "services/boot_guard.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

namespace services::boot {

namespace {

constexpr char kNamespace[] = "boot";
constexpr char kKeyStreak[] = "streak";

uint8_t s_streak = 0;
bool s_marked_good = false;
esp_reset_reason_t s_reason = ESP_RST_UNKNOWN;

void store(uint8_t value) {
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putUChar(kKeyStreak, value);
    prefs.end();
  }
}

bool abnormal(esp_reset_reason_t reason) {
  return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
         reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
         reason == ESP_RST_BROWNOUT;
}

}  // namespace

void begin() {
  s_reason = esp_reset_reason();
  Preferences prefs;
  if (prefs.begin(kNamespace, true)) {
    s_streak = prefs.getUChar(kKeyStreak, 0);
    prefs.end();
  }
  // Only restarts the chip did not ask for count; unplugging or pressing
  // RESET does not.
  if (abnormal(s_reason)) {
    if (s_streak < 250) {
      ++s_streak;
    }
    store(s_streak);
  }
  Serial.printf("Last restart: %s; abnormal restarts in a row: %u%s\n", resetReason(),
                static_cast<unsigned>(s_streak), safeMode() ? " - SAFE MODE" : "");
}

void loop() {
  if (!s_marked_good && millis() > kStableMs) {
    s_marked_good = true;
    if (s_streak != 0 && !safeMode()) {
      // A good start in normal mode: the earlier restarts were a passing problem.
      s_streak = 0;
      store(0);
    }
  }
}

uint8_t crashStreak() { return s_streak; }

bool safeMode() { return s_streak >= kSkipMapsAfter; }

bool skipMaps() { return s_streak >= kSkipMapsAfter; }

bool skipFetch() { return s_streak >= kSkipFetchAfter; }

const char* resetReason() {
  switch (s_reason) {
    case ESP_RST_POWERON:
      return "power on";
    case ESP_RST_EXT:
      return "reset button";
    case ESP_RST_SW:
      return "restart";
    case ESP_RST_PANIC:
      return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "watchdog (the program froze)";
    case ESP_RST_BROWNOUT:
      return "brownout (supply voltage dropped)";
    case ESP_RST_DEEPSLEEP:
      return "wake from sleep";
    default:
      return "unknown";
  }
}

void clear() {
  s_streak = 0;
  store(0);
}

}  // namespace services::boot
