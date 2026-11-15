#pragma once

#include <cstdint>

/** Device-level settings stored in NVS: web server port and display options. */
namespace services::device {

/** Load from NVS. Call once at boot, before the display and Wi-Fi come up. */
void init();

/** TCP port of the LAN web page (the Wi-Fi setup hotspot always uses 80). */
uint16_t httpPort();
/** Persist a new port (1..65535). Returns false and changes nothing if invalid. */
bool setHttpPort(long port);

/** Backlight level 5..255 (needs the display BL pin wired to the backlight GPIO). */
uint8_t brightness();
void setBrightness(long level);

/** Screen rotation in quarter turns, 0..3. */
uint8_t rotation();
void setRotation(long quarter_turns);

/** Panel colour fixes for modules that ship with a different glass variant. */
bool invertColors();
void setInvertColors(bool on);
bool swapRedBlue();
void setSwapRedBlue(bool on);

/** Back to defaults (used by the BOOT long-press reset). */
void reset();

}  // namespace services::device
