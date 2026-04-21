# Phase 2-D4 Implementation Plan

> **Date**: 2026-04-21  
> **Goal**: Implement clock enrichment feature

## Task 1: Add time-based state derivation

**File**: `src/clock_face.cpp`

Add private function after the namespace block (around line 43):

```cpp
// PersonaState values matching main.cpp enum
enum PersonaState { P_SLEEP = 0, P_IDLE = 1, P_BUSY = 2, 
                    P_ATTENTION = 3, P_CELEBRATE = 4, P_DIZZY = 5, P_HEART = 6 };

static uint8_t deriveClockBuddyState(const struct tm& t) {
  int wday = t.tm_wday;  // 0=Sun, 1=Mon, ..., 5=Fri, 6=Sat
  int hour = t.tm_hour;
  
  // Priority: sleep > celebrate > heart > dizzy > idle
  if (hour >= 1 && hour < 7) {
    return P_SLEEP;  // 01:00-07:00
  }
  if (wday == 5 && hour >= 12) {
    return P_CELEBRATE;  // Friday 12:00+
  }
  if (wday == 0 || wday == 6) {
    return P_HEART;  // Sat/Sun
  }
  if (hour >= 22) {
    return P_DIZZY;  // 22:00+
  }
  return P_IDLE;
}
```

## Task 2: Modify paintValid to render buddy

**File**: `src/clock_face.cpp`

After line 99 (after drawing date), add:

```cpp
  // Draw mood-based buddy below the date (D4)
  uint8_t buddyState = deriveClockBuddyState(lt);
  buddyRenderTo(&sp, buddyState);
```

## Task 3: Modify paintInvalid to render idle buddy

**File**: `src/clock_face.cpp`

After line 74 (after "waiting for Claude"), add:

```cpp
  // Show idle buddy even when time is invalid (D4)
  buddyRenderTo(&sp, 1);  // P_IDLE = 1
```

## Task 4: Include buddy.h

**File**: `src/clock_face.cpp`

Add at the top with other includes:

```cpp
#include "buddy.h"
```

## Task 5: Verify and test

1. Run `~/.platformio/penv/bin/pio run` to verify firmware builds
2. Run `~/.platformio/penv/bin/pio test -e native` to run host tests

## Notes

- No changes to clock_face.h needed
- buddyRenderTo handles its own animation timing
- Position: buddyRenderTo uses center coords internally, default positioning should work
