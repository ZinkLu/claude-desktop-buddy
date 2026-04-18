# Phase 2-C Clock Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a clock display mode to the X-Knob firmware: long-press toggles home ↔ clock, clock shows HH:MM / :SS / Day Mon DD when BLE time has been synced, or a `--:--` placeholder before sync; any incoming prompt preempts clock and returns to home.

**Architecture:** New module `src/clock_face.{h,cpp}` owns the clock rendering and internal dirty-tracking; `main.cpp` gains a `DisplayMode` enum, a LONG-event toggle, and a prompt-interrupt path. No other modules change. Time comes from `time(nullptr)` which `data.h` already populates via `settimeofday` after a BLE `time` packet.

**Tech Stack:**
- Same as Phase 1 — ESP32-S3 Arduino, TFT_eSPI 2.5.43 + 240×240 PSRAM sprite, standard C time functions.
- Native unit tests via PlatformIO Unity on `[env:native]` (already set up in Phase 1 Task 4).

**Spec:** `docs/superpowers/specs/2026-04-18-phase2c-clock-design.md`
**Source paths:**
- Working dir: `/Users/zinklu/code/opensources/claude-desktop-buddy`
- PIO: `~/.platformio/penv/bin/pio`
- Target branch: create `phase-2c` from `main` before starting (main currently has Phase 1 merged at `8c696a5` plus the spec commit).

---

## File Structure

**New:**
- `src/clock_face.h` — public API: `clock_face_invalidate()`, `clock_face_tick()`, plus a `clock_face_internal::` namespace exposing the pure formatters for native tests.
- `src/clock_face.cpp` — implementation (valid-time render, invalid-time fallback, dirty tracking).
- `test/test_clock_format/test_clock_format.cpp` — Unity tests for the formatter functions.

**Modified:**
- `src/main.cpp` — add `#include "clock_face.h"`, add `DisplayMode` enum + state, route `EVT_LONG` to a toggle, branch render loop on mode, force-exit to home on new prompt.
- `platformio.ini` — extend `build_src_filter` to include `clock_face.cpp`.

**Untouched:** `ble_bridge.{h,cpp}`, `hw_*.{h,cpp}`, `buddy.{h,cpp}`, `buddies/*.cpp`, `character.{h,cpp}`, `data.h`, `stats.h`, `xfer.h`.

---

## Pre-flight

- [ ] **Check you are on main and synced**

Run:
```bash
cd /Users/zinklu/code/opensources/claude-desktop-buddy
git status
git log --oneline -3
```
Expected:
- Clean working tree (or only `.claude/` untracked).
- HEAD at `6e23abd Add Phase 2-C clock mode design doc` or a later commit.

If not on main: `git checkout main && git pull origin main`.

- [ ] **Create the `phase-2c` branch**

Run:
```bash
git checkout -b phase-2c
```

All Phase 2-C commits land on this branch. Merge to main via PR at the end.

---

## Task 1: `clock_face.h` interface

Establish the public API and the internal-namespace formatter signatures that Task 2 (tests) and Task 3 (implementation) depend on.

**Files:**
- Create: `src/clock_face.h`

- [ ] **Step 1.1: Write `src/clock_face.h`**

`src/clock_face.h`:
```cpp
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
```

- [ ] **Step 1.2: Build to verify header compiles**

Run:
```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: `[SUCCESS]`. Header isn't referenced yet so nothing new compiles; the build should just pass unchanged.

- [ ] **Step 1.3: Commit**

```bash
git add src/clock_face.h
git commit -m "clock_face: add public header with internal helpers for tests

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Native tests for formatter (TDD red)

**Files:**
- Create: `test/test_clock_format/test_clock_format.cpp`
- Modify: `platformio.ini` (extend `[env:native]` `build_src_filter` to include `clock_face.cpp` once it exists — add now so tests can link).

- [ ] **Step 2.1: Extend the native env source filter**

Open `platformio.ini`. Under `[env:native]`, the current `build_src_filter = +<hw_input.cpp>` needs `clock_face.cpp` too. Edit:

```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17
build_src_filter = +<hw_input.cpp> +<clock_face.cpp>
test_build_src = yes
```

- [ ] **Step 2.2: Write the test file**

`test/test_clock_format/test_clock_format.cpp`:
```cpp
#include <unity.h>
#include <time.h>
#include <string.h>
#include "../../src/clock_face.h"

using namespace clock_face_internal;

static struct tm mktm(int hour, int min, int sec, int mday, int mon, int wday) {
  struct tm t{};
  t.tm_hour = hour;
  t.tm_min  = min;
  t.tm_sec  = sec;
  t.tm_mday = mday;
  t.tm_mon  = mon;   // 0-based
  t.tm_wday = wday;  // 0 = Sunday
  return t;
}

void setUp() {}
void tearDown() {}

void test_fmt_typical() {
  struct tm t = mktm(12, 34, 56, 18, 3 /* Apr */, 5 /* Fri */);
  char hm[6], ss[3], date[16];
  fmt_time_fields(t, hm, ss, date);
  TEST_ASSERT_EQUAL_STRING("12:34", hm);
  TEST_ASSERT_EQUAL_STRING("56", ss);
  TEST_ASSERT_EQUAL_STRING("Fri Apr 18", date);
}

void test_fmt_zero_pad_day() {
  struct tm t = mktm(9, 5, 3, 1, 0 /* Jan */, 0 /* Sun */);
  char hm[6], ss[3], date[16];
  fmt_time_fields(t, hm, ss, date);
  TEST_ASSERT_EQUAL_STRING("09:05", hm);
  TEST_ASSERT_EQUAL_STRING("03", ss);
  TEST_ASSERT_EQUAL_STRING("Sun Jan 01", date);
}

void test_fmt_midnight() {
  struct tm t = mktm(0, 0, 0, 31, 11 /* Dec */, 6 /* Sat */);
  char hm[6], ss[3], date[16];
  fmt_time_fields(t, hm, ss, date);
  TEST_ASSERT_EQUAL_STRING("00:00", hm);
  TEST_ASSERT_EQUAL_STRING("00", ss);
  TEST_ASSERT_EQUAL_STRING("Sat Dec 31", date);
}

void test_needs_redraw_nothing_changed() {
  struct tm t = mktm(12, 34, 56, 18, 3, 5);
  int cm = 34, cs = 56, cd = 18;
  TEST_ASSERT_FALSE(needs_redraw(t, cm, cs, cd));
  TEST_ASSERT_EQUAL(34, cm);
  TEST_ASSERT_EQUAL(56, cs);
  TEST_ASSERT_EQUAL(18, cd);
}

void test_needs_redraw_second_tick() {
  struct tm t = mktm(12, 34, 57, 18, 3, 5);
  int cm = 34, cs = 56, cd = 18;
  TEST_ASSERT_TRUE(needs_redraw(t, cm, cs, cd));
  TEST_ASSERT_EQUAL(57, cs);
  TEST_ASSERT_EQUAL(34, cm);  // minute unchanged
  TEST_ASSERT_EQUAL(18, cd);
}

void test_needs_redraw_minute_rolls() {
  struct tm t = mktm(12, 35, 0, 18, 3, 5);
  int cm = 34, cs = 59, cd = 18;
  TEST_ASSERT_TRUE(needs_redraw(t, cm, cs, cd));
  TEST_ASSERT_EQUAL(35, cm);
  TEST_ASSERT_EQUAL(0, cs);
}

void test_needs_redraw_day_rolls() {
  struct tm t = mktm(0, 0, 0, 19, 3, 6);
  int cm = 59, cs = 59, cd = 18;
  TEST_ASSERT_TRUE(needs_redraw(t, cm, cs, cd));
  TEST_ASSERT_EQUAL(19, cd);
}

void test_needs_redraw_first_call_with_sentinel_cache() {
  // Fresh cache with -1 sentinel must always trigger redraw.
  struct tm t = mktm(0, 0, 0, 1, 0, 0);
  int cm = -1, cs = -1, cd = -1;
  TEST_ASSERT_TRUE(needs_redraw(t, cm, cs, cd));
  TEST_ASSERT_EQUAL(0, cm);
  TEST_ASSERT_EQUAL(0, cs);
  TEST_ASSERT_EQUAL(1, cd);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fmt_typical);
  RUN_TEST(test_fmt_zero_pad_day);
  RUN_TEST(test_fmt_midnight);
  RUN_TEST(test_needs_redraw_nothing_changed);
  RUN_TEST(test_needs_redraw_second_tick);
  RUN_TEST(test_needs_redraw_minute_rolls);
  RUN_TEST(test_needs_redraw_day_rolls);
  RUN_TEST(test_needs_redraw_first_call_with_sentinel_cache);
  return UNITY_END();
}
```

- [ ] **Step 2.3: Run tests — expect link failure**

Run:
```bash
~/.platformio/penv/bin/pio test -e native -f test_clock_format 2>&1 | tail -25
```
Expected: linker error about unresolved `clock_face_internal::fmt_time_fields` and `::needs_redraw` symbols (because `clock_face.cpp` doesn't exist yet). OR compile error if the include path failed — check the path `../../src/clock_face.h` resolves.

If the tests somehow pass or run: something is wrong; the implementation file shouldn't exist yet. Don't proceed.

- [ ] **Step 2.4: Commit the failing tests**

```bash
git add test/test_clock_format/test_clock_format.cpp platformio.ini
git commit -m "clock_face: add failing unit tests for formatter and dirty check

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: `clock_face.cpp` formatters (TDD green)

**Files:**
- Create: `src/clock_face.cpp`

- [ ] **Step 3.1: Write `src/clock_face.cpp` with formatter-only scaffolding**

This version makes Task 2's tests pass. The render path (Task 4) is a stub that does nothing until the on-device task.

`src/clock_face.cpp`:
```cpp
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

// Render stubs — filled in Task 4.
void clock_face_invalidate() {}
void clock_face_tick()       {}
```

- [ ] **Step 3.2: Run tests — expect all green**

Run:
```bash
~/.platformio/penv/bin/pio test -e native -f test_clock_format 2>&1 | tail -15
```
Expected: all 8 tests pass.

If any fail, read the Unity output and fix the formatter before continuing.

- [ ] **Step 3.3: Confirm firmware env still builds**

Run:
```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: `[SUCCESS]`. Nothing calls `clock_face_*` from main yet; the file just needs to compile in the firmware env too. If it fails because `platformio.ini` `build_src_filter` for `env:x-knob` doesn't yet include `clock_face.cpp`, add it now:

Edit the `env:x-knob` `build_src_filter` line to append `+<clock_face.cpp>`. Rebuild.

- [ ] **Step 3.4: Commit**

```bash
git add src/clock_face.cpp platformio.ini
git commit -m "clock_face: formatters pass native tests; render stubbed

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Implement render (valid-time + invalid-time fallback)

Flesh out the stubs. Reads `time()` each tick, uses `dataRtcValid()` from `data.h` to decide which layout to paint.

**Files:**
- Modify: `src/clock_face.cpp`

- [ ] **Step 4.1: Replace the stubs with the real render**

Open `src/clock_face.cpp`. Replace the two stub functions at the bottom with:

```cpp
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
```

Also remove the now-redundant empty stubs at the very bottom of the file.

- [ ] **Step 4.2: Build firmware**

Run:
```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -15
```
Expected: `[SUCCESS]`. If you see `time()` or `localtime_r` unresolved, make sure `#include <time.h>` is in `clock_face.h` (it already is per Task 1).

- [ ] **Step 4.3: Re-run native tests (regression check)**

Run:
```bash
~/.platformio/penv/bin/pio test -e native -f test_clock_format 2>&1 | tail -15
```
Expected: still all 8 pass. The formatter code path is unchanged — tests should still be green.

- [ ] **Step 4.4: Commit**

```bash
git add src/clock_face.cpp
git commit -m "clock_face: implement valid + invalid render paths

paintValid renders HH:MM/SS/date at sizes 6/3/2. paintInvalid shows
--:-- plus 'waiting for Claude' placeholder. clock_face_tick reads
time() on each call, compares against cached min/sec/day, and only
pushes the sprite when one of those fields changed. Validity flip
between invalid->valid (e.g. after first BLE time sync) forces a
full repaint.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Route `EVT_LONG` and `DisplayMode` in `main.cpp`

Wire up the mode toggle and branch the render path.

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 5.1: Read the current `main.cpp`**

Run:
```bash
sed -n '1,30p' src/main.cpp
```
Record the current include list. You will add one line.

- [ ] **Step 5.2: Add the include**

Open `src/main.cpp`. After the existing `#include "buddy.h"` line, add:
```cpp
#include "clock_face.h"
```

- [ ] **Step 5.3: Add the `DisplayMode` enum and state near the top**

Immediately after the `PersonaState` enum declaration (below the last `enum ...` line), add:

```cpp
// Phase 2-C: home ↔ clock toggle via LONG press.
enum DisplayMode { DISP_HOME, DISP_CLOCK };
static DisplayMode displayMode = DISP_HOME;
```

- [ ] **Step 5.4: Route `EVT_LONG` to toggle the mode**

Locate the `switch (e)` block in `loop()`. Find the `case EVT_LONG:` branch (currently prints `"LONG"`). Replace it with:

```cpp
    case EVT_LONG:
      Serial.println("LONG");
      if (displayMode == DISP_HOME) {
        displayMode = DISP_CLOCK;
        clock_face_invalidate();
      } else {
        displayMode = DISP_HOME;
        buddyInvalidate();
      }
      break;
```

- [ ] **Step 5.5: Branch the render path**

Locate the render block that reads:
```cpp
  TFT_eSprite& sp = hw_display_sprite();
  if (firstFrame) { sp.fillSprite(TFT_BLACK); firstFrame = false; }
  buddyTick((uint8_t)activeState);
  ...
  if (inPrompt) drawApproval();
  else          drawHudSimple();
  sp.pushSprite(0, 0);
```

Wrap it in a mode check. Replace the block with:

```cpp
  TFT_eSprite& sp = hw_display_sprite();
  if (firstFrame) { sp.fillSprite(TFT_BLACK); firstFrame = false; }

  if (displayMode == DISP_CLOCK) {
    clock_face_tick();
  } else {
    if (buddyMode) {
      buddyTick((uint8_t)activeState);
    } else if (characterLoaded()) {
      characterSetState((uint8_t)activeState);
      characterTick();
    } else {
      sp.setTextDatum(MC_DATUM);
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString("no character", 120, 120);
      sp.setTextDatum(TL_DATUM);
    }
    if (inPrompt) drawApproval();
    else          drawHudSimple();
    sp.pushSprite(0, 0);
  }
```

Notes:
- `clock_face_tick()` does its own `pushSprite`, so the home branch keeps the push at the end and the clock branch does not duplicate.
- `characterSetState` / `characterTick` fallbacks and `drawApproval` / `drawHudSimple` stay in the home branch exactly as before.

- [ ] **Step 5.6: Build**

Run:
```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -10
```
Expected: `[SUCCESS]`. If `buddy.h` does not already declare `buddyInvalidate`, you will see an unresolved reference — it does (see `src/buddy.h`), so no change expected.

- [ ] **Step 5.7: Commit**

```bash
git add src/main.cpp
git commit -m "main: DisplayMode toggle; LONG routes to clock_face

Add DISP_HOME / DISP_CLOCK enum. EVT_LONG flips the mode and
invalidates the incoming renderer so it repaints on next tick.
Render loop branches: clock_face_tick owns its own push; home
branch keeps the existing buddy/character + approval/HUD path.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Prompt-interrupt force-back-to-home

Per spec §3 and §1: any new prompt arriving while in `DISP_CLOCK` snaps the device back to home so the approval panel is never hidden.

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 6.1: Find the new-prompt detection block**

It looks like:
```cpp
  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId) - 1);
    lastPromptId[sizeof(lastPromptId) - 1] = 0;
    responseSent = false;
    approvalChoice = true;
    if (tama.promptId[0]) promptArrivedMs = now;
  }
```

- [ ] **Step 6.2: Add the mode-exit inside the `tama.promptId[0]` guard**

Change the inner `if` so only a non-empty prompt triggers the force-back:

```cpp
  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId) - 1);
    lastPromptId[sizeof(lastPromptId) - 1] = 0;
    responseSent = false;
    approvalChoice = true;
    if (tama.promptId[0]) {
      promptArrivedMs = now;
      if (displayMode == DISP_CLOCK) {
        displayMode = DISP_HOME;
        buddyInvalidate();   // force home first-frame repaint
      }
    }
  }
```

Only non-empty prompts cause the exit. A prompt clearing to empty (after approve/deny) must NOT drag the user back to home from anywhere — they already chose the decision from home.

- [ ] **Step 6.3: Build**

Run:
```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: `[SUCCESS]`.

- [ ] **Step 6.4: Commit**

```bash
git add src/main.cpp
git commit -m "main: clock mode exits to home on new prompt arrival

Any non-empty new promptId while in DISP_CLOCK snaps displayMode back
to DISP_HOME and invalidates the buddy renderer so the approval panel
shows immediately. Preserves Phase 1's approval-first priority.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: On-device acceptance

Subagents cannot flash hardware. These checks are the human's. Each is from spec §8 acceptance.

- [ ] **Step 7.1: Flash and open serial**

Run:
```bash
~/.platformio/penv/bin/pio run -t upload && ~/.platformio/penv/bin/pio device monitor
```
If `device monitor` fails with "Device not configured", unplug and replug USB then re-run just the monitor command.

- [ ] **Step 7.2: Invalid-time placeholder**

Boot fresh (don't pair yet). Press and hold the knob button for >600 ms to trigger LONG. Expected screen:
```
        --:--
      (no time)
     waiting for
        Claude
```
Serial should also print `LONG`.

- [ ] **Step 7.3: Valid-time display after BLE sync**

LONG again to exit clock (back to capybara). Open Claude Desktop → Developer → Open Hardware Buddy → Connect → pair. After connection, LONG again. Expected: real time appears (`HH:MM`, seconds, "Day Mon DD") within 1 s.

Watch for 30 seconds. Seconds should advance each second without flicker. When the minute rolls over, the `HH:MM` row updates cleanly.

- [ ] **Step 7.4: LONG toggles back to home**

From clock, LONG again. Expected: capybara re-renders immediately (no 200 ms delay — that's what the `buddyInvalidate()` is for).

- [ ] **Step 7.5: Prompt preemption**

LONG into clock. Trigger a Claude Desktop permission prompt (run a Claude session that needs `Bash` or `Write`). Expected:
- Within ~1 s of the prompt arriving, the device leaves clock and shows the approval panel on home.
- Approve via CLICK. Stays on home afterward (does NOT auto-return to clock).

- [ ] **Step 7.6: Rotation is a no-op in clock**

LONG into clock. Turn the knob CW and CCW. Expected: motor click fires, serial prints `CW` / `CCW`, but the clock face does not change (no persona state cycling, no screen artifact).

- [ ] **Step 7.7: No regression on home**

LONG back to home. Verify persona rotation still works (CW/CCW cycles the 7 states under the capybara). Verify the state label at y=205 updates correctly.

- [ ] **Step 7.8: If any check fails**

Do not merge. Note the failing check number, what happened, and what you expected. Report back; iterate on the relevant task.

- [ ] **Step 7.9: Push branch and prepare PR**

```bash
git push -u origin phase-2c
```
Then open a PR `phase-2c → main` on GitHub. Title: `Phase 2-C: clock mode`. Body can reference the spec doc commit.

---

## Risks and watchpoints

1. **Font size 6 rendering width** (spec §7). `"12:34"` at size 6 is ~150 px wide in GLCD. Visible in 240×240 circle but near the edge. If it looks clipped on flash, drop to size 5 (`sp.setTextSize(5)`) in `paintValid`. Native tests don't catch rendering problems.

2. **Minute/day cache sentinel** (`-1`) must be outside any valid `tm_min` / `tm_mday` range — `tm_min` is 0..59, `tm_mday` is 1..31, so `-1` is safe. If you change to another sentinel, update `clock_face_invalidate`'s reset values too.

3. **`localtime_r` thread safety**: `time()` + `localtime_r` is re-entrant. We only call it from the main loop (single thread). No mutex needed. If Phase 2-D introduces a dedicated rendering task, revisit.

4. **BLE prompt with empty `promptId`**: Claude Desktop clears the prompt by sending an empty string. Our force-back logic correctly guards on `tama.promptId[0]`, so transitions to empty don't bounce the user. Test 7.5 implicitly verifies this (resolving the prompt should not reopen clock).

5. **`buddyInvalidate()` on exit from clock**: this resets the buddy's internal dirty cache so it repaints on the next tick. If we forgot to invalidate, the home path's 5fps gate would hold the last frame from before clock mode — user sees the old capybara pose for up to 200 ms before the next animation tick. Spec §3 calls this out.

---

## When Task 7 passes

Tag `phase-2c` and merge the PR. Update `MEMORY.md`:
- Edit `/Users/zinklu/.claude/projects/-Users-zinklu-code-opensources-claude-desktop-buddy/memory/phase2_backlog.md`:
  - Move "Sub-project C" from active to completed.
  - Surface sub-project A (menus) as the natural next.
- Commit the memory change to the agent's memory folder (not the repo).
