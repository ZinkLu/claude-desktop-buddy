#include "clock_face.h"
#include <stdio.h>
#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#include "hw_display.h"
#include "buddy.h"
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

// PersonaState values matching main.cpp enum (D4)
enum PersonaState { P_SLEEP = 0, P_IDLE = 1, P_BUSY = 2,
                    P_ATTENTION = 3, P_CELEBRATE = 4, P_DIZZY = 5, P_HEART = 6 };

// Derive buddy mood state from current time (D4).
// Priority: sleep > celebrate > heart > dizzy > idle
static uint8_t deriveClockBuddyState(const struct tm& t) {
  int wday = t.tm_wday;  // 0=Sun, 1=Mon, ..., 5=Fri, 6=Sat
  int hour = t.tm_hour;

  if (hour >= 1 && hour < 7) {
    return P_SLEEP;       // 01:00 - 07:00
  }
  if (wday == 5 && hour >= 12) {
    return P_CELEBRATE;   // Friday 12:00+
  }
  if (wday == 0 || wday == 6) {
    return P_HEART;       // Sat / Sun
  }
  if (hour >= 22) {
    return P_DIZZY;       // 22:00+
  }
  return P_IDLE;
}

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
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(TFT_BLACK);

  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.setTextSize(6);
  sp.drawString("--:--", 120, 80);

  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("(no time)", 120, 130);
  sp.drawString("waiting for", 120, 150);
  sp.drawString("Claude", 120, 160);

  // Show idle buddy even when time is invalid (D4)
  buddyRenderTo(&sp, P_IDLE, 0, 40);

  sp.setTextDatum(TL_DATUM);
  sp.pushSprite(0, 0);
}

static void paintValid(const struct tm& t) {
  char hm[6], ss[3], date[16];
  clock_face_internal::fmt_time_fields(t, hm, ss, date);

  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(TFT_BLACK);

  sp.setTextDatum(MC_DATUM);

  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.setTextSize(6);
  sp.drawString(hm, 120, 70);

  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.setTextSize(3);
  sp.drawString(ss, 120, 135);

  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextSize(2);
  sp.drawString(date, 120, 180);

  // Draw mood-based buddy below the date (D4)
  uint8_t buddyState = deriveClockBuddyState(t);
  buddyRenderTo(&sp, buddyState, 0, 40);

  sp.setTextDatum(TL_DATUM);
  sp.pushSprite(0, 0);
}

void clock_face_tick() {
  time_t now;
  time(&now);
  _valid = (now >= TIME_SYNCED_MIN_EPOCH);

  // Validity transition forces a full repaint of the new layout.
  if (_valid != _lastValid) {
    _lastValid = _valid;
    _cachedMin = -1; _cachedSec = -1; _cachedDay = -1;
    if (!_valid) { paintInvalid(); return; }
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
