#include "hardware/display.h"

#include "config.h"
#include "hardware/display_font.h"
#include "services/device_settings.h"

LGFX tft;

void displayInit() {
  tft.init();
  tft.setTextWrap(false);
  displayApplySettings();
  displayFontInit();
}

void displayApplySettings() {
  tft.setRotation(services::device::rotation());
  tft.applyColorMode(services::device::invertColors(), config::kDisplayRgbOrder);
  // The radar is dimmed in software (see radar_display.cpp) so the slider
  // works without the BL wire. If BL is wired, the backlight follows too, on a
  // gentler curve so the two do not multiply into darkness.
  tft.setBrightness(static_cast<uint8_t>(96 + services::device::brightness() * 159 / 255));
}
