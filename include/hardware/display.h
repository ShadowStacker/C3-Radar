#pragma once

#ifdef PLANE_RADAR_HOST
#include "host_lgfx.hpp"  // PC preview build: the "panel" is an off-screen sprite
#else
#include "hardware/lgfx_config.hpp"
#endif

extern LGFX tft;

void displayInit();
/** Push brightness / rotation / colour-mode settings to the panel. */
void displayApplySettings();
