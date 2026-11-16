#pragma once

#include <cstddef>
#include <cstdint>

namespace ui {

/** Allocate the strip buffer. Call once at boot, before Wi-Fi fragments the heap. */
bool radarDisplayInit();

/** Compose a fresh frame (grid, runways, aircraft) and send it to the panel. */
void radarDisplayDraw();

/** Same as radarDisplayDraw(); kept for the periodic redraw call site. */
void radarDisplayRefreshAircraft();

/** Tell the renderer a status screen replaced the radar (stops the web mirror). */
void radarDisplayHidden();

/** Average time to draw and send one frame, in milliseconds. */
uint32_t radarDisplayFrameMs();

/** Aircraft drawn in the last frame: inside the outer ring / as rim markers. */
size_t radarDisplayInRangeCount();
size_t radarDisplayBeyondCount();

/**
 * Re-render the frame that is currently on the panel, strip by strip, as
 * little-endian RGB565 rows (top to bottom) - used for the web page mirror.
 * `sink` returns false to abort. Returns false if the radar is not on screen.
 * Pass a null sink to just ask whether a frame is available.
 */
using FrameSink = bool (*)(const uint8_t* rows, size_t bytes, void* ctx);
bool radarDisplayStreamFrame(FrameSink sink, void* ctx);

}  // namespace ui
