/**
 * Plane Radar — WiFi setup, then radar UI on the round 360x360 GC9B72 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/device_settings.h"
#include "services/map_store.h"
#include "services/radar_location.h"
#include "services/web_ui.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"
#include "ui/theme.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_redraw_ms = 0;
/** While non-zero, the "Connected / web address" screen stays up until this time. */
unsigned long g_info_screen_until_ms = 0;

/** After joining Wi-Fi at boot: show where the web page lives for a moment. */
void showConnectedInfo() {
  ui::radarDisplayHidden();
  statusScreenConnected(WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                        services::device::httpPort());
  g_radar_visible = false;
  g_info_screen_until_ms = millis() + config::kConnectedScreenMs;
  if (g_info_screen_until_ms == 0) {
    g_info_screen_until_ms = 1;
  }
}

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  services::adsb::requestRefresh();
  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

// ADS-B fetch runs on its own task: the HTTPS request blocks for ~1-2 s, and
// keeping it off the main loop lets the radar keep redrawing (dead-reckoned) at
// 4 Hz throughout. The task publishes into the shared aircraft buffer under a
// lock; the render loop reads a snapshot.
void adsbFetchTask(void*) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && !services::web::updateInProgress()) {
      services::adsb::fetchUpdate(services::location::lat(),
                                  services::location::lon(),
                                  ui::radar::fetchRadiusKm(),
                                  ui::radar::militaryOnly());
    }
    // Sleeps the poll interval, or wakes early when a setting changed.
    services::adsb::waitForNextFetch(config::kAdsbFetchIntervalMs);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  services::device::init();
  ui::theme::init();
  displayInit();
  // Grab the strip buffer while the heap is still unfragmented.
  ui::radarDisplayInit();
  services::mapstore::init();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  services::adsb::init();

  if (wifiSetupConnect()) {
    showConnectedInfo();
  }

  // Start the background ADS-B fetch (pinned to core 0, away from the render
  // loop on core 1). It checks Wi-Fi state each cycle.
  xTaskCreatePinnedToCore(adsbFetchTask, "adsb", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  handleBootButton();
  wifiLoop();

  // Wherever the location was changed from (web page, setup form, reset),
  // switch to the stored map for the new spot.
  static double map_lat = 1000.0;
  static double map_lon = 1000.0;
  if (map_lat != services::location::lat() || map_lon != services::location::lon()) {
    map_lat = services::location::lat();
    map_lon = services::location::lon();
    services::mapstore::selectForLocation();
  }

  if (services::web::rebootPending()) {
    Serial.println("Firmware updated - restarting");
    delay(400);  // let the browser receive the reply
    ESP.restart();
  }

  if (WiFi.status() != WL_CONNECTED) {
    g_info_screen_until_ms = 0;
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (g_info_screen_until_ms != 0) {
      if (static_cast<long>(millis() - g_info_screen_until_ms) >= 0) {
        g_info_screen_until_ms = 0;
      }
    } else if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (millis() - g_last_redraw_ms >= config::kRadarRedrawIntervalMs) {
      // Redraw at 4 Hz with dead-reckoned positions; the ADS-B fetch runs on
      // its own task (adsbFetchTask).
      g_last_redraw_ms = millis();
      ui::radarDisplayRefreshAircraft();
    }
  }

  delay(10);
}
