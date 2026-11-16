#include "ui/strip_canvas.h"

#include <algorithm>
#include <cstdlib>

namespace ui::canvas {

LGFX_Sprite s_band(&tft);
uint16_t* s_px = nullptr;
int s_by = 0;

void fillSpan(int x0, int x1, int y, uint16_t color) {
  const int ly = y - s_by;
  if (static_cast<unsigned>(ly) >= static_cast<unsigned>(kBandH)) {
    return;
  }
  x0 = std::max(x0, 0);
  x1 = std::min(x1, kW - 1);
  const uint16_t raw = swap16(color);
  uint16_t* p = &s_px[ly * kW + x0];
  for (int x = x0; x <= x1; ++x) {
    *p++ = raw;
  }
}

void blendRect(int x, int y, int w, int h, uint16_t color, uint32_t a) {
  if (!rowsVisible(y, y + h - 1)) {
    return;
  }
  const int x0 = std::max(x, 0);
  const int x1 = std::min(x + w, kW);
  const int y0 = std::max(y, s_by);
  const int y1 = std::min(y + h, s_by + kBandH);
  for (int yy = y0; yy < y1; ++yy) {
    uint16_t* p = &s_px[(yy - s_by) * kW + x0];
    for (int xx = x0; xx < x1; ++xx, ++p) {
      *p = swap16(mix565(swap16(*p), color, a));
    }
  }
}

uint32_t isqrt32(uint32_t v) {
  uint32_t res = 0;
  uint32_t bit = 1u << 30;
  while (bit > v) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (v >= res + bit) {
      v -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return res;
}

void fillDisc(int cx, int cy, int r, uint16_t color) {
  if (!rowsVisible(cy - r, cy + r)) {
    return;
  }
  const int y0 = std::max(cy - r, s_by);
  const int y1 = std::min(cy + r, s_by + kBandH - 1);
  for (int y = y0; y <= y1; ++y) {
    const int dy = y - cy;
    const int half = static_cast<int>(isqrt32(static_cast<uint32_t>(r * r - dy * dy)));
    fillSpan(cx - half, cx + half, y, color);
  }
}

/**
 * Anti-aliased circle outline. half_w16 is half the stroke width in 1/16 px.
 * Distance to the circle is approximated as (d^2 - r^2) / 2r, which is exact
 * enough within a couple of pixels of the stroke and needs no square root per
 * pixel.
 */
void ring(int cx, int cy, int r, int half_w16, uint16_t color, uint32_t a_max) {
  const int reach = (half_w16 + 15) / 16 + 1;
  if (r <= reach || !rowsVisible(cy - r - reach, cy + r + reach)) {
    return;
  }
  const int r_out = r + reach;
  const int r_in = r - reach;
  const int y0 = std::max(cy - r_out, s_by);
  const int y1 = std::min(cy + r_out, s_by + kBandH - 1);
  const int r_sq = r * r;
  for (int y = y0; y <= y1; ++y) {
    const int dy = y - cy;
    const int dy_sq = dy * dy;
    const int x_out = static_cast<int>(isqrt32(static_cast<uint32_t>(r_out * r_out - dy_sq)));
    const int x_in = (dy_sq < r_in * r_in)
                         ? static_cast<int>(isqrt32(static_cast<uint32_t>(r_in * r_in - dy_sq)))
                         : 0;
    for (int dx = x_in; dx <= x_out; ++dx) {
      const int err16 = std::abs(dx * dx + dy_sq - r_sq) * 8 / r;  // |dist - r| * 16
      int cov = half_w16 + 8 - err16;
      if (cov <= 0) {
        continue;
      }
      if (cov > 16) {
        cov = 16;
      }
      const uint32_t a = static_cast<uint32_t>(cov) * a_max / 16u;
      blendPx(cx + dx, y, color, a);
      if (dx != 0) {
        blendPx(cx - dx, y, color, a);
      }
    }
  }
}

/** Anti-aliased filled dot; r16 = radius in 1/16 px. */
void spot(int cx, int cy, int r16, uint16_t color, uint32_t a_max) {
  const int reach = (r16 + 15) / 16 + 1;
  if (!rowsVisible(cy - reach, cy + reach)) {
    return;
  }
  for (int dy = -reach; dy <= reach; ++dy) {
    for (int dx = -reach; dx <= reach; ++dx) {
      const int d16 = static_cast<int>(isqrt32(static_cast<uint32_t>((dx * dx + dy * dy) * 256)));
      int cov = r16 + 8 - d16;
      if (cov <= 0) {
        continue;
      }
      if (cov > 16) {
        cov = 16;
      }
      blendPx(cx + dx, cy + dy, color, static_cast<uint32_t>(cov) * a_max / 16u);
    }
  }
}

void thinLine(int x0, int y0, int x1, int y1, const LineStyle& style, int clip_r) {
  if (!rowsVisible(std::min(y0, y1) - 1, std::max(y0, y1) + 1)) {
    return;
  }
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int clip_sq = clip_r * clip_r;
  const auto plot = [&](int x, int y, uint32_t a) {
    const int dx = x - cx;
    const int dy = y - cy;
    if (dx * dx + dy * dy <= clip_sq) {
      blendPx(x, y, style.color, a);
    }
  };
  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int adx = std::abs(dx);
  const int ady = std::abs(dy);
  const uint32_t alpha = style.alpha;

  if (adx >= ady) {
    if (adx == 0) {
      plot(x0, y0, alpha);
      return;
    }
    if (dx < 0) {
      std::swap(x0, x1);
      std::swap(y0, y1);
    }
    const int32_t grad = static_cast<int32_t>(static_cast<int64_t>(y1 - y0) * 65536 / (x1 - x0));
    int32_t yf = y0 * 65536 + 0x8000;  // pixel centres
    for (int x = x0; x <= x1; ++x, yf += grad) {
      if (style.dashed && ((x + (yf >> 16)) & 7) >= 5) {
        continue;
      }
      const int32_t pos = yf - 0x8000;
      const int y = pos >> 16;
      const uint32_t frac = (static_cast<uint32_t>(pos) >> 11) & 31u;  // 0..31
      plot(x, y, alpha * (32u - frac) / 32u);
      plot(x, y + 1, alpha * frac / 32u);
    }
  } else {
    if (dy < 0) {
      std::swap(x0, x1);
      std::swap(y0, y1);
    }
    const int32_t grad = static_cast<int32_t>(static_cast<int64_t>(x1 - x0) * 65536 / (y1 - y0));
    // Only walk the rows that are in this strip.
    const int ya = std::max(y0, s_by);
    const int yb = std::min(y1, s_by + kBandH - 1);
    int32_t xf = x0 * 65536 + 0x8000 + grad * (ya - y0);
    for (int y = ya; y <= yb; ++y, xf += grad) {
      if (style.dashed && ((y + (xf >> 16)) & 7) >= 5) {
        continue;
      }
      const int32_t pos = xf - 0x8000;
      const int x = pos >> 16;
      const uint32_t frac = (static_cast<uint32_t>(pos) >> 11) & 31u;
      plot(x, y, alpha * (32u - frac) / 32u);
      plot(x + 1, y, alpha * frac / 32u);
    }
  }
}

}  // namespace ui::canvas
