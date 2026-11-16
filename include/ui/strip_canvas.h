#pragma once

#include <cstdint>

#include "hardware/display.h"
#include "ui/radar_theme.h"

/**
 * The strip ("band") the radar is rasterised into, plus integer-only drawing
 * helpers. All coordinates are screen coordinates; the helpers clip to the
 * strip currently being drawn.
 */
namespace ui::canvas {

constexpr int kW = radar::kSize;
constexpr int kBandH = radar::kBandHeight;

/** The shared strip sprite. */
extern LGFX_Sprite s_band;
/** Strip pixels: big-endian RGB565, as the panel takes them. */
extern uint16_t* s_px;
/** Screen y of the strip's first row. */
extern int s_by;

inline uint16_t swap16(uint16_t v) { return __builtin_bswap16(v); }

/** Blend fg over bg; a = 0..32. */
inline uint16_t mix565(uint16_t bg, uint16_t fg, uint32_t a) {
  const uint32_t b = (bg | (static_cast<uint32_t>(bg) << 16)) & 0x07E0F81Fu;
  const uint32_t f = (fg | (static_cast<uint32_t>(fg) << 16)) & 0x07E0F81Fu;
  const uint32_t r = ((b * (32u - a) + f * a) >> 5) & 0x07E0F81Fu;
  return static_cast<uint16_t>(r | (r >> 16));
}

/** True if screen rows y0..y1 (inclusive) touch the strip being drawn. */
inline bool rowsVisible(int y0, int y1) {
  return y1 >= s_by && y0 < s_by + kBandH;
}

inline void blendPx(int x, int y, uint16_t color, uint32_t a) {
  const int ly = y - s_by;
  if (a == 0 || static_cast<unsigned>(x) >= static_cast<unsigned>(kW) ||
      static_cast<unsigned>(ly) >= static_cast<unsigned>(kBandH)) {
    return;
  }
  uint16_t* p = &s_px[ly * kW + x];
  *p = swap16(a >= 32 ? color : mix565(swap16(*p), color, a));
}

void fillSpan(int x0, int x1, int y, uint16_t color);
void blendRect(int x, int y, int w, int h, uint16_t color, uint32_t a);
uint32_t isqrt32(uint32_t v);
void fillDisc(int cx, int cy, int r, uint16_t color);
/** Anti-aliased circle outline; half_w16 = half the stroke width in 1/16 px. */
void ring(int cx, int cy, int r, int half_w16, uint16_t color, uint32_t a_max = 32);
/** Anti-aliased filled dot; r16 = radius in 1/16 px. */
void spot(int cx, int cy, int r16, uint16_t color, uint32_t a_max = 32);

/** Line style for thinLine(). */
struct LineStyle {
  uint16_t color;
  uint8_t alpha;  // 0..32
  bool dashed;
};
/**
 * 1 px anti-aliased line (integer only), drawn only inside the circle of
 * radius clip_r around the screen centre.
 */
void thinLine(int x0, int y0, int x1, int y1, const LineStyle& style, int clip_r);

}  // namespace ui::canvas
