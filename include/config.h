#pragma once

#include <cstdint>

#include <driver/gpio.h>
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>  // CONFIG_IDF_TARGET_* (absent in the PC preview build)
#endif

#define PLANE_RADAR_VERSION "0.14.1"

namespace config {

constexpr char kFirmwareVersion[] = PLANE_RADAR_VERSION;

// --- Wi-Fi portal ---
constexpr char kPortalIp[] = "192.168.4.1";
/**
 * Prefix of the default device name / mDNS host: "plane-radar-xxxx", where
 * xxxx comes from the chip's MAC so several radars can share a network. The
 * name can be changed on the web page (services::device::name()).
 */
constexpr char kPortalHostname[] = "plane-radar";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- Board wiring ---
// BOOT is the on-board button (active LOW). The display pins are picked so the
// wires sit next to each other on each board's header.
#if CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3 SuperMini: 3V3, GND, 13, 12, 11, 10, 9, 8 are one row of the header.
constexpr char kBoardName[] = "ESP32-S3 SuperMini";
constexpr gpio_num_t kBootPin = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinBl = GPIO_NUM_13;
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_12;  // display SCL
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_11;  // display SDA
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_9;
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_8;
#else
// ESP32-C3 Super Mini.
constexpr char kBoardName[] = "ESP32-C3 Super Mini";
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL
/** Backlight (BL / BLK pin), PWM dimmed. Leave unconnected for always-on. */
constexpr gpio_num_t kDisplayPinBl = GPIO_NUM_5;
#endif
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: GC9B72 2.1" round 360x360 (SPI) ---
constexpr uint8_t kDisplayPwmChannel = 0;
constexpr uint32_t kDisplayPwmFreq = 5000;

constexpr int kDisplayWidth = 360;
constexpr int kDisplayHeight = 360;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
/** Colour inversion default; can be flipped from the web page. */
constexpr bool kDisplayInvert = false;
/** These GC9B72 modules are wired BGR: without this, reds show as blue. */
constexpr bool kDisplayRgbOrder = true;

// --- Web server ---
/** Default HTTP port for the LAN web page (changeable from the web page). */
constexpr uint16_t kDefaultHttpPort = 80;
/** How long the "Connected / address" screen stays up after joining Wi-Fi. */
constexpr unsigned long kConnectedScreenMs = 3500;

// --- Radar center defaults (overridden via WiFi setup portal) ---
// Redmond Municipal Airport (Roberts Field, KRDM), Oregon.
constexpr double kDefaultRadarLat = 44.2541;
constexpr double kDefaultRadarLon = -121.1500;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 5000;
/** Redraw cadence; aircraft are dead-reckoned along their track/speed so motion
 *  is smooth. ~4 Hz = 250 ms (panel scanout caps at ~24 Hz; the full-frame
 *  recompose+present is the practical limit ~10-15 Hz). */
constexpr unsigned long kRadarRedrawIntervalMs = 250;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;
/** Show a "NO DATA" badge when the last good ADS-B fetch is older than this. */
constexpr unsigned long kAdsbStaleMs = 30000;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
