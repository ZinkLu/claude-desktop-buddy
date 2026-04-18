#include "clock_face.h"
#include <stdio.h>
#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#include "hw_display.h"
#endif

// Time is considered "valid" (synced) when time() returns something after
// this epoch. 1735689600 == 2025-01-01 00:00 UTC; ESP32 boots at 1970, so
// anything post-2025 must have come from the BLE bridge's settimeofday.
// This avoids including data.h here (which uses file-scope static state
// and can't be safely included from a second translation unit).
static const time_t TIME_SYNCED_MIN_EPOCH = 1735689600;

namespace clock_face_internal {

static const char* DOW[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char* MON[] = {"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};

void fmt_time_fields(const struct tm& t, char* hm, char* ss, char* date) {
  snprintf(hm,   6,  "%02d:%02d", t.tm_hour, t.tm_min);
  snprintf(ss,   3,  "%02d",      t.tm_sec);
  const char* dow = (t.tm_wday >= 0 && t.tm_wday < 7) ? DOW[t.tm_wday] : "???";
  const char* mon = (t.tm_mon  >= 0 && t.tm_mon  < 12) ? MON[t.tm_mon]  : "???";
  snprintf(date, 16, "%s %s %02d", dow, mon, t.tm_mday);
}

bool needs_redraw(const struct tm& t, int& cachedMin, int& cachedSec, int& cachedDay) {
  bool changed = (t.tm_min != cachedMin) ||
                 (t.tm_sec != cachedSec) ||
                 (t.tm_mday != cachedDay);
  if (changed) {
    cachedMin = t.tm_min;
    cachedSec = t.tm_sec;
    cachedDay = t.tm_mday;
  }
  return changed;
}

}  // namespace clock_face_internal

// Render stubs — Task 4 replaces these with real paintValid/paintInvalid logic.
void clock_face_invalidate() {}
void clock_face_tick()       {}
