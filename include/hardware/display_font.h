#pragma once

#include <LovyanGFX.hpp>

/** Embedded anti-aliased UI fonts, each rendered at its native pixel size. */
enum class UiFont : uint8_t {
  kSmall14 = 0,  // aircraft tags, runway / scale labels, badges
  kMedium16,
  kCardinal20,   // N / E / S / W
  kBody22,       // status screens
  kTitle26,      // status screen titles
  kCount,
};

/** Parse the embedded fonts. Call once after the display is up. */
bool displayFontInit();

/** Font to pass to setFont(); falls back to a built-in bitmap font if needed. */
const lgfx::IFont* uiFont(UiFont font);
