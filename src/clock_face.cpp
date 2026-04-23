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

#ifdef ARDUINO

// Private module state
static bool    _valid      = false;
static bool    _lastValid  = false;
static int     _cachedMin  = -1;
static int     _cachedSec  = -1;
static int     _cachedDay  = -1;

void clock_face_invalidate() {
  _cachedMin = -1;
  _cachedSec = -1;
  _cachedDay = -1;
  _lastValid = !_valid;   // Force repaint on next tick regardless of validity
}

static void paintInvalid() {
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->fillScreen(BLACK);

  canvas->setTextColor(WHITE, BLACK);
  canvas->setTextSize(6);
  canvas->setCursor(120 - 6*6*3, 80 - 4*6); canvas->print("--:--");

  canvas->setTextSize(1);
  canvas->setTextColor(DARKGREY, BLACK);
  canvas->setCursor(120 - 7*3, 130 - 4); canvas->print("(no time)");
  canvas->setCursor(120 - 11*3, 150 - 4); canvas->print("waiting for");
  canvas->setCursor(120 - 6*3, 160 - 4); canvas->print("Claude");
}

static void paintValid(const struct tm& t) {
  char hm[6], ss[3], date[16];
  clock_face_internal::fmt_time_fields(t, hm, ss, date);

  Arduino_GFX* canvas = hw_display_canvas();
  canvas->fillScreen(BLACK);

  canvas->setTextColor(WHITE, BLACK);
  canvas->setTextSize(6);
  int16_t tw = strlen(hm) * 6 * 6;
  canvas->setCursor(120 - tw/2, 70 - 4*6);
  canvas->print(hm);

  canvas->setTextColor(LIGHTGREY, BLACK);
  canvas->setTextSize(3);
  tw = strlen(ss) * 6 * 3;
  canvas->setCursor(120 - tw/2, 135 - 4*3);
  canvas->print(ss);

  canvas->setTextColor(DARKGREY, BLACK);
  canvas->setTextSize(2);
  tw = strlen(date) * 6 * 2;
  canvas->setCursor(120 - tw/2, 180 - 4*2);
  canvas->print(date);

  hw_display_flush();
}

void clock_face_tick() {
  time_t now;
  time(&now);
  _valid = (now >= TIME_SYNCED_MIN_EPOCH);

  // Validity transition forces a full repaint of the new layout.
  if (_valid != _lastValid) {
    _lastValid = _valid;
    _cachedMin = -1; _cachedSec = -1; _cachedDay = -1;
    if (!_valid) { paintInvalid(); hw_display_flush(); return; }
    // else fall through to the valid branch below for first paint
  }

  if (!_valid) {
    // Static placeholder; already painted on the transition. Nothing else
    // to do until time syncs.
    return;
  }

  struct tm lt;
  localtime_r(&now, &lt);

  if (clock_face_internal::needs_redraw(lt, _cachedMin, _cachedSec, _cachedDay)) {
    paintValid(lt);
  }
}

#else

// Host-test build: render API is not exercised by tests.
void clock_face_invalidate() {}
void clock_face_tick()       {}

#endif
