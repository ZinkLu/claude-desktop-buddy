# Phase 2-B Info Pages + Pet Mode + HUD Scroll Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship three features unblocked by Phase 2-A's input FSM: 4-page Info display, interactive Pet mode with motor-driven purr / tickle-kick / squish / fall-asleep, and 3-line HUD history scrolling on home.

**Architecture:** Extend the existing `input_fsm` with 3 new `DisplayMode` values + routing for home-CLICK-cycle and HUD scroll. New `pet_gesture.{h,cpp}` classifier (pure, testable) detects stroking vs tickling from rotation streams. Extended `hw_motor` API adds continuous patterns (purr/vibrate/pulse-series) driven by a cooperative `hw_motor_tick(now_ms)` in the main loop — no FreeRTOS tasks. Render functions split into `info_pages.cpp` and `pet_pages.cpp`. Main.cpp integrates callbacks, pet gesture forwarding, motor tick, and multi-line HUD with scroll offset.

**Tech Stack:** Same as Phase 1 / 2-A / 2-C. Native tests via existing `[env:native]` + Unity.

**Spec:** `docs/superpowers/specs/2026-04-18-phase2b-info-pet-hud-design.md`
**Target branch:** cut `phase-2b` from `main` at the start (main has `cb8944a` = the spec commit).
**PIO:** `~/.platformio/penv/bin/pio`
**Working dir:** `/Users/zinklu/code/opensources/claude-desktop-buddy`

---

## File Structure

**New:**
- `src/pet_gesture.h` / `pet_gesture.cpp` — stroke/tickle classifier; pure C++, no hw deps
- `src/info_pages.h` / `info_pages.cpp` — 4 page renderers
- `src/pet_pages.h` / `pet_pages.cpp` — main pet page + stats page renderers
- `test/test_pet_gesture/test_pet_gesture.cpp` — Unity tests for classifier
- `test/test_hud_scroll/test_hud_scroll.cpp` — Unity tests for scroll offset arithmetic

**Modified:**
- `src/hw_motor.h` / `hw_motor.cpp` — add purr / kick / wiggle / pulse_series / vibrate / tick APIs
- `src/input_fsm.h` / `input_fsm.cpp` — extend `DisplayMode` with `DISP_PET`, `DISP_PET_STATS`, `DISP_INFO`; extend `FsmView` with `infoPage` + `hudScroll`; extend `FsmCallbacks` with 6 new fields; route home-CLICK-cycle, pet-rotation forwarding, info-page scroll, HUD scroll
- `test/test_menu_fsm/test_menu_fsm.cpp` — add test cases for new routing; existing 21 tests still pass
- `src/menu_panels.cpp` — update main menu `help` CLICK to enter Info page 0, `about` to enter Info page 3
- `src/main.cpp` — register new callbacks; port `wrapInto` helper from upstream; multi-line `drawHudSimple` with `hudScroll`; pet gesture handler; pet mode tick (purr safety, animation state selection); `hw_motor_tick` call each loop
- `platformio.ini` — extend env filters with new `.cpp` files

**Untouched:** `ble_bridge.*`, `hw_input.*`, `hw_display.*`, `hw_power.*`, `hw_leds.*`, `buddy.*`, `buddies/*.cpp`, `character.*`, `data.h`, `stats.h`, `xfer.h`, `clock_face.*`

---

## Pre-flight

- [ ] **Sync main + create branch**

```bash
cd /Users/zinklu/code/opensources/claude-desktop-buddy
git checkout main
git pull origin main
git log --oneline -1
# Expected: cb8944a Add Phase 2-B spec: info pages, interactive pet mode, HUD scroll
git checkout -b phase-2b
```

---

## Task 1: Extend `hw_motor` with continuous-mode APIs

Foundation for pet mode. No UI change, no user-visible effect yet.

**Files:**
- Modify: `src/hw_motor.h`
- Modify: `src/hw_motor.cpp`

- [ ] **Step 1.1: Extend `src/hw_motor.h`**

Open the file. Current content ends with the `hw_motor_click_default()` declaration. Append:

```cpp
// --- Continuous / queued patterns ----------------------------------------
// All continuous patterns are driven by hw_motor_tick() — main loop MUST
// call it every iteration or these won't animate.

// Start a low-frequency alternating-direction oscillation. Feels like a
// cat purring. level 0..4; level 0 is a silent no-op.
void hw_motor_purr_start(uint8_t level);
void hw_motor_purr_stop();
bool hw_motor_purr_active();

// One-shot directional kick. direction: 0 = forward vector, 1 = reverse.
// Stronger than click (used to "push back" on tickling).
void hw_motor_kick(uint8_t direction, uint8_t level);

// One-shot short L-R-L wiggle pattern (~180 ms total).
void hw_motor_wiggle();

// Queue N clicks spaced gap_ms apart. Returns immediately; pulses fired
// by hw_motor_tick. A second call before the first finishes replaces
// the queue.
void hw_motor_pulse_series(uint8_t n, uint16_t gap_ms, uint8_t level);

// Continuous vibration for duration_ms. level 0..4.
void hw_motor_vibrate(uint16_t duration_ms, uint8_t level);

// Drive purr / pulse_series / vibrate state machines. Call every main
// loop iteration. Cheap when all states are idle.
void hw_motor_tick(uint32_t now_ms);
```

- [ ] **Step 1.2: Extend `src/hw_motor.cpp`**

At the bottom of the file, after the existing `hw_motor_click_default` impl, append:

```cpp
// --- Continuous-mode state ------------------------------------------------

// Purr state: alternating-direction pulses at ~150 ms cadence.
static bool     _purr_active        = false;
static uint8_t  _purr_level         = 0;
static uint32_t _purr_next_at_ms    = 0;
static bool     _purr_next_direction= false;
static const uint32_t PURR_GAP_MS   = 150;

// Pulse-series queue state.
static uint8_t  _series_remaining   = 0;
static uint16_t _series_gap_ms      = 0;
static uint8_t  _series_level       = 0;
static uint32_t _series_next_at_ms  = 0;

// Vibrate state.
static bool     _vib_active         = false;
static uint32_t _vib_end_ms         = 0;
static uint8_t  _vib_level          = 0;
static uint32_t _vib_next_pulse_ms  = 0;
static const uint32_t VIB_GAP_MS    = 40;   // 25 Hz pulse cadence for continuous feel

static uint8_t level_to_strength(uint8_t level) {
  if (level == 0) return 0;
  if (level > 4)  level = 3;
  return HAPTIC_STRENGTH[level];
}

void hw_motor_purr_start(uint8_t level) {
  if (level == 0) return;
  if (level > 4) level = 3;
  _purr_active = true;
  _purr_level = level;
  _purr_next_at_ms = millis();   // fire first pulse immediately
  _purr_next_direction = false;
}

void hw_motor_purr_stop() {
  _purr_active = false;
}

bool hw_motor_purr_active() { return _purr_active; }

// Private: apply a directional 3-phase pulse. direction 0 uses the same
// vector as hw_motor_click (CH1 full, CH2 half, CH3 zero); direction 1
// rotates the vector (CH1 zero, CH2 half, CH3 full) to reverse torque.
static void motor_directional_pulse(uint8_t strength, uint8_t direction, uint8_t ms) {
  if (direction == 0) {
    ledcWrite(CH1, strength);
    ledcWrite(CH2, strength / 2);
    ledcWrite(CH3, 0);
  } else {
    ledcWrite(CH1, 0);
    ledcWrite(CH2, strength / 2);
    ledcWrite(CH3, strength);
  }
  delay(ms);
  hw_motor_off();
}

void hw_motor_kick(uint8_t direction, uint8_t level) {
  uint8_t s = level_to_strength(level);
  if (s == 0) return;
  motor_directional_pulse(s, direction, 40);   // sharper than a click
}

void hw_motor_wiggle() {
  uint8_t s = HAPTIC_STRENGTH[3];
  motor_directional_pulse(s, 0, 60);
  delay(20);
  motor_directional_pulse(s, 1, 60);
  delay(20);
  motor_directional_pulse(s, 0, 60);
}

void hw_motor_pulse_series(uint8_t n, uint16_t gap_ms, uint8_t level) {
  if (level == 0 || n == 0) return;
  if (level > 4) level = 3;
  _series_remaining = n;
  _series_gap_ms = gap_ms;
  _series_level = level;
  _series_next_at_ms = millis();   // fire first pulse on next tick
}

void hw_motor_vibrate(uint16_t duration_ms, uint8_t level) {
  if (level == 0 || duration_ms == 0) return;
  if (level > 4) level = 3;
  _vib_active = true;
  _vib_level = level;
  _vib_end_ms = millis() + duration_ms;
  _vib_next_pulse_ms = millis();
}

void hw_motor_tick(uint32_t now_ms) {
  // Purr
  if (_purr_active && now_ms >= _purr_next_at_ms) {
    uint8_t s = level_to_strength(_purr_level);
    motor_directional_pulse(s, _purr_next_direction ? 1 : 0, 20);
    _purr_next_direction = !_purr_next_direction;
    _purr_next_at_ms = now_ms + PURR_GAP_MS;
  }

  // Pulse series
  if (_series_remaining > 0 && now_ms >= _series_next_at_ms) {
    uint8_t s = level_to_strength(_series_level);
    motor_directional_pulse(s, 0, 25);
    _series_remaining--;
    _series_next_at_ms = now_ms + _series_gap_ms;
  }

  // Vibrate (alternating quick pulses until end_ms)
  if (_vib_active) {
    if (now_ms >= _vib_end_ms) {
      _vib_active = false;
    } else if (now_ms >= _vib_next_pulse_ms) {
      uint8_t s = level_to_strength(_vib_level);
      motor_directional_pulse(s, (now_ms & 0x20) ? 1 : 0, 10);
      _vib_next_pulse_ms = now_ms + VIB_GAP_MS;
    }
  }
}
```

Two details above rely on things elsewhere in the file:
- `HAPTIC_STRENGTH[]` — already declared above `hw_motor_click_default`
- `CH1` / `CH2` / `CH3`, `ledcWrite`, `hw_motor_off` — already in the file from Phase 1

- [ ] **Step 1.3: Build**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: `[SUCCESS]`. If `HAPTIC_STRENGTH` isn't visible to the new code, verify it's declared with file-scope visibility near the top of `hw_motor.cpp` (not inside a function).

- [ ] **Step 1.4: Commit**

```bash
git add src/hw_motor.h src/hw_motor.cpp
git commit -m "motor: add continuous-mode APIs (purr/kick/wiggle/series/vibrate)

Non-blocking cooperative patterns driven by hw_motor_tick(now_ms) in
the main loop — no FreeRTOS tasks. Each state machine is self-contained:
purr alternates direction at 150 ms, vibrate pulses at 25 Hz until
end time, pulse_series fires N clicks at a user-chosen gap. Kick and
wiggle are one-shot blocking patterns (short enough not to matter).

Foundation for Phase 2-B's interactive pet mode.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `pet_gesture` classifier with native tests (TDD)

Pure logic. No hw deps. Classifies rotation event streams into STROKE / TICKLE / NONE.

**Files:**
- Create: `src/pet_gesture.h`
- Create: `src/pet_gesture.cpp`
- Create: `test/test_pet_gesture/test_pet_gesture.cpp`
- Modify: `platformio.ini` (extend both env filters)

- [ ] **Step 2.1: Write `src/pet_gesture.h`**

```cpp
#pragma once
#include <stdint.h>
#include "hw_input.h"

enum PetGesture {
  PGEST_NONE = 0,
  PGEST_STROKE,
  PGEST_TICKLE,
};

// Feed each rotation event. Returns a gesture classification or NONE.
// A cooldown prevents double-firing of the same gesture within 300 ms.
// `e` must be EVT_ROT_CW, EVT_ROT_CCW, or anything else (which is ignored).
PetGesture pet_gesture_step(InputEvent e, uint32_t now_ms);

// Reset the rolling history buffer and cooldown. Call when entering pet mode.
void pet_gesture_reset();

namespace pet_gesture_internal {
  // Size of the rolling event buffer.
  constexpr uint8_t BUF_SZ = 8;

  // Reset; for use by unit tests.
  void reset_for_tests();
}
```

- [ ] **Step 2.2: Extend `[env:native]` filter**

Open `platformio.ini`. `[env:native]` current `build_src_filter = +<hw_input.cpp> +<clock_face.cpp> +<input_fsm.cpp>`. Change to:
```
build_src_filter = +<hw_input.cpp> +<clock_face.cpp> +<input_fsm.cpp> +<pet_gesture.cpp>
```

Also append `+<pet_gesture.cpp>` to `[env:x-knob]`'s filter at the end.

- [ ] **Step 2.3: Write the failing test file**

`test/test_pet_gesture/test_pet_gesture.cpp`:
```cpp
#include <unity.h>
#include "../../src/pet_gesture.h"
#include "../../src/hw_input.h"

void setUp() { pet_gesture_internal::reset_for_tests(); }
void tearDown() {}

// Helper to feed N events with spacing gap_ms.
static PetGesture feed(InputEvent events[], int n, uint32_t t0, uint32_t gap_ms) {
  PetGesture last = PGEST_NONE;
  for (int i = 0; i < n; i++) {
    PetGesture g = pet_gesture_step(events[i], t0 + i * gap_ms);
    if (g != PGEST_NONE) last = g;
  }
  return last;
}

void test_no_events_returns_none() {
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_NONE, 1000));
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_CLICK, 1100));
}

void test_single_rotation_returns_none() {
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_ROT_CW, 1000));
}

void test_three_alternations_returns_none() {
  // 3 events is below the stroke threshold (4+).
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_NONE, feed(seq, 3, 1000, 50));
}

void test_four_alternations_within_400ms_is_stroke() {
  // 4 events in 300 ms, directions alternate twice.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq, 4, 1000, 80));
}

void test_four_uniform_within_400ms_is_not_stroke() {
  // 4 events same direction, fast. Only 1 direction change = 0 alternations
  // — this is a borderline case but by our rule "2+ alternations" it's not stroke.
  // Also not tickle unless we hit 5 events within 250 ms.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_NONE, feed(seq, 4, 1000, 80));
}

void test_five_uniform_within_250ms_is_tickle() {
  // 5 same-direction events in 200 ms.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_TICKLE, feed(seq, 5, 1000, 40));
}

void test_five_spread_over_600ms_is_not_tickle() {
  // 5 same-direction events spread slowly — not tickle.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  // 5 events over 600 ms = gap 150 ms, which is outside 250 ms window.
  TEST_ASSERT_EQUAL(PGEST_NONE, feed(seq, 5, 1000, 150));
}

void test_cooldown_prevents_double_stroke() {
  // First stroke fires, then another set of alternations 100 ms later should not.
  InputEvent seq1[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq1, 4, 1000, 80));
  // Now feed more events within 300 ms cooldown — must return NONE despite pattern.
  InputEvent seq2[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_NONE, feed(seq2, 4, 1350, 40));
}

void test_cooldown_expires() {
  InputEvent seq1[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq1, 4, 1000, 80));
  // 500 ms after the stroke fired — well past the 300 ms cooldown.
  InputEvent seq2[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq2, 4, 1800, 80));
}

void test_non_rotation_events_ignored() {
  // Feeding CLICK between rotations should not break pattern detection.
  pet_gesture_step(EVT_ROT_CW,  1000);
  pet_gesture_step(EVT_CLICK,   1020);   // ignored
  pet_gesture_step(EVT_ROT_CCW, 1080);
  pet_gesture_step(EVT_ROT_CW,  1160);
  PetGesture g = pet_gesture_step(EVT_ROT_CCW, 1240);
  TEST_ASSERT_EQUAL(PGEST_STROKE, g);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_events_returns_none);
  RUN_TEST(test_single_rotation_returns_none);
  RUN_TEST(test_three_alternations_returns_none);
  RUN_TEST(test_four_alternations_within_400ms_is_stroke);
  RUN_TEST(test_four_uniform_within_400ms_is_not_stroke);
  RUN_TEST(test_five_uniform_within_250ms_is_tickle);
  RUN_TEST(test_five_spread_over_600ms_is_not_tickle);
  RUN_TEST(test_cooldown_prevents_double_stroke);
  RUN_TEST(test_cooldown_expires);
  RUN_TEST(test_non_rotation_events_ignored);
  return UNITY_END();
}
```

- [ ] **Step 2.4: Run — expect link failure**

```bash
~/.platformio/penv/bin/pio test -e native -f test_pet_gesture 2>&1 | tail -15
```
Expected: unresolved `pet_gesture_step`, `pet_gesture_reset`, `pet_gesture_internal::reset_for_tests`. `pet_gesture.cpp` doesn't exist yet.

- [ ] **Step 2.5: Commit failing tests**

```bash
git add test/test_pet_gesture/test_pet_gesture.cpp platformio.ini src/pet_gesture.h
git commit -m "pet_gesture: add header and failing unit tests

10 tests covering NONE / STROKE (4+ alternations in 400 ms with 2+
direction changes) / TICKLE (5+ uniform events in 250 ms) classification,
cooldown, non-rotation event isolation.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 2.6: Write `src/pet_gesture.cpp`**

```cpp
#include "pet_gesture.h"
#include <string.h>

namespace {

struct Event {
  uint32_t t;
  int8_t   dir;   // +1 CW, -1 CCW
};

static const uint32_t STROKE_WINDOW_MS   = 400;
static const uint8_t  STROKE_MIN_EVENTS  = 4;
static const uint8_t  STROKE_MIN_ALTS    = 2;
static const uint32_t TICKLE_WINDOW_MS   = 250;
static const uint8_t  TICKLE_MIN_EVENTS  = 5;
static const uint32_t COOLDOWN_MS        = 300;

Event    _buf[pet_gesture_internal::BUF_SZ];
uint8_t  _head = 0;
uint8_t  _count = 0;
uint32_t _cooldown_until = 0;

void push(uint32_t t, int8_t dir) {
  _buf[_head] = {t, dir};
  _head = (_head + 1) % pet_gesture_internal::BUF_SZ;
  if (_count < pet_gesture_internal::BUF_SZ) _count++;
}

// Read the i-th most recent event (0 = most recent).
const Event& recent(uint8_t i) {
  uint8_t idx = (_head + pet_gesture_internal::BUF_SZ - 1 - i) % pet_gesture_internal::BUF_SZ;
  return _buf[idx];
}

}  // namespace

void pet_gesture_reset() {
  _head = 0;
  _count = 0;
  _cooldown_until = 0;
  memset(_buf, 0, sizeof(_buf));
}

namespace pet_gesture_internal {
void reset_for_tests() { pet_gesture_reset(); }
}  // namespace pet_gesture_internal

PetGesture pet_gesture_step(InputEvent e, uint32_t now_ms) {
  if (e != EVT_ROT_CW && e != EVT_ROT_CCW) return PGEST_NONE;
  push(now_ms, (e == EVT_ROT_CW) ? 1 : -1);

  if (now_ms < _cooldown_until) return PGEST_NONE;

  // Try TICKLE: 5+ uniform events in 250 ms.
  if (_count >= TICKLE_MIN_EVENTS) {
    uint32_t oldest = recent(TICKLE_MIN_EVENTS - 1).t;
    if (now_ms - oldest <= TICKLE_WINDOW_MS) {
      int8_t dir0 = recent(0).dir;
      bool uniform = true;
      for (uint8_t i = 1; i < TICKLE_MIN_EVENTS; i++) {
        if (recent(i).dir != dir0) { uniform = false; break; }
      }
      if (uniform) {
        _cooldown_until = now_ms + COOLDOWN_MS;
        return PGEST_TICKLE;
      }
    }
  }

  // Try STROKE: 4+ events in 400 ms with 2+ alternations.
  if (_count >= STROKE_MIN_EVENTS) {
    uint32_t oldest = recent(STROKE_MIN_EVENTS - 1).t;
    if (now_ms - oldest <= STROKE_WINDOW_MS) {
      uint8_t alternations = 0;
      for (uint8_t i = 0; i + 1 < STROKE_MIN_EVENTS; i++) {
        if (recent(i).dir != recent(i + 1).dir) alternations++;
      }
      if (alternations >= STROKE_MIN_ALTS) {
        _cooldown_until = now_ms + COOLDOWN_MS;
        return PGEST_STROKE;
      }
    }
  }

  return PGEST_NONE;
}
```

- [ ] **Step 2.7: Run — expect all 10 green**

```bash
~/.platformio/penv/bin/pio test -e native -f test_pet_gesture 2>&1 | tail -15
```
Expected: 10 PASSED.

Common failure: `test_cooldown_prevents_double_stroke` fails because the cooldown is set only on classified gestures, not on subsequent events that happen to match. That's correct — cooldown starts at the gesture firing time. Check the test timing: first stroke fires at t=1240 (1000 + 3*80). Cooldown ends at 1540. Next feed starts at 1350 — within cooldown. PASS expected.

- [ ] **Step 2.8: Firmware build**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: `[SUCCESS]`.

- [ ] **Step 2.9: Commit**

```bash
git add src/pet_gesture.cpp
git commit -m "pet_gesture: implement classifier to pass all 10 tests

Ring buffer of 8 rotation events + cooldown. Tickle takes priority
over stroke when both patterns match the tail of history. Non-rotation
events are ignored by the step function so intermixed CLICK / LONG
don't corrupt pattern detection.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Extend `input_fsm` with new modes + routing

Add `DISP_PET`, `DISP_PET_STATS`, `DISP_INFO` to the enum; extend `FsmView` and `FsmCallbacks`; route home-CLICK-cycle, info-page-scroll, pet-rotation-forwarding, HUD scroll on home.

**Files:**
- Modify: `src/input_fsm.h`
- Modify: `src/input_fsm.cpp`
- Modify: `test/test_menu_fsm/test_menu_fsm.cpp` — add test cases

- [ ] **Step 3.1: Extend `src/input_fsm.h`**

At the top, find the `enum DisplayMode` block and add three values (after DISP_PASSKEY):

```cpp
enum DisplayMode {
  DISP_HOME = 0,
  DISP_CLOCK,
  DISP_MENU,
  DISP_SETTINGS,
  DISP_RESET,
  DISP_HELP,
  DISP_ABOUT,
  DISP_PASSKEY,
  DISP_PET,
  DISP_PET_STATS,
  DISP_INFO,
};
```

Find `struct FsmView` and add two new fields at the end:

```cpp
struct FsmView {
  DisplayMode mode;
  uint8_t menuSel;
  uint8_t settingsSel;
  uint8_t resetSel;
  uint8_t resetConfirmIdx;
  uint32_t resetConfirmUntil;
  uint8_t infoPage;      // 0..3
  uint8_t hudScroll;     // 0..30 (newest..oldest)
};
```

Find `struct FsmCallbacks` and add six new fields at the end:

```cpp
struct FsmCallbacks {
  void (*turn_off)();
  void (*delete_char)();
  void (*factory_reset)();
  void (*toggle_demo)();

  void (*brightness_changed)(uint8_t level);
  void (*haptic_changed)(uint8_t level);
  void (*transcript_changed)(bool on);

  void (*invalidate_clock)();
  void (*invalidate_buddy)();
  void (*invalidate_panel)();

  // NEW in Phase 2-B
  void (*on_enter_pet)();                      // greeting pulse, gesture reset
  void (*on_exit_pet)();                       // bye pulse, stop purr
  void (*on_pet_rotation)(bool cw);            // forwarded for gesture classifier
  void (*on_pet_long_press)();                 // squish vibration
  void (*on_info_page_change)(uint8_t page);   // repaint info page
  void (*on_hud_scroll_change)(uint8_t ofs);   // repaint home HUD strip
};
```

All new fields are optional / null-safe per the existing `CALL0` / `CALL1` macros in `input_fsm.cpp`.

- [ ] **Step 3.2: Add test cases in `test/test_menu_fsm/test_menu_fsm.cpp`**

At the top of the file, find the `MockCalls` struct and add counters for the new callbacks:

```cpp
struct MockCalls {
  int turn_off;
  int delete_char;
  int factory_reset;
  int toggle_demo;
  int brightness_changed;
  uint8_t last_brightness;
  int haptic_changed;
  uint8_t last_haptic;
  int transcript_changed;
  bool last_transcript;
  int invalidate_clock;
  int invalidate_buddy;
  int invalidate_panel;
  // NEW
  int on_enter_pet;
  int on_exit_pet;
  int on_pet_rotation;
  bool last_pet_rotation_cw;
  int on_pet_long_press;
  int on_info_page_change;
  uint8_t last_info_page;
  int on_hud_scroll_change;
  uint8_t last_hud_scroll;
};
```

Add mock fn stubs (after the existing mk_* functions):

```cpp
static void mk_on_enter_pet()                  { mock.on_enter_pet++; }
static void mk_on_exit_pet()                   { mock.on_exit_pet++; }
static void mk_on_pet_rotation(bool cw)        { mock.on_pet_rotation++; mock.last_pet_rotation_cw = cw; }
static void mk_on_pet_long_press()             { mock.on_pet_long_press++; }
static void mk_on_info_page_change(uint8_t p)  { mock.on_info_page_change++; mock.last_info_page = p; }
static void mk_on_hud_scroll_change(uint8_t o) { mock.on_hud_scroll_change++; mock.last_hud_scroll = o; }
```

Extend the `CB` initializer (add after existing 10 fields, in order):

```cpp
static const FsmCallbacks CB = {
  mk_turn_off, mk_delete_char, mk_factory_reset, mk_toggle_demo,
  mk_brightness, mk_haptic, mk_transcript,
  mk_invalidate_clock, mk_invalidate_buddy, mk_invalidate_panel,
  mk_on_enter_pet, mk_on_exit_pet, mk_on_pet_rotation,
  mk_on_pet_long_press, mk_on_info_page_change, mk_on_hud_scroll_change,
};
```

Append new test functions at the end of the file (before `int main()`):

```cpp
// --- Phase 2-B: home CLICK cycle + pet + info + HUD scroll -------------

void test_home_click_enters_pet() {
  input_fsm_dispatch(EVT_CLICK, 1000);
  TEST_ASSERT_EQUAL(DISP_PET, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.on_enter_pet);
}

void test_pet_click_enters_info_page0() {
  input_fsm_dispatch(EVT_CLICK, 1000);  // home -> pet
  input_fsm_dispatch(EVT_CLICK, 1100);  // pet -> info
  TEST_ASSERT_EQUAL(DISP_INFO, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().infoPage);
  TEST_ASSERT_EQUAL(1, mock.on_exit_pet);
}

void test_info_click_returns_home() {
  input_fsm_dispatch(EVT_CLICK, 1000);  // home -> pet
  input_fsm_dispatch(EVT_CLICK, 1100);  // pet -> info
  input_fsm_dispatch(EVT_CLICK, 1200);  // info -> home
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_info_rotation_pages() {
  input_fsm_dispatch(EVT_CLICK, 1000);  // home -> pet
  input_fsm_dispatch(EVT_CLICK, 1100);  // pet -> info (page 0)
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  TEST_ASSERT_EQUAL(1, input_fsm_view().infoPage);
  TEST_ASSERT_EQUAL(1, mock.on_info_page_change);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CW, 1400);
  input_fsm_dispatch(EVT_ROT_CW, 1500);   // wraps 3 -> 0
  TEST_ASSERT_EQUAL(0, input_fsm_view().infoPage);
  input_fsm_dispatch(EVT_ROT_CCW, 1600);  // 0 -> 3 (reverse wrap)
  TEST_ASSERT_EQUAL(3, input_fsm_view().infoPage);
}

void test_info_long_returns_home() {
  input_fsm_dispatch(EVT_CLICK, 1000);  // -> pet
  input_fsm_dispatch(EVT_CLICK, 1100);  // -> info
  input_fsm_dispatch(EVT_LONG, 1200);
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_pet_rotation_forwards_callback() {
  input_fsm_dispatch(EVT_CLICK, 1000);     // -> pet
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  TEST_ASSERT_EQUAL(1, mock.on_pet_rotation);
  TEST_ASSERT_TRUE(mock.last_pet_rotation_cw);
  input_fsm_dispatch(EVT_ROT_CCW, 1200);
  TEST_ASSERT_EQUAL(2, mock.on_pet_rotation);
  TEST_ASSERT_FALSE(mock.last_pet_rotation_cw);
}

void test_pet_double_enters_stats() {
  input_fsm_dispatch(EVT_CLICK, 1000);   // -> pet
  input_fsm_dispatch(EVT_DOUBLE, 1100);
  TEST_ASSERT_EQUAL(DISP_PET_STATS, input_fsm_view().mode);
}

void test_pet_stats_click_returns_to_pet() {
  input_fsm_dispatch(EVT_CLICK, 1000);   // -> pet
  input_fsm_dispatch(EVT_DOUBLE, 1100);  // -> stats
  input_fsm_dispatch(EVT_CLICK, 1200);
  TEST_ASSERT_EQUAL(DISP_PET, input_fsm_view().mode);
}

void test_pet_long_press_triggers_squish_callback() {
  input_fsm_dispatch(EVT_CLICK, 1000);  // -> pet
  // LONG from pet normally means exit, but spec §5 + §3 table: Pet-mode
  // LONG fires on_pet_long_press AND returns to home. So both effects.
  input_fsm_dispatch(EVT_LONG, 1100);
  TEST_ASSERT_EQUAL(1, mock.on_pet_long_press);
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.on_exit_pet);
}

void test_home_rotation_scrolls_hud() {
  input_fsm_dispatch(EVT_ROT_CW, 1000);
  TEST_ASSERT_EQUAL(1, input_fsm_view().hudScroll);
  TEST_ASSERT_EQUAL(1, mock.on_hud_scroll_change);
  input_fsm_dispatch(EVT_ROT_CCW, 1100);
  TEST_ASSERT_EQUAL(0, input_fsm_view().hudScroll);
  // CCW below 0 clamps at 0.
  input_fsm_dispatch(EVT_ROT_CCW, 1200);
  TEST_ASSERT_EQUAL(0, input_fsm_view().hudScroll);
}

void test_home_hud_scroll_clamps_at_30() {
  for (int i = 0; i < 35; i++) input_fsm_dispatch(EVT_ROT_CW, 1000 + i * 10);
  TEST_ASSERT_EQUAL(30, input_fsm_view().hudScroll);
}

void test_menu_help_opens_info_page_0() {
  input_fsm_dispatch(EVT_LONG, 1000);  // home -> menu
  // 'help' is item 3; scroll to it
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  TEST_ASSERT_EQUAL(3, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 1400);
  TEST_ASSERT_EQUAL(DISP_INFO, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().infoPage);
}

void test_menu_about_opens_info_page_3() {
  input_fsm_dispatch(EVT_LONG, 1000);  // home -> menu
  // 'about' is item 4
  for (int i = 0; i < 4; i++) input_fsm_dispatch(EVT_ROT_CW, 1100 + i*100);
  TEST_ASSERT_EQUAL(4, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 1500);
  TEST_ASSERT_EQUAL(DISP_INFO, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(3, input_fsm_view().infoPage);
}

void test_prompt_force_home_stops_pet() {
  input_fsm_dispatch(EVT_CLICK, 1000);   // home -> pet
  input_fsm_force_home_on_prompt();
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.on_exit_pet);
}
```

Register the new tests in `main()` (inside `UNITY_BEGIN()/END()`):

```cpp
  RUN_TEST(test_home_click_enters_pet);
  RUN_TEST(test_pet_click_enters_info_page0);
  RUN_TEST(test_info_click_returns_home);
  RUN_TEST(test_info_rotation_pages);
  RUN_TEST(test_info_long_returns_home);
  RUN_TEST(test_pet_rotation_forwards_callback);
  RUN_TEST(test_pet_double_enters_stats);
  RUN_TEST(test_pet_stats_click_returns_to_pet);
  RUN_TEST(test_pet_long_press_triggers_squish_callback);
  RUN_TEST(test_home_rotation_scrolls_hud);
  RUN_TEST(test_home_hud_scroll_clamps_at_30);
  RUN_TEST(test_menu_help_opens_info_page_0);
  RUN_TEST(test_menu_about_opens_info_page_3);
  RUN_TEST(test_prompt_force_home_stops_pet);
```

- [ ] **Step 3.3: Run — expect failures**

```bash
~/.platformio/penv/bin/pio test -e native -f test_menu_fsm 2>&1 | tail -25
```
Expected: 21 existing tests still pass, new 14 tests FAIL (because `input_fsm.cpp` doesn't handle the new logic yet). Total: 35 tests, ~21 passed / 14 failed.

If existing tests fail: you broke something in the struct layout. The `CB` initializer must use aggregate initialization matching the struct field order. Double-check field order in `input_fsm.h`.

- [ ] **Step 3.4: Update `src/input_fsm.cpp` to pass new tests**

Open `src/input_fsm.cpp`. Several changes to make:

**A. Extend `_reset_state()`** to initialize the new view fields:

Find `_reset_state()` and add:
```cpp
  _v.infoPage = 0;
  _v.hudScroll = 0;
```

**B. Add constants** near the top:
```cpp
static const uint8_t INFO_N        = 4;
static const uint8_t HUD_MAX_SCROLL = 30;
```

**C. Modify `_menu_click`** cases 3 (help) and 4 (about) to enter DISP_INFO:

```cpp
static void _menu_click(uint8_t idx) {
  switch (idx) {
    case 0: _enter(DISP_SETTINGS); _v.settingsSel = 0; _clear_reset_arm(); CALL0(invalidate_panel); break;
    case 1: _enter(DISP_CLOCK); CALL0(invalidate_clock); break;
    case 2: CALL0(turn_off); break;
    case 3: _enter(DISP_INFO); _v.infoPage = 0; CALL0(invalidate_panel); CALL1(on_info_page_change, 0); break;  // help -> info 0
    case 4: _enter(DISP_INFO); _v.infoPage = 3; CALL0(invalidate_panel); CALL1(on_info_page_change, 3); break;  // about -> info 3
    case 5: CALL0(toggle_demo); CALL0(invalidate_panel); break;
    case 6: _go_home(); break;
    default: break;
  }
}
```

**D. Extend `input_fsm_dispatch`** to handle the new modes.

After the `if (_passkeyActive) return;` guard, BEFORE the existing `EVT_LONG` branch, add:

```cpp
  // --- Home CLICK cycle / HUD scroll (Phase 2-B) --------------------------
  if (_v.mode == DISP_HOME) {
    if (e == EVT_CLICK) {
      // Only enter pet if no prompt is active. (Prompt handling is in
      // main.cpp, which checks inPrompt before calling dispatch for CLICK.
      // Here we assume the caller has already decided to route the click
      // to the FSM — i.e., not in prompt.)
      _enter(DISP_PET);
      CALL0(on_enter_pet);
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_ROT_CW) {
      if (_v.hudScroll < HUD_MAX_SCROLL) _v.hudScroll++;
      CALL1(on_hud_scroll_change, _v.hudScroll);
      return;
    }
    if (e == EVT_ROT_CCW) {
      if (_v.hudScroll > 0) _v.hudScroll--;
      CALL1(on_hud_scroll_change, _v.hudScroll);
      return;
    }
    // EVT_LONG falls through to existing "home -> menu" handler below.
  }

  // --- Pet mode ------------------------------------------------------------
  if (_v.mode == DISP_PET) {
    if (e == EVT_ROT_CW || e == EVT_ROT_CCW) {
      CALL1(on_pet_rotation, (e == EVT_ROT_CW));
      return;
    }
    if (e == EVT_CLICK) {
      CALL0(on_exit_pet);
      _enter(DISP_INFO); _v.infoPage = 0;
      CALL0(invalidate_panel);
      CALL1(on_info_page_change, 0);
      return;
    }
    if (e == EVT_DOUBLE) {
      _enter(DISP_PET_STATS);
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_LONG) {
      CALL0(on_pet_long_press);
      CALL0(on_exit_pet);
      _go_home();
      return;
    }
  }

  // --- Pet stats sub-page --------------------------------------------------
  if (_v.mode == DISP_PET_STATS) {
    if (e == EVT_CLICK || e == EVT_DOUBLE) {
      _enter(DISP_PET);
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_LONG) {
      CALL0(on_exit_pet);
      _go_home();
      return;
    }
    // Rotation is a no-op.
    return;
  }

  // --- Info mode -----------------------------------------------------------
  if (_v.mode == DISP_INFO) {
    if (e == EVT_ROT_CW) {
      _v.infoPage = (_v.infoPage + 1) % INFO_N;
      CALL1(on_info_page_change, _v.infoPage);
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_ROT_CCW) {
      _v.infoPage = (_v.infoPage + INFO_N - 1) % INFO_N;
      CALL1(on_info_page_change, _v.infoPage);
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_CLICK || e == EVT_LONG) {
      _go_home();
      return;
    }
    return;
  }
```

**E. Update `_go_home`** to fire `on_exit_pet` if leaving pet-ish mode:

```cpp
static void _go_home() {
  if (_v.mode == DISP_PET || _v.mode == DISP_PET_STATS) {
    CALL0(on_exit_pet);
  }
  _v.mode = DISP_HOME;
  _clear_reset_arm();
  _v.hudScroll = 0;   // reset HUD scroll on arrival home
  CALL0(invalidate_buddy);
}
```

Note: resetting `hudScroll = 0` on home entry is a design choice not in the spec. Spec §6.4 says "new transcript line + hudScroll==0 keep at 0, else leave alone" — but that's about in-home rotation, not mode entry. For clean UX, snapping to newest when returning to home makes sense. If the user prefers otherwise, we can revisit.

Actually, re-reading spec §6: the spec implies hudScroll persists. For safety, do NOT reset on home entry here. Remove the `_v.hudScroll = 0;` line above.

Corrected `_go_home`:
```cpp
static void _go_home() {
  if (_v.mode == DISP_PET || _v.mode == DISP_PET_STATS) {
    CALL0(on_exit_pet);
  }
  _v.mode = DISP_HOME;
  _clear_reset_arm();
  CALL0(invalidate_buddy);
}
```

**F. Update `input_fsm_force_home_on_prompt`** to fire `on_exit_pet`:

It already calls `_go_home()` which now handles the pet exit. No change needed beyond §E.

- [ ] **Step 3.5: Run — expect all 35 tests green**

```bash
~/.platformio/penv/bin/pio test -e native -f test_menu_fsm 2>&1 | tail -20
```
Expected: 35 tests PASSED.

Common gotchas:
- `test_home_click_enters_pet` fails: existing LONG handler's `case DISP_HOME: _enter(DISP_MENU);` catches EVT_LONG. But EVT_CLICK is a separate event — our new home-CLICK branch should fire first. Verify the order: home-CLICK-cycle block comes BEFORE the existing LONG branch.
- `test_pet_long_press_triggers_squish_callback` fails: verify the pet LONG handler fires `on_pet_long_press` BEFORE `on_exit_pet` and `_go_home`.

- [ ] **Step 3.6: Firmware build**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: `[SUCCESS]`. The new callbacks in `FsmCallbacks` require existing `main.cpp` initializer to provide them — but `main.cpp` initializes with aggregate initialization and the new fields will default-initialize to nullptr. The FSM's `CALL0`/`CALL1` macros null-check. Main.cpp will be updated in Task 6 to register real callbacks.

- [ ] **Step 3.7: Commit**

```bash
git add src/input_fsm.h src/input_fsm.cpp test/test_menu_fsm/test_menu_fsm.cpp
git commit -m "input_fsm: add pet / pet_stats / info modes + home CLICK cycle

DisplayMode grows by 3 values. FsmView gains infoPage and hudScroll.
FsmCallbacks gains 6 new callbacks for pet interactions (enter/exit/
rotation/long-press) and info page changes plus HUD scroll changes.

Routing: home CLICK enters pet, pet CLICK enters info page 0,
pet DOUBLE enters stats sub-page, info rotation pages, pet rotation
forwards to callback for gesture classification. LONG from any pet/info
mode returns home. Menu help/about shortcuts enter info at pages 0/3.

35 native FSM tests now pass (21 prior + 14 new).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `info_pages` renderer

**Files:**
- Create: `src/info_pages.h`
- Create: `src/info_pages.cpp`
- Modify: `platformio.ini`

- [ ] **Step 4.1: Write `src/info_pages.h`**

```cpp
#pragma once
#include <stdint.h>

// Renders the given info page to hw_display_sprite() and pushes.
// Called from main.cpp when displayMode == DISP_INFO each frame (page
// content for CLAUDE and SYSTEM updates live; others are static but we
// just redraw unconditionally — cheap at 50 fps vs the sprite push cost).
void draw_info_page(uint8_t page);
```

- [ ] **Step 4.2: Write `src/info_pages.cpp`**

```cpp
#include "info_pages.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "esp_mac.h"
#include "hw_display.h"
#include "ble_bridge.h"

// Live-state bridges owned by main.cpp (same pattern as menu_panels).
extern uint8_t panel_brightness();
extern uint8_t panel_haptic();
extern const char* info_bt_name();
extern uint8_t info_sessions_total();
extern uint8_t info_sessions_running();
extern uint8_t info_sessions_waiting();
extern uint32_t info_last_msg_age_s();
extern const char* info_claude_state_name();
extern const char* info_bt_status();   // "linked" / "discover" / "off"
extern const char* info_scenario_name();

static const uint16_t BG       = TFT_BLACK;
static const uint16_t TEXT     = TFT_WHITE;
static const uint16_t TEXT_DIM = TFT_DARKGREY;

static void panel_title(const char* t) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);
  sp.setTextDatum(TL_DATUM);
  sp.setTextSize(2);
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 28);
  sp.print(t);
  sp.drawFastHLine(40, 50, 160, TEXT_DIM);
  sp.setTextSize(1);
}

static void page_footer(uint8_t page) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 208);
  char buf[12];
  snprintf(buf, sizeof(buf), "page %u/4", (unsigned)(page + 1));
  sp.print(buf);
}

static void draw_about() {
  panel_title("ABOUT");
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 60);  sp.print("claude-desktop-buddy");
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 72);  sp.print("X-Knob port");
  sp.setCursor(40, 88);  sp.print("Watches Claude Desktop");
  sp.setCursor(40, 100); sp.print("sessions. Sleeps idle,");
  sp.setCursor(40, 112); sp.print("wakes on work, shows");
  sp.setCursor(40, 124); sp.print("approvals.");
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 144); sp.print("Controls");
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 156); sp.print("turn  scroll");
  sp.setCursor(40, 168); sp.print("click select");
  sp.setCursor(40, 180); sp.print("long  menu / back");
  page_footer(0);
}

static void draw_claude() {
  panel_title("CLAUDE");
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 60);  sp.print("sessions");
  sp.setTextColor(TEXT_DIM, BG);
  char buf[24];
  snprintf(buf, sizeof(buf), "total   %u", (unsigned)info_sessions_total());
  sp.setCursor(52, 72);  sp.print(buf);
  snprintf(buf, sizeof(buf), "running %u", (unsigned)info_sessions_running());
  sp.setCursor(52, 84);  sp.print(buf);
  snprintf(buf, sizeof(buf), "waiting %u", (unsigned)info_sessions_waiting());
  sp.setCursor(52, 96);  sp.print(buf);

  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 116); sp.print("link");
  sp.setTextColor(TEXT_DIM, BG);
  snprintf(buf, sizeof(buf), "via      %s", info_scenario_name());
  sp.setCursor(52, 128); sp.print(buf);
  snprintf(buf, sizeof(buf), "ble      %s", bleConnected() ? (bleSecure() ? "encrypted" : "open") : "-");
  sp.setCursor(52, 140); sp.print(buf);
  snprintf(buf, sizeof(buf), "last msg %lus", (unsigned long)info_last_msg_age_s());
  sp.setCursor(52, 152); sp.print(buf);
  snprintf(buf, sizeof(buf), "state    %s", info_claude_state_name());
  sp.setCursor(52, 164); sp.print(buf);

  page_footer(1);
}

static void draw_system() {
  panel_title("SYSTEM");
  TFT_eSprite& sp = hw_display_sprite();

  char buf[32];
  uint32_t up = millis() / 1000;
  snprintf(buf, sizeof(buf), "uptime %luh %02lum", up / 3600, (up / 60) % 60);
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 60); sp.print(buf);

  snprintf(buf, sizeof(buf), "heap   %uKB", (unsigned)(ESP.getFreeHeap() / 1024));
  sp.setCursor(40, 72); sp.print(buf);

  snprintf(buf, sizeof(buf), "bright %u/4", (unsigned)panel_brightness());
  sp.setCursor(40, 84); sp.print(buf);

  snprintf(buf, sizeof(buf), "haptic %u/4", (unsigned)panel_haptic());
  sp.setCursor(40, 96); sp.print(buf);

  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 116); sp.print("bluetooth");
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(52, 128); sp.print(info_bt_name());

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  sp.setCursor(52, 140); sp.print(buf);

  sp.setCursor(52, 152); sp.print(info_bt_status());

  page_footer(2);
}

static void draw_credits() {
  panel_title("CREDITS");
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 60);  sp.print("made by");
  sp.setTextColor(TEXT, BG);
  sp.setCursor(52, 72);  sp.print("Felix Rieseberg");
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 96);  sp.print("source");
  sp.setCursor(52, 108); sp.print("github.com/anthropics");
  sp.setCursor(52, 120); sp.print("  /claude-desktop-buddy");
  sp.setCursor(40, 140); sp.print("X-Knob port");
  sp.setCursor(52, 152); sp.print("github.com/ZinkLu");
  sp.setCursor(52, 164); sp.print("  /claude-desktop-buddy");
  sp.setCursor(40, 184); sp.print("hardware: X-Knob ESP32-S3");
  page_footer(3);
}

void draw_info_page(uint8_t page) {
  switch (page) {
    case 0: draw_about();   break;
    case 1: draw_claude();  break;
    case 2: draw_system();  break;
    case 3: draw_credits(); break;
    default: draw_about();  break;
  }
  hw_display_sprite().pushSprite(0, 0);
}
```

- [ ] **Step 4.3: Extend `platformio.ini` firmware env**

Append `+<info_pages.cpp>` to `[env:x-knob]`'s `build_src_filter`. Not added to `[env:native]` (depends on hw_display).

- [ ] **Step 4.4: Build**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -12
```
Expected: `[SUCCESS]`. If any `info_*` extern fn fails to link: those will be defined in Task 6 (main.cpp). For this Task, we only need the firmware to compile each `.cpp` individually; linking against main.cpp's unprovided externs WILL fail. Move the link step to Task 6.

Actually — the linker will fail. To unblock this task, write temporary stub functions at the bottom of `info_pages.cpp` guarded by `#ifdef PHASE_2B_STUB_BRIDGES`. Don't define that flag — let the linker error fail here, then Task 6 provides the real impls.

Alternative: skip the build check in this task and let Task 6 test the link. Mark this task's "build" as:
```bash
# compile-only check — linking happens once main.cpp provides the bridges
~/.platformio/penv/bin/pio run -t checkprogsize 2>&1 | tail -8
```
which will fail with missing symbols but confirms compilation.

Easier: just compile info_pages.cpp standalone:
```bash
~/.platformio/penv/bin/pio ci --project-conf=platformio.ini src/info_pages.cpp -l "hw_display.h in ..." 
```

Actually simplest: DEFINE TEMPORARY STUBS at the bottom of info_pages.cpp BEFORE the closing `#endif`-free region. They'll be removed in Task 6.

Add at the BOTTOM of `info_pages.cpp`:
```cpp
// Temporary stubs so Task 4 builds standalone. Task 6's main.cpp provides
// the real implementations; remove these stubs there.
__attribute__((weak)) const char* info_bt_name()          { return "Claude-????"; }
__attribute__((weak)) uint8_t     info_sessions_total()   { return 0; }
__attribute__((weak)) uint8_t     info_sessions_running() { return 0; }
__attribute__((weak)) uint8_t     info_sessions_waiting() { return 0; }
__attribute__((weak)) uint32_t    info_last_msg_age_s()   { return 0; }
__attribute__((weak)) const char* info_claude_state_name(){ return "idle"; }
__attribute__((weak)) const char* info_bt_status()        { return "off"; }
__attribute__((weak)) const char* info_scenario_name()    { return "none"; }
```

`__attribute__((weak))` means main.cpp's strong definitions override these. Build succeeds standalone AND with main's implementations.

`panel_brightness` / `panel_haptic` are already defined in main.cpp from Phase 2-A — no weak stub needed.

- [ ] **Step 4.5: Build again with weak stubs**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -8
```
Expected: `[SUCCESS]`.

- [ ] **Step 4.6: Commit**

```bash
git add src/info_pages.h src/info_pages.cpp platformio.ini
git commit -m "info_pages: add 4-page info renderer

ABOUT (page 0): intro + controls summary. CLAUDE (1): live session counts
+ BLE link + last message age + current state. SYSTEM (2): uptime, heap,
brightness, haptic, BT name, MAC, link status. CREDITS (3): authors,
source URLs.

Live-state bridges declared as weak extern so the file builds standalone;
Task 6's main.cpp provides real implementations that override.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `pet_pages` renderer

**Files:**
- Create: `src/pet_pages.h`
- Create: `src/pet_pages.cpp`
- Modify: `platformio.ini`

- [ ] **Step 5.1: Write `src/pet_pages.h`**

```cpp
#pragma once
#include <stdint.h>

// Main pet page: big character + hint + compact stats footer.
// `showHint` is true for the first 3 s after entry (fades after).
// Persona state is passed in so main can override (heart during stroke,
// dizzy during tickle, sleep on fall-asleep, etc.).
void draw_pet_main(uint8_t personaState, bool showHint);

// Full stats readout sub-page (mood, fed, energy, level, counters).
void draw_pet_stats();
```

- [ ] **Step 5.2: Write `src/pet_pages.cpp`**

```cpp
#include "pet_pages.h"
#include <Arduino.h>
#include "hw_display.h"
#include "buddy.h"

// Bridges to main.cpp for stats.
extern uint8_t     pet_mood_tier();      // 0..4
extern uint8_t     pet_fed_progress();   // 0..10
extern uint8_t     pet_energy_tier();    // 0..5
extern uint8_t     pet_level();
extern uint16_t    pet_approvals();
extern uint16_t    pet_denials();
extern uint32_t    pet_nap_seconds();
extern uint32_t    pet_tokens_total();
extern uint32_t    pet_tokens_today();

static const uint16_t BG       = TFT_BLACK;
static const uint16_t TEXT     = TFT_WHITE;
static const uint16_t TEXT_DIM = TFT_DARKGREY;
static const uint16_t HEART    = 0xF810;
static const uint16_t HOT      = 0xFA20;

static void tiny_heart(int x, int y, bool filled, uint16_t col) {
  TFT_eSprite& sp = hw_display_sprite();
  if (filled) {
    sp.fillCircle(x - 2, y, 2, col);
    sp.fillCircle(x + 2, y, 2, col);
    sp.fillTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 5, col);
  } else {
    sp.drawCircle(x - 2, y, 2, col);
    sp.drawCircle(x + 2, y, 2, col);
    sp.drawLine(x - 4, y + 1, x, y + 5, col);
    sp.drawLine(x + 4, y + 1, x, y + 5, col);
  }
}

void draw_pet_main(uint8_t personaState, bool showHint) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);

  // Character — reuse buddy renderer. Scale stays at peek=false (2x home size).
  // The buddy tick call must be driven by main.cpp; we only draw the sprite
  // state after the tick has run. But since draw_pet_main is called AFTER
  // buddyTick in the main loop, the sprite already has the character painted
  // for DISP_HOME's typical y=BUDDY_Y_BASE=55 area. That works for pet too.

  buddyTick(personaState);

  // Hint at top (fades out via showHint from main).
  if (showHint) {
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TEXT_DIM, BG);
    sp.setTextSize(1);
    sp.drawString("pet me", 120, 30);
    sp.setTextDatum(TL_DATUM);
  }

  // Compact stats footer: mood hearts + level pill.
  uint8_t mood = pet_mood_tier();
  uint16_t moodCol = (mood >= 3) ? HEART : (mood >= 2) ? HOT : TEXT_DIM;
  sp.setTextColor(TEXT_DIM, BG);
  sp.setTextSize(1);
  sp.setCursor(40, 200); sp.print("mood");
  for (int i = 0; i < 4; i++) tiny_heart(80 + i * 12, 204, i < mood, moodCol);

  char buf[12];
  snprintf(buf, sizeof(buf), "lv %u", (unsigned)pet_level());
  sp.setCursor(160, 200); sp.print(buf);

  sp.pushSprite(0, 0);
}

void draw_pet_stats() {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);
  sp.setTextDatum(TL_DATUM);
  sp.setTextSize(1);

  int y = 40;
  // mood
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, y); sp.print("mood");
  uint8_t mood = pet_mood_tier();
  uint16_t moodCol = (mood >= 3) ? HEART : (mood >= 2) ? HOT : TEXT_DIM;
  for (int i = 0; i < 4; i++) tiny_heart(90 + i * 14, y + 4, i < mood, moodCol);

  y += 16;
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, y); sp.print("fed");
  uint8_t fed = pet_fed_progress();
  for (int i = 0; i < 10; i++) {
    int px = 80 + i * 10;
    if (i < fed) sp.fillCircle(px, y + 4, 2, TEXT);
    else         sp.drawCircle(px, y + 4, 2, TEXT_DIM);
  }

  y += 16;
  sp.setCursor(40, y); sp.print("energy");
  uint8_t en = pet_energy_tier();
  uint16_t enCol = (en >= 4) ? 0x07FF : (en >= 2) ? 0xFFE0 : HOT;
  for (int i = 0; i < 5; i++) {
    int px = 100 + i * 14;
    if (i < en) sp.fillRect(px, y, 9, 8, enCol);
    else        sp.drawRect(px, y, 9, 8, TEXT_DIM);
  }

  y += 22;
  char buf[24];
  sp.fillRoundRect(40, y, 52, 16, 3, HEART);
  sp.setTextColor(BG, HEART);
  sp.setCursor(48, y + 4);
  snprintf(buf, sizeof(buf), "Lv %u", (unsigned)pet_level());
  sp.print(buf);

  y += 24;
  sp.setTextColor(TEXT_DIM, BG);
  snprintf(buf, sizeof(buf), "approved  %u", (unsigned)pet_approvals());
  sp.setCursor(40, y); sp.print(buf); y += 12;
  snprintf(buf, sizeof(buf), "denied    %u", (unsigned)pet_denials());
  sp.setCursor(40, y); sp.print(buf); y += 12;
  uint32_t nap = pet_nap_seconds();
  snprintf(buf, sizeof(buf), "napped    %luh%02lum", nap / 3600, (nap / 60) % 60);
  sp.setCursor(40, y); sp.print(buf); y += 12;

  // Token formatting: big numbers -> K / M.
  auto tok_fmt = [&](uint32_t v, char* out, size_t n) {
    if      (v >= 1000000) snprintf(out, n, "%lu.%luM", v / 1000000, (v / 100000) % 10);
    else if (v >= 1000)    snprintf(out, n, "%lu.%luK", v / 1000, (v / 100) % 10);
    else                   snprintf(out, n, "%lu", (unsigned long)v);
  };
  char tok[12];
  tok_fmt(pet_tokens_total(), tok, sizeof(tok));
  snprintf(buf, sizeof(buf), "tokens    %s", tok);
  sp.setCursor(40, y); sp.print(buf); y += 12;
  tok_fmt(pet_tokens_today(), tok, sizeof(tok));
  snprintf(buf, sizeof(buf), "today     %s", tok);
  sp.setCursor(40, y); sp.print(buf);

  sp.pushSprite(0, 0);
}
```

Add weak stubs at the bottom (remove in Task 6):
```cpp
__attribute__((weak)) uint8_t  pet_mood_tier()     { return 2; }
__attribute__((weak)) uint8_t  pet_fed_progress()  { return 0; }
__attribute__((weak)) uint8_t  pet_energy_tier()   { return 3; }
__attribute__((weak)) uint8_t  pet_level()         { return 0; }
__attribute__((weak)) uint16_t pet_approvals()     { return 0; }
__attribute__((weak)) uint16_t pet_denials()       { return 0; }
__attribute__((weak)) uint32_t pet_nap_seconds()   { return 0; }
__attribute__((weak)) uint32_t pet_tokens_total()  { return 0; }
__attribute__((weak)) uint32_t pet_tokens_today()  { return 0; }
```

- [ ] **Step 5.3: Extend `platformio.ini`**

Append `+<pet_pages.cpp>` to `[env:x-knob]`'s filter.

- [ ] **Step 5.4: Build**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -6
```
Expected: `[SUCCESS]`.

- [ ] **Step 5.5: Commit**

```bash
git add src/pet_pages.h src/pet_pages.cpp platformio.ini
git commit -m "pet_pages: main pet view + stats sub-page renderers

Main: character via buddyTick at existing peek-false scale, optional
'pet me' hint fades via showHint arg from caller, compact mood+level
footer. Stats: full upstream-style readout with mood hearts, fed circles
10 pips, energy bars 5, level pill, approvals/denials/napped counters,
tokens total + today with K/M formatting.

Live-state bridges declared as weak extern so the file builds standalone;
Task 6's main.cpp provides real implementations.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Main.cpp integration

The big one. Registers all new callbacks, provides the extern bridge implementations, ports `wrapInto` for multi-line HUD, drives pet gesture + motor tick, selects persona states for pet-mode animations.

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 6.1: Read current main.cpp, plan edits**

```bash
wc -l src/main.cpp
```
Expect ~330 lines. Most of Task 6 is additions, not restructures.

- [ ] **Step 6.2: Update includes**

After existing `#include "menu_panels.h"` add:
```cpp
#include "info_pages.h"
#include "pet_pages.h"
#include "pet_gesture.h"
```

- [ ] **Step 6.3: Add extern bridge implementations**

Find the existing bridge block in `main.cpp` (the `panel_brightness`, `panel_haptic`, etc.). Right after it, add:

```cpp
// Info page bridges
const char* info_bt_name()              { return btName; }
uint8_t     info_sessions_total()       { return tama.sessionsTotal; }
uint8_t     info_sessions_running()     { return tama.sessionsRunning; }
uint8_t     info_sessions_waiting()     { return tama.sessionsWaiting; }
uint32_t    info_last_msg_age_s()       { return (millis() - tama.lastUpdated) / 1000; }
const char* info_claude_state_name() {
  static const char* names[] = {"sleep","idle","busy","attention","celebrate","dizzy","heart"};
  return names[(uint8_t)activeState < 7 ? (uint8_t)activeState : 1];
}
const char* info_bt_status() {
  if (!settings().bt) return "off";
  if (dataBtActive())  return "linked";
  if (bleConnected())  return "discover";
  return "off";
}
const char* info_scenario_name()        { return dataScenarioName(); }

// Pet page bridges (stats.h accessors live in main's TU)
uint8_t  pet_mood_tier()    { return statsMoodTier(); }
uint8_t  pet_fed_progress() { return statsFedProgress(); }
uint8_t  pet_energy_tier()  { return statsEnergyTier(); }
uint8_t  pet_level()        { return stats().level; }
uint16_t pet_approvals()    { return stats().approvals; }
uint16_t pet_denials()      { return stats().denials; }
uint32_t pet_nap_seconds()  { return stats().napSeconds; }
uint32_t pet_tokens_total() { return stats().tokens; }
uint32_t pet_tokens_today() { return tama.tokensToday; }
```

These override the weak stubs in info_pages.cpp / pet_pages.cpp.

- [ ] **Step 6.4: Add pet-mode state and callbacks**

Just before `setup()`, add:

```cpp
// Pet mode runtime state
static uint32_t petEnterMs = 0;
static uint32_t petStrokeLastMs = 0;
static uint32_t petStrokeTotalMs = 0;
static bool     petFellAsleep = false;
static uint32_t petDizzyUntilMs = 0;
static uint32_t petSquishUntilMs = 0;
static const uint32_t PET_HINT_MS        = 3000;
static const uint32_t PET_IDLE_MS        = 3000;
static const uint32_t PET_MAX_STROKE_MS  = 30000;
static const uint32_t PET_DIZZY_DURATION = 1000;
static const uint32_t PET_SQUISH_DURATION = 800;

static void cb_on_enter_pet() {
  petEnterMs = millis();
  petStrokeLastMs = 0;
  petStrokeTotalMs = 0;
  petFellAsleep = false;
  petDizzyUntilMs = 0;
  petSquishUntilMs = 0;
  pet_gesture_reset();
  hw_motor_pulse_series(3, 100, settings().haptic);
}

static void cb_on_exit_pet() {
  hw_motor_purr_stop();
  hw_motor_pulse_series(2, 80, settings().haptic);
}

static void cb_on_pet_rotation(bool cw) {
  // pet_gesture runs on rotation events regardless. Step with the
  // reconstructed InputEvent for uniformity.
  PetGesture g = pet_gesture_step(cw ? EVT_ROT_CW : EVT_ROT_CCW, millis());
  if (g == PGEST_STROKE) {
    if (!hw_motor_purr_active()) hw_motor_purr_start(1);
    petStrokeLastMs = millis();
    petStrokeTotalMs += 200;   // approximate per-stroke accumulator
  } else if (g == PGEST_TICKLE) {
    // Reverse the direction the user was turning so the knob pushes back.
    hw_motor_kick(cw ? 1 : 0, settings().haptic + 1 > 4 ? 4 : settings().haptic + 1);
    petDizzyUntilMs = millis() + PET_DIZZY_DURATION;
  }
}

static void cb_on_pet_long_press() {
  hw_motor_vibrate(PET_SQUISH_DURATION, settings().haptic);
  petSquishUntilMs = millis() + PET_SQUISH_DURATION;
}

static void cb_on_info_page_change(uint8_t /*p*/)   { /* main loop repaints each frame */ }
static void cb_on_hud_scroll_change(uint8_t /*o*/)  { /* main loop repaints each frame */ }
```

- [ ] **Step 6.5: Register new callbacks in `setup()`**

Find the `FsmCallbacks fsm_cb = { ... };` in setup. Extend with the 6 new fields after the existing 10:

```cpp
  static const FsmCallbacks fsm_cb = {
    action_turn_off,
    action_delete_char,
    action_factory_reset,
    action_toggle_demo,
    cycle_brightness,
    cycle_haptic,
    toggle_transcript,
    clock_face_invalidate,
    buddyInvalidate,
    invalidate_panel_cb,
    cb_on_enter_pet,
    cb_on_exit_pet,
    cb_on_pet_rotation,
    cb_on_pet_long_press,
    cb_on_info_page_change,
    cb_on_hud_scroll_change,
  };
```

- [ ] **Step 6.6: Port `wrapInto` helper**

Somewhere before `drawHudSimple`, add the upstream word-wrap helper:

```cpp
// Greedy word-wrap into fixed-width rows. Continuation rows get a leading
// space. Returns number of rows written.
static uint8_t wrapInto(const char* in, char out[][24], uint8_t maxRows, uint8_t width) {
  uint8_t row = 0, col = 0;
  const char* p = in;
  while (*p && row < maxRows) {
    while (*p == ' ') p++;
    const char* w = p;
    while (*p && *p != ' ') p++;
    uint8_t wlen = p - w;
    if (wlen == 0) break;
    uint8_t need = (col > 0 ? 1 : 0) + wlen;
    if (col + need > width) {
      out[row][col] = 0;
      if (++row >= maxRows) return row;
      out[row][0] = ' '; col = 1;
    }
    if (col > 1 || (col == 1 && out[row][0] != ' ')) out[row][col++] = ' ';
    else if (col == 1 && row > 0) {}
    while (wlen > width - col) {
      uint8_t take = width - col;
      memcpy(&out[row][col], w, take); col += take; w += take; wlen -= take;
      out[row][col] = 0;
      if (++row >= maxRows) return row;
      out[row][0] = ' '; col = 1;
    }
    memcpy(&out[row][col], w, wlen); col += wlen;
  }
  if (col > 0 && row < maxRows) { out[row][col] = 0; row++; }
  return row;
}
```

- [ ] **Step 6.7: Rewrite `drawHudSimple` for multi-line scrolling**

Replace the existing `drawHudSimple` with:

```cpp
static void drawHudSimple() {
  TFT_eSprite& sp = hw_display_sprite();
  const int SHOW = 3;
  const int LH = 8;
  const int WIDTH = 32;
  const int AREA = SHOW * LH + 8;

  sp.fillRect(0, 240 - AREA, 240, AREA, TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.setTextDatum(TL_DATUM);

  if (tama.nLines == 0) {
    // No transcript yet; show msg as a single line if present.
    const char* line = tama.msg;
    if (line && *line) {
      sp.setCursor(24, 240 - LH - 4);
      sp.print(line);
    }
    return;
  }

  // Wrap each transcript line, build a flat display buffer (source row
  // tracked so we could dim older lines in future — not done here for
  // simplicity).
  static char disp[32][24];
  uint8_t nDisp = 0;
  for (uint8_t i = 0; i < tama.nLines && nDisp < 32; i++) {
    uint8_t got = wrapInto(tama.lines[i], &disp[nDisp], 32 - nDisp, WIDTH);
    nDisp += got;
  }
  if (nDisp == 0) return;

  uint8_t scroll = input_fsm_view().hudScroll;
  uint8_t maxBack = (nDisp > SHOW) ? (uint8_t)(nDisp - SHOW) : 0;
  if (scroll > maxBack) scroll = maxBack;

  int end = (int)nDisp - scroll;
  int start = end - SHOW; if (start < 0) start = 0;

  for (int i = 0; start + i < end; i++) {
    sp.setCursor(24, 240 - AREA + 4 + i * LH);
    sp.print(disp[start + i]);
  }

  if (scroll > 0) {
    char b[6];
    snprintf(b, sizeof(b), "-%u", (unsigned)scroll);
    sp.setTextColor(TFT_ORANGE, TFT_BLACK);
    sp.setCursor(240 - 24, 240 - LH - 4);
    sp.print(b);
  }
}
```

- [ ] **Step 6.8: Branch render on new modes + integrate motor tick + pet state selection**

Find the existing render switch in `loop()`. Extend it with new cases:

```cpp
  DisplayMode m = input_fsm_view().mode;
  switch (m) {
    case DISP_CLOCK:
      clock_face_tick();
      break;
    case DISP_MENU:     draw_main_menu(); break;
    case DISP_SETTINGS: draw_settings();  break;
    case DISP_RESET:    draw_reset();     break;
    case DISP_HELP:     draw_help();      break;
    case DISP_ABOUT:    draw_about();     break;
    case DISP_PASSKEY:  draw_passkey();   break;
    case DISP_INFO:
      draw_info_page(input_fsm_view().infoPage);
      break;
    case DISP_PET: {
      uint8_t state = (uint8_t)P_IDLE;
      if (petFellAsleep) state = (uint8_t)P_SLEEP;
      else if (millis() < petDizzyUntilMs) state = (uint8_t)P_DIZZY;
      else if (millis() < petSquishUntilMs) state = (uint8_t)P_HEART;   // squish reuses heart frames
      else if (hw_motor_purr_active()) state = (uint8_t)P_HEART;         // being stroked
      bool showHint = (millis() - petEnterMs) < PET_HINT_MS;
      draw_pet_main(state, showHint);
      break;
    }
    case DISP_PET_STATS:
      draw_pet_stats();
      break;
    case DISP_HOME:
    default: {
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
      if (inPrompt)            drawApproval();
      else if (settings().hud) drawHudSimple();
      sp.pushSprite(0, 0);
      break;
    }
  }

  hw_motor_tick(now);   // drive continuous motor patterns
```

- [ ] **Step 6.9: Pet idle / fall-asleep safety in loop**

Just before the render switch, add:

```cpp
  // Pet mode purr safety: stop if user hasn't stroked for 3 s;
  // after 30 s of total stroking, transition to fell-asleep state.
  if (input_fsm_view().mode == DISP_PET && hw_motor_purr_active()) {
    if (petStrokeLastMs > 0 && now - petStrokeLastMs > PET_IDLE_MS) {
      hw_motor_purr_stop();
    } else if (petStrokeTotalMs > PET_MAX_STROKE_MS) {
      hw_motor_purr_stop();
      petFellAsleep = true;
    }
  }
```

- [ ] **Step 6.10: CLICK handling must respect inPrompt**

Currently main dispatches CLICK to input_fsm only for the non-prompt branch. With Phase 2-B, home CLICK now cycles into Pet. That must still be suppressed while a prompt is active — the approval flow's CLICK is "send permission". Verify the existing `if (inPrompt) { ... } else { ... }` structure in main.cpp still routes CLICK to input_fsm ONLY in the else branch. Current Phase 2-A code is correct here; no change needed.

- [ ] **Step 6.11: Build**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -15
```
Expected: `[SUCCESS]`.

Common failures:
- Weak-symbol collision: main.cpp's `pet_mood_tier` should override the weak stub in pet_pages.cpp. If the linker complains about multiple definitions, drop the `__attribute__((weak))` from pet_pages.cpp (these stubs can be removed now that real impls exist).
- `hw_motor_tick` / `hw_motor_purr_*` not declared: confirm `#include "hw_motor.h"` is in main.cpp's include list.

- [ ] **Step 6.12: Remove weak stubs from info_pages.cpp / pet_pages.cpp**

Now that main.cpp provides strong implementations, the weak stubs are dead code. Remove them.

```bash
# Edit both files, delete the __attribute__((weak)) blocks at bottom.
```

Rebuild to confirm:
```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: `[SUCCESS]`. Linker finds main.cpp's implementations for all bridge symbols.

- [ ] **Step 6.13: Native test regression**

```bash
~/.platformio/penv/bin/pio test -e native 2>&1 | tail -10
```
Expected: 48+ tests pass (38 previous + 10 pet_gesture + 14 new menu_fsm).

- [ ] **Step 6.14: Commit**

```bash
git add src/main.cpp src/info_pages.cpp src/pet_pages.cpp
git commit -m "main: integrate Phase 2-B — pet mode, info pages, HUD scroll

Bridges: info_* and pet_* extern accessors read live state from tama /
stats / settings. Replaces the weak stubs in info_pages/pet_pages.

Callbacks: cb_on_enter_pet fires greeting pulse + resets pet_gesture +
clears per-entry state. cb_on_pet_rotation feeds pet_gesture and acts
on classifications (purr start on stroke, kick on tickle). cb_on_exit_pet
stops purr + fires bye pulse. cb_on_pet_long_press triggers vibrate
and squish flag.

Render: new DISP_INFO / DISP_PET / DISP_PET_STATS branches call their
respective renderers. Pet persona state selected from petFellAsleep /
petDizzyUntilMs / petSquishUntilMs / purr_active flags.

drawHudSimple rewritten to render 3 wrapped lines with scroll offset
from FSM view. Shows '-N' corner marker when scroll > 0.

hw_motor_tick(now) called every loop iteration to drive continuous
patterns.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Update menu_panels to show new entries (trivial) and on-device acceptance

Menu labels didn't change — `help` and `about` are still there, they just route into Info now (the FSM routing change in Task 3 was enough). No menu_panels.cpp change needed.

This task is pure on-device verification.

- [ ] **Step 7.1: Flash**

```bash
~/.platformio/penv/bin/pio run -t upload && ~/.platformio/penv/bin/pio device monitor
```

Run through spec §8 scenarios:

- [ ] **7.2**: home CLICK → Pet mode. Motor gives 3 pulses. Character shown at home scale.
- [ ] **7.3**: In Pet, rotate small back-and-forth. Motor purrs. Character shows P_HEART animation.
- [ ] **7.4**: Stop stroking for 3 s. Purr stops.
- [ ] **7.5**: Stroke continuously 30 s. Character transitions to P_SLEEP (fell asleep).
- [ ] **7.6**: Rotate fast quarter-turn. Motor kick-back + dizzy animation for 1 s.
- [ ] **7.7**: Long press in Pet. Motor vibrates 800 ms; character shows P_HEART briefly.
- [ ] **7.8**: Double click in Pet → stats sub-page with mood, fed, energy, level, counters.
- [ ] **7.9**: Click in stats sub-page → back to Pet main.
- [ ] **7.10**: Click in Pet → Info page 0 (ABOUT). Two pulses on exit.
- [ ] **7.11**: Rotate CW in Info → page 1 (CLAUDE). Session counts update live.
- [ ] **7.12**: Continue rotating → page 2 (SYSTEM) with uptime/heap/BT info, page 3 (CREDITS).
- [ ] **7.13**: Click in Info → home. Capybara visible.
- [ ] **7.14**: Menu → `help` → Info page 0 directly.
- [ ] **7.15**: Menu → `about` → Info page 3 directly.
- [ ] **7.16**: On home, rotate CW → bottom HUD shows older line. `-1` in corner.
- [ ] **7.17**: Rotate CCW → back to newest. Indicator disappears.
- [ ] **7.18**: Trigger a prompt while in Pet. Returns to home with approval. Purr stops.

- [ ] **7.19: Push branch**

```bash
git push -u origin phase-2b
```

Open a PR at https://github.com/ZinkLu/claude-desktop-buddy/pull/new/phase-2b titled `Phase 2-B: info pages, interactive pet mode, HUD scroll`.

---

## Risks & watchpoints

1. **Purr pulse strength and thermals** — first real continuous motor pattern. 200 pulses at strength 50 over 30 s is still well under ~5 A·s motor energy. Watch for warming during Task 7.5. If hot, lower the purr strength or raise `PURR_GAP_MS` from 150 to 250.

2. **Gesture classifier thresholds** — 4 events in 400 ms with 2 alternations is a first guess. On hardware, users may find it easy or hard to trigger STROKE. Tunable via native tests without reflashing.

3. **Render ordering for pet** — `buddyTick` writes to the sprite before `draw_pet_main` writes hint + footer. That's fine since buddy's y-range is ~55..180 and pet's overlays are y<40 and y>195. No overlap.

4. **`petStrokeTotalMs` approximation** — we add 200 ms per detected stroke, not real elapsed time. Good enough for the 30-s safety cutoff but not precise. If the user reports "fell asleep after only 5 strokes", switch to tracking real elapsed stroke time.

5. **Info page 1 (CLAUDE) live updates** — called every frame. `info_last_msg_age_s()` changes every second, triggering a full panel redraw at 50 fps. Acceptable (same as clock mode) but ~5 kB of text overdraw per second. If this eats CPU, gate on `seconds_changed` like clock_face does.

6. **Memory growth** — 4 new cpp files add flash. Phase 2-A ended at Flash 14.6%; Phase 2-B likely lands around 15.5%. Still far from the 7 MB partition limit.

7. **`wrapInto` imported into main.cpp** — ~30 lines added. Main.cpp grows past 500. Tolerable for now; mark for cleanup when sub-project D lands and main balloons further.

---

## When Task 7 passes

Merge the PR. Update `MEMORY.md`:
- Move sub-project B from deferred to completed in `phase2_backlog.md`
- C and A already completed; B now too — only D / E / F remain
