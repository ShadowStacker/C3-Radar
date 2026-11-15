#include "hardware/display_font.h"

#include <Arduino.h>
#include <lgfx/v1/lgfx_fonts.hpp>
#include <lgfx/v1/misc/DataWrapper.hpp>

#define UI_FONT_BLOB(px)                                               \
  extern const uint8_t font_##px##_start[] asm("_binary_data_ui_font_" \
                                               #px "_vlw_start");      \
  extern const uint8_t font_##px##_end[] asm("_binary_data_ui_font_"   \
                                             #px "_vlw_end");

extern "C" {
UI_FONT_BLOB(14)
UI_FONT_BLOB(16)
UI_FONT_BLOB(20)
UI_FONT_BLOB(22)
UI_FONT_BLOB(26)
}

namespace {

constexpr size_t kFontCount = static_cast<size_t>(UiFont::kCount);

struct Blob {
  const uint8_t* start;
  const uint8_t* end;
  const lgfx::IFont* fallback;
};

const Blob kBlobs[kFontCount] = {
    {font_14_start, font_14_end, &fonts::FreeSansBold9pt7b},
    {font_16_start, font_16_end, &fonts::FreeSansBold9pt7b},
    {font_20_start, font_20_end, &fonts::FreeSansBold12pt7b},
    {font_22_start, font_22_end, &fonts::FreeSansBold12pt7b},
    {font_26_start, font_26_end, &fonts::FreeSansBold18pt7b},
};

lgfx::PointerWrapper s_data[kFontCount];
lgfx::VLWfont s_fonts[kFontCount];
bool s_loaded[kFontCount];

}  // namespace

bool displayFontInit() {
  bool all = true;
  for (size_t i = 0; i < kFontCount; ++i) {
    if (s_loaded[i]) {
      continue;
    }
    const size_t len = static_cast<size_t>(kBlobs[i].end - kBlobs[i].start);
    s_data[i].set(kBlobs[i].start, len);
    s_loaded[i] = len > 0 && s_fonts[i].loadFont(&s_data[i]);
    if (!s_loaded[i]) {
      Serial.printf("UI font %u failed to load - using bitmap fallback\n",
                    static_cast<unsigned>(i));
      all = false;
    }
  }
  return all;
}

const lgfx::IFont* uiFont(UiFont font) {
  const size_t i = static_cast<size_t>(font);
  return s_loaded[i] ? static_cast<const lgfx::IFont*>(&s_fonts[i])
                     : kBlobs[i].fallback;
}
