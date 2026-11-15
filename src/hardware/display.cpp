#include "hardware/display.h"

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
  tft.applyColorMode(services::device::invertColors(),
                     services::device::swapRedBlue());
  tft.setBrightness(services::device::brightness());
}
