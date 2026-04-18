#pragma once
#include <stdint.h>
#include <time.h>

// Public API — called from main.cpp
// ---------------------------------------------------------------------------

// Reset internal dirty-tracking state so the next tick paints the full face.
// Call on mode entry and whenever the time-valid flag flips.
void clock_face_invalidate();

// Called once per main-loop iteration while DisplayMode == DISP_CLOCK.
// Reads time(), compares against cached minute/second/day, redraws and
// pushes the sprite only when something changed. Also handles the
// BLE-time-not-synced placeholder layout.
void clock_face_tick();


// Internal helpers exposed for native unit tests only. Do not call from
// main.cpp or other modules.
// ---------------------------------------------------------------------------
namespace clock_face_internal {

// Formats a `struct tm` into three null-terminated strings:
//   hm:   "HH:MM"           5 chars + null, buffer >= 6
//   ss:   "SS"              2 chars + null, buffer >= 3
//   date: "Day Mon DD"     10 chars + null, buffer >= 16
// Buffers MUST be at least the sizes above. Day-of-week uses {Sun,Mon,Tue,
// Wed,Thu,Fri,Sat}[tm.tm_wday]. Month uses {Jan,...,Dec}[tm.tm_mon]. Day
// of month is zero-padded to 2 chars.
void fmt_time_fields(const struct tm& t, char* hm, char* ss, char* date);

// Returns true if any of the three displayed fields (minute, second, day
// of month) differs from the cached values, and updates the cache. Used by
// clock_face_tick to decide whether to redraw+push.
//
// `cachedMin`, `cachedSec`, `cachedDay` are in/out: read for comparison,
// written with the current values if a change is detected.
bool needs_redraw(const struct tm& t,
                  int& cachedMin, int& cachedSec, int& cachedDay);

}  // namespace clock_face_internal
