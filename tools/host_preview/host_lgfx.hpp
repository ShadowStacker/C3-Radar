// PC preview build: the "panel" is a 360x360 off-screen sprite.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

class LGFX : public LGFX_Sprite {
 public:
  void init() {
    setColorDepth(lgfx::color_depth_t::rgb565_2Byte);
    createSprite(config::kDisplayWidth, config::kDisplayHeight);
  }
  void applyColorMode(bool, bool) {}
  void setBrightness(uint8_t) {}
};
