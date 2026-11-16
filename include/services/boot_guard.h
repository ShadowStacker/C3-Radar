#pragma once

#include <cstdint>

/**
 * Crash-loop protection. If the radar keeps restarting abnormally (crash,
 * watchdog, brownout) before it has run for a while, it starts in safe mode
 * with the most likely culprits switched off, so the web page stays reachable
 * for removing a bad map or installing other firmware.
 */
namespace services::boot {

/** Abnormal restarts in a row after which stored maps are not loaded. */
constexpr uint8_t kSkipMapsAfter = 3;
/** ...and after which aircraft data is not fetched either. */
constexpr uint8_t kSkipFetchAfter = 5;
/** Running this long counts as a good start. */
constexpr unsigned long kStableMs = 45000;

/** Call first thing in setup(). */
void begin();
/** Call from loop(); clears the streak once the radar has been up long enough. */
void loop();

/** Abnormal restarts in a row (0 after a good start). */
uint8_t crashStreak();
bool safeMode();
bool skipMaps();
bool skipFetch();
/** Why the chip last restarted, in plain words. */
const char* resetReason();
/** Forget the streak (the user asked to try a normal start). */
void clear();

}  // namespace services::boot
