# Phase 2-A Menus + Input FSM Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the sparse Phase 1/2-C main loop with a proper interactive shell: context-sensitive input FSM (LONG opens a main menu), three menu panels (main/settings/reset), static help and about screens, plus a full-screen passkey UI that appears during BLE pairing. New `brightness` and `haptic` settings persist to NVS.

**Architecture:** Extract input routing and menu state into `src/input_fsm.{h,cpp}` with its own `DisplayMode` enum + `FsmView` accessor; extract drawing into `src/menu_panels.{h,cpp}`. `main.cpp` shrinks: it becomes a thin adapter that polls events, calls `input_fsm_dispatch`, polls `blePasskey()`, and branches the render loop on `input_fsm_view().mode`. Side effects (turn off, haptic change, brightness change, etc.) flow back to `main.cpp` via a callback struct registered at init.

**Tech Stack:** Same as Phase 1/2-C. New native tests use the existing `[env:native]` + Unity setup.

**Spec:** `docs/superpowers/specs/2026-04-18-phase2a-menus-design.md`
**Target branch:** cut `phase-2a` from `main` at the start of Task 1 (main currently at `5c79d68` — the spec commit).
**PIO binary:** `~/.platformio/penv/bin/pio`
**Working dir:** `/Users/zinklu/code/opensources/claude-desktop-buddy`

---

## File Structure

**New:**
- `src/input_fsm.h` — `DisplayMode` enum (moves here from `main.cpp`), `FsmCallbacks` struct, `FsmView` struct, public API.
- `src/input_fsm.cpp` — private state, dispatch logic, reset-confirm timer, passkey overlay transitions.
- `src/menu_panels.h` — six render function declarations.
- `src/menu_panels.cpp` — render implementations (main / settings / reset / help / about / passkey).
- `test/test_input_fsm/test_input_fsm.cpp` — Unity tests for pure FSM state transitions.

**Modified:**
- `src/main.cpp` — remove Phase 2-C's local `DisplayMode` + LONG handler (now in `input_fsm`); branch render loop on `input_fsm_view().mode`; wire BLE passkey polling; register callbacks; migrate `hw_motor_click(120)` call sites to new `hw_motor_click_default()`.
- `src/stats.h` — extend `Settings` struct with `brightness` and `haptic`; extend `settingsLoad` / `settingsSave`; update the `_settings` default literal.
- `src/hw_motor.h` + `src/hw_motor.cpp` — add `hw_motor_click_default()` that reads `settings().haptic` and dispatches through a strength table.
- `platformio.ini` — extend both envs' `build_src_filter` with `+<input_fsm.cpp>` and `+<menu_panels.cpp>`.

**Untouched:** `ble_bridge.*`, `hw_input.*`, `hw_display.*`, `hw_power.*`, `hw_leds.*`, `buddy.*`, `buddies/*.cpp`, `character.*`, `data.h`, `xfer.h`, `clock_face.*`.

---

## Pre-flight

- [ ] **Sync main and create `phase-2a`**

```bash
cd /Users/zinklu/code/opensources/claude-desktop-buddy
git checkout main
git pull origin main
git log --oneline -1
# Expected: 5c79d68 Add Phase 2-A spec: input FSM + menus + passkey UI
git checkout -b phase-2a
```

---

## Task 1: Extend `Settings` with brightness + haptic

Foundational. No UI yet. Just persists and reads two new bytes.

**Files:**
- Modify: `src/stats.h`

- [ ] **Step 1.1: Read current `Settings` struct and load/save helpers**

```bash
sed -n '179,215p' src/stats.h
```
Record the surrounding context so edits are unambiguous.

- [ ] **Step 1.2: Extend the `Settings` struct**

Find the block that currently reads:
```cpp
struct Settings {
  bool sound;
  bool bt;
  bool wifi;     // placeholder — no WiFi stack linked yet, just stores the pref
  bool led;
  bool hud;
  uint8_t clockRot;  // 0=auto 1=portrait 2=landscape
};

static Settings _settings = { true, true, false, true, true, 0 };
```

Replace with:
```cpp
struct Settings {
  bool sound;
  bool bt;
  bool wifi;     // placeholder — no WiFi stack linked yet, just stores the pref
  bool led;
  bool hud;
  uint8_t clockRot;  // 0=auto 1=portrait 2=landscape (unused on X-Knob)
  uint8_t brightness;  // 0..4 → 20%..100% backlight PWM
  uint8_t haptic;      // 0..4 → motor bump strength (0 = off)
};

static Settings _settings = { true, true, false, true, true, 0, 3, 3 };
```

Defaults: `brightness = 3` (60%), `haptic = 3` (strength 120, the Phase 1 hardcoded default).

- [ ] **Step 1.3: Extend `settingsLoad`**

Find:
```cpp
inline void settingsLoad() {
  _prefs.begin("buddy", true);
  _settings.sound = _prefs.getBool("s_snd", true);
  _settings.bt    = _prefs.getBool("s_bt",  true);
  _settings.wifi  = _prefs.getBool("s_wifi",false);
  _settings.led   = _prefs.getBool("s_led", true);
  _settings.hud      = _prefs.getBool("s_hud", true);
  _settings.clockRot = _prefs.getUChar("s_crot", 0);
  if (_settings.clockRot > 2) _settings.clockRot = 0;
  _prefs.end();
}
```

Before the `_prefs.end();` line, add:
```cpp
  _settings.brightness = _prefs.getUChar("s_bright", 3);
  if (_settings.brightness > 4) _settings.brightness = 3;
  _settings.haptic     = _prefs.getUChar("s_haptic", 3);
  if (_settings.haptic > 4) _settings.haptic = 3;
```

- [ ] **Step 1.4: Extend `settingsSave`**

Find:
```cpp
inline void settingsSave() {
  _prefs.begin("buddy", false);
  _prefs.putBool("s_snd", _settings.sound);
  _prefs.putBool("s_bt",  _settings.bt);
  _prefs.putBool("s_wifi",_settings.wifi);
  _prefs.putBool("s_led", _settings.led);
  _prefs.putBool("s_hud", _settings.hud);
  _prefs.putUChar("s_crot", _settings.clockRot);
  _prefs.end();
}
```

Before the `_prefs.end();` line, add:
```cpp
  _prefs.putUChar("s_bright", _settings.brightness);
  _prefs.putUChar("s_haptic", _settings.haptic);
```

- [ ] **Step 1.5: Build**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -4
```
Expected: `[SUCCESS]`. Nothing consumes the new fields yet; build just needs to keep working.

- [ ] **Step 1.6: Commit**

```bash
git add src/stats.h
git commit -m "settings: add brightness (0-4) and haptic (0-4) with NVS persistence

New s_bright and s_haptic keys default to 3. Clamps any out-of-range
value from NVS back to 3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `hw_motor_click_default()` + migrate call sites

Centralize motor strength reading from settings so every future call site respects the user's haptic level.

**Files:**
- Modify: `src/hw_motor.h`, `src/hw_motor.cpp`, `src/main.cpp`

- [ ] **Step 2.1: Extend `src/hw_motor.h`**

Current content of `src/hw_motor.h`:
```cpp
#pragma once
#include <stdint.h>

void hw_motor_init();
void hw_motor_click(uint8_t strength);   // strength 0..255, ~30 ms open-loop pulse
void hw_motor_off();
```

Append one declaration so it reads:
```cpp
#pragma once
#include <stdint.h>

void hw_motor_init();
void hw_motor_click(uint8_t strength);   // strength 0..255, ~30 ms open-loop pulse
void hw_motor_off();

// Convenience wrapper: reads the current haptic level from settings() and
// fires a click at the configured strength. Level 0 is a silent no-op.
void hw_motor_click_default();
```

- [ ] **Step 2.2: Implement the wrapper in `src/hw_motor.cpp`**

At the bottom of `src/hw_motor.cpp`, append:
```cpp
#include "stats.h"

static const uint8_t HAPTIC_STRENGTH[5] = { 0, 40, 80, 120, 200 };

void hw_motor_click_default() {
  uint8_t level = settings().haptic;
  if (level > 4) level = 3;     // defensive clamp; NVS load already clamps
  if (level == 0) return;       // haptic off
  hw_motor_click(HAPTIC_STRENGTH[level]);
}
```

`stats.h` is header-only with `static Preferences _prefs;` etc. Including it from a second translation unit normally creates duplicate state, **but** we're only reading `settings().haptic` via the inline accessor. The `settings()` return is `_settings&` from whichever TU loaded it. To avoid the duplicate-state trap, include `stats.h` **only** inside the `hw_motor.cpp` file scope where the compiler has already seen `main.cpp`'s copy of `_settings` — that means in Arduino builds this links to the correct `_settings`. Test this with the build.

If the build emits duplicate-symbol errors for `_settings` / `_prefs`: back the wrapper with an extern fn instead:
1. In `src/main.cpp`, after including `stats.h`, add `uint8_t current_haptic_level() { return settings().haptic; }`.
2. In `src/hw_motor.cpp`, replace `#include "stats.h"` with `extern "C" uint8_t current_haptic_level();` (or `extern uint8_t current_haptic_level();` since it's C++).
3. Replace `uint8_t level = settings().haptic;` with `uint8_t level = current_haptic_level();`.

We try the direct include first because it's cleaner; fall back if the linker complains.

- [ ] **Step 2.3: Build**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -15
```
Expected: `[SUCCESS]` on the direct-include approach. If duplicate-symbol errors appear, apply the extern-fn fallback from step 2.2 and rebuild.

- [ ] **Step 2.4: Migrate `main.cpp` call sites**

In `src/main.cpp`, find every `hw_motor_click(120)` call. At time of writing (on main at `5c79d68`) there is one in `loop()`:
```cpp
  if (e != EVT_NONE) hw_motor_click(120);
```

Replace with:
```cpp
  if (e != EVT_NONE) hw_motor_click_default();
```

If you find additional hardcoded strengths in main.cpp, leave them — the only one to migrate is the generic "on any event" click. Approval-confirm could later use a custom strength; that's a sub-project D concern.

- [ ] **Step 2.5: Build again**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -5
```
Expected: `[SUCCESS]`.

- [ ] **Step 2.6: Commit**

```bash
git add src/hw_motor.h src/hw_motor.cpp src/main.cpp
git commit -m "motor: add hw_motor_click_default, honor settings().haptic

Strength table {0,40,80,120,200} maps haptic level 0-4 to motor
PWM magnitude. Level 0 skips the click entirely. Main loop's generic
'any input event' click now reads the user's current preference.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: `input_fsm.h` header

Establish the public API and move `DisplayMode` here so every TU agrees on it.

**Files:**
- Create: `src/input_fsm.h`

- [ ] **Step 3.1: Write the header**

`src/input_fsm.h`:
```cpp
#pragma once
#include <stdint.h>
#include "hw_input.h"

enum DisplayMode {
  DISP_HOME = 0,
  DISP_CLOCK,
  DISP_MENU,
  DISP_SETTINGS,
  DISP_RESET,
  DISP_HELP,
  DISP_ABOUT,
  DISP_PASSKEY,
};

// Side-effect callbacks the FSM invokes in response to menu actions.
// All are optional (any NULL pointer is silently skipped). Register once
// at init.
struct FsmCallbacks {
  // Action items
  void (*turn_off)();
  void (*delete_char)();
  void (*factory_reset)();
  void (*toggle_demo)();

  // Settings changes — payload = the new level (0..4) or new bool.
  void (*brightness_changed)(uint8_t level);
  void (*haptic_changed)(uint8_t level);
  void (*transcript_changed)(bool on);

  // Render invalidations
  void (*invalidate_clock)();
  void (*invalidate_buddy)();
  void (*invalidate_panel)();   // repaint the active menu panel
};

// Snapshot of FSM state for renderers. All fields read-only from outside.
struct FsmView {
  DisplayMode mode;
  uint8_t menuSel;
  uint8_t settingsSel;
  uint8_t resetSel;
  uint8_t resetConfirmIdx;    // 0xFF if not armed
  uint32_t resetConfirmUntil; // millis() expiry; 0 if not armed
};

// API
void input_fsm_init(const FsmCallbacks* cb);
void input_fsm_dispatch(InputEvent e, uint32_t now_ms);
void input_fsm_tick(uint32_t now_ms);    // clears expired reset arm; called each main loop
void input_fsm_on_passkey_change(bool active);  // called when blePasskey() (non-)zero transitions
void input_fsm_force_home_on_prompt();
const FsmView& input_fsm_view();

// Internal helpers exposed for unit tests only.
namespace input_fsm_internal {
  // Reset all FSM state to defaults. Used by test setUp() and input_fsm_init.
  void reset_for_tests();
}
```

- [ ] **Step 3.2: Build**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -4
```
Expected: `[SUCCESS]`. Nothing includes this header yet; it just needs to compile.

- [ ] **Step 3.3: Commit**

```bash
git add src/input_fsm.h
git commit -m "input_fsm: add header with DisplayMode, FsmCallbacks, FsmView

Moves DisplayMode out of main.cpp so input_fsm, menu_panels, and
main.cpp all see the same enum. Adds a callback struct for side-effect
dispatch and a view struct so renderers can read FSM state without
directly accessing module-internal globals.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Native tests for `input_fsm` (TDD red)

**Files:**
- Create: `test/test_input_fsm/test_input_fsm.cpp`
- Modify: `platformio.ini` (extend `[env:native]` `build_src_filter`)

- [ ] **Step 4.1: Extend native env filter**

Open `platformio.ini`. Find `[env:native]`. Current `build_src_filter = +<hw_input.cpp> +<clock_face.cpp>`. Change to:
```
build_src_filter = +<hw_input.cpp> +<clock_face.cpp> +<input_fsm.cpp>
```

Also find `[env:x-knob]` and append `+<input_fsm.cpp>` to its `build_src_filter` at the end.

- [ ] **Step 4.2: Write the test file**

`test/test_input_fsm/test_input_fsm.cpp`:
```cpp
#include <unity.h>
#include "../../src/input_fsm.h"
#include "../../src/hw_input.h"

// --- Test doubles for the callback struct -------------------------------

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
};

static MockCalls mock;
static void mk_turn_off()                       { mock.turn_off++; }
static void mk_delete_char()                    { mock.delete_char++; }
static void mk_factory_reset()                  { mock.factory_reset++; }
static void mk_toggle_demo()                    { mock.toggle_demo++; }
static void mk_brightness(uint8_t l)            { mock.brightness_changed++; mock.last_brightness = l; }
static void mk_haptic(uint8_t l)                { mock.haptic_changed++;     mock.last_haptic = l; }
static void mk_transcript(bool on)              { mock.transcript_changed++; mock.last_transcript = on; }
static void mk_invalidate_clock()               { mock.invalidate_clock++; }
static void mk_invalidate_buddy()               { mock.invalidate_buddy++; }
static void mk_invalidate_panel()               { mock.invalidate_panel++; }

static const FsmCallbacks CB = {
  mk_turn_off, mk_delete_char, mk_factory_reset, mk_toggle_demo,
  mk_brightness, mk_haptic, mk_transcript,
  mk_invalidate_clock, mk_invalidate_buddy, mk_invalidate_panel,
};

// --- Host-side shim for settings().haptic / brightness / hud ------------
// The FSM reads settings values via callbacks, not via stats.h, so tests
// don't need a shim.

void setUp() {
  mock = MockCalls{};
  input_fsm_internal::reset_for_tests();
  input_fsm_init(&CB);
}
void tearDown() {}

// --- Tests --------------------------------------------------------------

void test_init_is_home() {
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_long_home_to_menu() {
  input_fsm_dispatch(EVT_LONG, 1000);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().menuSel);
}

void test_long_menu_to_home() {
  input_fsm_dispatch(EVT_LONG, 1000);   // home -> menu
  input_fsm_dispatch(EVT_LONG, 1100);   // menu -> home
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.invalidate_buddy);  // home renderer invalidated on exit
}

void test_menu_scroll_wraps() {
  input_fsm_dispatch(EVT_LONG, 1000);   // -> menu, sel=0
  // 6 CCW should wrap past 0
  input_fsm_dispatch(EVT_ROT_CCW, 1100);
  TEST_ASSERT_EQUAL(6, input_fsm_view().menuSel);  // wrapped to last item (close)
}

void test_menu_click_clock_enters_clock() {
  input_fsm_dispatch(EVT_LONG, 1000);   // menu
  input_fsm_dispatch(EVT_ROT_CW, 1100); // sel 0 -> 1 (clock)
  input_fsm_dispatch(EVT_CLICK, 1200);
  TEST_ASSERT_EQUAL(DISP_CLOCK, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.invalidate_clock);
}

void test_menu_click_turn_off_calls_callback() {
  input_fsm_dispatch(EVT_LONG, 1000);
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);  // sel = 2 (turn off)
  input_fsm_dispatch(EVT_CLICK, 1300);
  TEST_ASSERT_EQUAL(1, mock.turn_off);
}

void test_menu_click_settings_opens_settings() {
  input_fsm_dispatch(EVT_LONG, 1000);
  // menuSel already at 0 (settings)
  input_fsm_dispatch(EVT_CLICK, 1100);
  TEST_ASSERT_EQUAL(DISP_SETTINGS, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().settingsSel);
}

void test_clock_long_returns_home() {
  input_fsm_dispatch(EVT_LONG, 1000);    // menu
  input_fsm_dispatch(EVT_ROT_CW, 1100);  // sel = 1 (clock)
  input_fsm_dispatch(EVT_CLICK, 1200);   // enter clock
  input_fsm_dispatch(EVT_LONG, 1300);    // exit to home
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_clock_rotation_is_noop() {
  input_fsm_dispatch(EVT_LONG, 1000);
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  input_fsm_dispatch(EVT_CLICK, 1200);   // into clock
  DisplayMode before = input_fsm_view().mode;
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CCW, 1400);
  TEST_ASSERT_EQUAL(before, input_fsm_view().mode);
}

void test_settings_brightness_cycles() {
  input_fsm_dispatch(EVT_LONG, 1000);     // menu
  input_fsm_dispatch(EVT_CLICK, 1100);    // into settings, sel=0 (brightness)
  input_fsm_dispatch(EVT_CLICK, 1200);    // click brightness
  TEST_ASSERT_EQUAL(1, mock.brightness_changed);
  // Level cycles via the callback; FSM doesn't track value, main.cpp does.
  // Just verify the callback fires on every CLICK.
  input_fsm_dispatch(EVT_CLICK, 1300);
  TEST_ASSERT_EQUAL(2, mock.brightness_changed);
}

void test_settings_back_returns_home() {
  input_fsm_dispatch(EVT_LONG, 1000);     // menu
  input_fsm_dispatch(EVT_CLICK, 1100);    // settings
  // scroll to 'back' (item 4)
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CW, 1400);
  input_fsm_dispatch(EVT_ROT_CW, 1500);
  TEST_ASSERT_EQUAL(4, input_fsm_view().settingsSel);
  input_fsm_dispatch(EVT_CLICK, 1600);
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_settings_long_returns_home() {
  input_fsm_dispatch(EVT_LONG, 1000);
  input_fsm_dispatch(EVT_CLICK, 1100);
  input_fsm_dispatch(EVT_LONG, 1200);
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_reset_arm_then_execute() {
  input_fsm_dispatch(EVT_LONG, 1000);     // menu
  input_fsm_dispatch(EVT_CLICK, 1100);    // settings
  // scroll to 'reset' (item 3)
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CW, 1400);
  TEST_ASSERT_EQUAL(3, input_fsm_view().settingsSel);
  input_fsm_dispatch(EVT_CLICK, 1500);    // into reset submenu
  TEST_ASSERT_EQUAL(DISP_RESET, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().resetSel);  // delete char
  // First click: arms
  input_fsm_dispatch(EVT_CLICK, 1600);
  TEST_ASSERT_EQUAL(0, input_fsm_view().resetConfirmIdx);
  TEST_ASSERT_EQUAL(0, mock.delete_char);
  // Second click within 3s: executes
  input_fsm_dispatch(EVT_CLICK, 1700);
  TEST_ASSERT_EQUAL(1, mock.delete_char);
}

void test_reset_arm_expires() {
  input_fsm_dispatch(EVT_LONG, 1000);
  input_fsm_dispatch(EVT_CLICK, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CW, 1400);
  input_fsm_dispatch(EVT_CLICK, 1500);   // into reset
  input_fsm_dispatch(EVT_CLICK, 1600);   // arm delete_char
  TEST_ASSERT_EQUAL(0, input_fsm_view().resetConfirmIdx);
  // Tick with now past expiry
  input_fsm_tick(5000);
  TEST_ASSERT_EQUAL(0xFF, input_fsm_view().resetConfirmIdx);
  // Second click now is a fresh arm, not execute
  input_fsm_dispatch(EVT_CLICK, 5100);
  TEST_ASSERT_EQUAL(0, input_fsm_view().resetConfirmIdx);
  TEST_ASSERT_EQUAL(0, mock.delete_char);
}

void test_reset_scroll_cancels_arm() {
  input_fsm_dispatch(EVT_LONG, 1000);
  input_fsm_dispatch(EVT_CLICK, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CW, 1400);
  input_fsm_dispatch(EVT_CLICK, 1500);   // into reset
  input_fsm_dispatch(EVT_CLICK, 1600);   // arm delete_char
  TEST_ASSERT_EQUAL(0, input_fsm_view().resetConfirmIdx);
  input_fsm_dispatch(EVT_ROT_CW, 1700);  // scroll away
  TEST_ASSERT_EQUAL(0xFF, input_fsm_view().resetConfirmIdx);
}

void test_passkey_overrides_and_restores() {
  input_fsm_dispatch(EVT_LONG, 1000);    // menu
  input_fsm_on_passkey_change(true);
  TEST_ASSERT_EQUAL(DISP_PASSKEY, input_fsm_view().mode);
  input_fsm_on_passkey_change(false);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
}

void test_passkey_ignores_input() {
  input_fsm_on_passkey_change(true);
  input_fsm_dispatch(EVT_CLICK, 1000);
  input_fsm_dispatch(EVT_LONG,  1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  TEST_ASSERT_EQUAL(DISP_PASSKEY, input_fsm_view().mode);
}

void test_prompt_force_home_from_menu() {
  input_fsm_dispatch(EVT_LONG, 1000);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
  input_fsm_force_home_on_prompt();
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.invalidate_buddy);
}

void test_prompt_force_home_respects_passkey() {
  input_fsm_on_passkey_change(true);
  input_fsm_force_home_on_prompt();
  // Passkey wins; force-home is ignored while passkey active.
  TEST_ASSERT_EQUAL(DISP_PASSKEY, input_fsm_view().mode);
}

void test_help_and_about_from_menu() {
  input_fsm_dispatch(EVT_LONG, 1000);     // menu
  // help = item 3
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  TEST_ASSERT_EQUAL(3, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 1400);
  TEST_ASSERT_EQUAL(DISP_HELP, input_fsm_view().mode);
  input_fsm_dispatch(EVT_CLICK, 1500);   // dismiss
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
  // about = item 4
  input_fsm_dispatch(EVT_ROT_CW, 1600);
  TEST_ASSERT_EQUAL(4, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 1700);
  TEST_ASSERT_EQUAL(DISP_ABOUT, input_fsm_view().mode);
  input_fsm_dispatch(EVT_LONG, 1800);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
}

void test_demo_stays_in_menu() {
  input_fsm_dispatch(EVT_LONG, 1000);
  // demo = item 5
  for (int i = 0; i < 5; i++) input_fsm_dispatch(EVT_ROT_CW, 1100 + i*100);
  TEST_ASSERT_EQUAL(5, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 2000);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);  // stays
  TEST_ASSERT_EQUAL(1, mock.toggle_demo);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_is_home);
  RUN_TEST(test_long_home_to_menu);
  RUN_TEST(test_long_menu_to_home);
  RUN_TEST(test_menu_scroll_wraps);
  RUN_TEST(test_menu_click_clock_enters_clock);
  RUN_TEST(test_menu_click_turn_off_calls_callback);
  RUN_TEST(test_menu_click_settings_opens_settings);
  RUN_TEST(test_clock_long_returns_home);
  RUN_TEST(test_clock_rotation_is_noop);
  RUN_TEST(test_settings_brightness_cycles);
  RUN_TEST(test_settings_back_returns_home);
  RUN_TEST(test_settings_long_returns_home);
  RUN_TEST(test_reset_arm_then_execute);
  RUN_TEST(test_reset_arm_expires);
  RUN_TEST(test_reset_scroll_cancels_arm);
  RUN_TEST(test_passkey_overrides_and_restores);
  RUN_TEST(test_passkey_ignores_input);
  RUN_TEST(test_prompt_force_home_from_menu);
  RUN_TEST(test_prompt_force_home_respects_passkey);
  RUN_TEST(test_help_and_about_from_menu);
  RUN_TEST(test_demo_stays_in_menu);
  return UNITY_END();
}
```

- [ ] **Step 4.3: Run — expect link failure**

```bash
~/.platformio/penv/bin/pio test -e native -f test_input_fsm 2>&1 | tail -20
```
Expected: unresolved symbols for `input_fsm_init`, `input_fsm_dispatch`, etc. — `input_fsm.cpp` doesn't exist yet.

Also expected: the firmware build **breaks** now because the `+<input_fsm.cpp>` filter entry points at a non-existent file. That's fine; Task 5 creates it. Don't attempt a firmware build at this point.

- [ ] **Step 4.4: Commit**

```bash
git add test/test_input_fsm/test_input_fsm.cpp platformio.ini
git commit -m "input_fsm: add failing unit tests for all FSM transitions

20 tests covering init, LONG from/to menu, rotation wrap, menu item
clicks (clock/turn-off/settings/help/about/demo), clock no-op rotation,
settings back-and-long navigation, reset double-confirm with expiry and
scroll-cancel, passkey override + input suppression, prompt force-home
respecting passkey.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `input_fsm.cpp` implementation (TDD green)

**Files:**
- Create: `src/input_fsm.cpp`

- [ ] **Step 5.1: Write `src/input_fsm.cpp`**

```cpp
#include "input_fsm.h"
#include <string.h>

// Menu item indices
//   Main:      0 settings, 1 clock, 2 turn off, 3 help, 4 about, 5 demo, 6 close
//   Settings:  0 brightness, 1 haptic, 2 transcript, 3 reset, 4 back
//   Reset:     0 delete char, 1 factory reset, 2 back

static const uint8_t MENU_N     = 7;
static const uint8_t SETTINGS_N = 5;
static const uint8_t RESET_N    = 3;
static const uint32_t RESET_CONFIRM_WINDOW_MS = 3000;

// Module state
static FsmCallbacks _cb = {};
static FsmView _v;
static DisplayMode _previousForPasskey = DISP_HOME;
static bool _passkeyActive = false;

static void _reset_state() {
  _v.mode = DISP_HOME;
  _v.menuSel = 0;
  _v.settingsSel = 0;
  _v.resetSel = 0;
  _v.resetConfirmIdx = 0xFF;
  _v.resetConfirmUntil = 0;
  _previousForPasskey = DISP_HOME;
  _passkeyActive = false;
}

void input_fsm_init(const FsmCallbacks* cb) {
  if (cb) _cb = *cb;
  _reset_state();
}

namespace input_fsm_internal {
void reset_for_tests() { _reset_state(); _cb = {}; }
}

const FsmView& input_fsm_view() { return _v; }

// Safe callback invocation — any null pointer is silently ignored.
#define CALL0(fn)      do { if (_cb.fn) _cb.fn(); } while (0)
#define CALL1(fn, a)   do { if (_cb.fn) _cb.fn(a); } while (0)

static void _enter(DisplayMode m) {
  _v.mode = m;
}

static void _clear_reset_arm() {
  _v.resetConfirmIdx = 0xFF;
  _v.resetConfirmUntil = 0;
}

static void _go_home() {
  _v.mode = DISP_HOME;
  _clear_reset_arm();
  CALL0(invalidate_buddy);
}

// Menu item 0..6; returns the side-effect for CLICK.
static void _menu_click(uint8_t idx) {
  switch (idx) {
    case 0: _enter(DISP_SETTINGS); _v.settingsSel = 0; _clear_reset_arm(); CALL0(invalidate_panel); break;
    case 1: _enter(DISP_CLOCK); CALL0(invalidate_clock); break;
    case 2: CALL0(turn_off); break;    // never returns; FSM state irrelevant after
    case 3: _enter(DISP_HELP); CALL0(invalidate_panel); break;
    case 4: _enter(DISP_ABOUT); CALL0(invalidate_panel); break;
    case 5: CALL0(toggle_demo); CALL0(invalidate_panel); break;   // stays in menu
    case 6: _go_home(); break;
    default: break;
  }
}

// Settings items 0..4
static void _settings_click(uint8_t idx) {
  switch (idx) {
    case 0: CALL1(brightness_changed, 0); CALL0(invalidate_panel); break;   // main owns cycling
    case 1: CALL1(haptic_changed, 0);     CALL0(invalidate_panel); break;
    case 2: CALL1(transcript_changed, false); CALL0(invalidate_panel); break;
    case 3: _enter(DISP_RESET); _v.resetSel = 0; _clear_reset_arm(); CALL0(invalidate_panel); break;
    case 4: _go_home(); break;   // 'back' returns to home, not menu (per spec §4)
    default: break;
  }
}

// Reset items 0..2 with double-confirm
static void _reset_click(uint8_t idx, uint32_t now) {
  if (idx == 2) {                      // back
    _clear_reset_arm();
    _enter(DISP_SETTINGS);
    CALL0(invalidate_panel);
    return;
  }
  bool armed = (_v.resetConfirmIdx == idx) && (now < _v.resetConfirmUntil);
  if (!armed) {
    _v.resetConfirmIdx = idx;
    _v.resetConfirmUntil = now + RESET_CONFIRM_WINDOW_MS;
    CALL0(invalidate_panel);
    return;
  }
  _clear_reset_arm();
  if (idx == 0) CALL0(delete_char);
  else          CALL0(factory_reset);
}

void input_fsm_dispatch(InputEvent e, uint32_t now_ms) {
  // Passkey overlay swallows all input.
  if (_passkeyActive) return;

  // LONG from anywhere (except HOME) is "get out to home".
  if (e == EVT_LONG) {
    switch (_v.mode) {
      case DISP_HOME:
        _enter(DISP_MENU);
        _v.menuSel = 0;
        _clear_reset_arm();
        CALL0(invalidate_panel);
        return;
      case DISP_HELP:
      case DISP_ABOUT:
        // LONG from help/about returns to menu, not home — users got there from menu.
        _enter(DISP_MENU);
        CALL0(invalidate_panel);
        return;
      default:
        _go_home();
        return;
    }
  }

  // Rotation
  if (e == EVT_ROT_CW || e == EVT_ROT_CCW) {
    int dir = (e == EVT_ROT_CW) ? 1 : -1;
    switch (_v.mode) {
      case DISP_MENU:
        _v.menuSel = (uint8_t)((_v.menuSel + dir + MENU_N) % MENU_N);
        CALL0(invalidate_panel);
        return;
      case DISP_SETTINGS:
        _v.settingsSel = (uint8_t)((_v.settingsSel + dir + SETTINGS_N) % SETTINGS_N);
        CALL0(invalidate_panel);
        return;
      case DISP_RESET:
        _v.resetSel = (uint8_t)((_v.resetSel + dir + RESET_N) % RESET_N);
        _clear_reset_arm();  // scrolling cancels arm
        CALL0(invalidate_panel);
        return;
      default:
        return;  // home/clock/help/about/passkey ignore rotation
    }
  }

  // CLICK
  if (e == EVT_CLICK) {
    switch (_v.mode) {
      case DISP_MENU:     _menu_click(_v.menuSel); return;
      case DISP_SETTINGS: _settings_click(_v.settingsSel); return;
      case DISP_RESET:    _reset_click(_v.resetSel, now_ms); return;
      case DISP_HELP:
      case DISP_ABOUT:
        _enter(DISP_MENU);
        CALL0(invalidate_panel);
        return;
      default:
        return;  // home click is handled by approval flow in main.cpp;
                 // FSM ignores it here. Clock click is ignored too.
    }
  }

  // DOUBLE is unused across all modes.
}

void input_fsm_tick(uint32_t now_ms) {
  if (_v.resetConfirmIdx != 0xFF && now_ms >= _v.resetConfirmUntil) {
    _clear_reset_arm();
    if (_v.mode == DISP_RESET) CALL0(invalidate_panel);
  }
}

void input_fsm_on_passkey_change(bool active) {
  if (active && !_passkeyActive) {
    _previousForPasskey = _v.mode;
    _v.mode = DISP_PASSKEY;
    _passkeyActive = true;
    CALL0(invalidate_panel);
  } else if (!active && _passkeyActive) {
    _v.mode = _previousForPasskey;
    _passkeyActive = false;
    // Repaint whatever comes back.
    if (_v.mode == DISP_HOME)  CALL0(invalidate_buddy);
    else if (_v.mode == DISP_CLOCK) CALL0(invalidate_clock);
    else CALL0(invalidate_panel);
  }
}

void input_fsm_force_home_on_prompt() {
  if (_passkeyActive) return;   // passkey wins
  if (_v.mode == DISP_HOME) return;
  _go_home();
}
```

- [ ] **Step 5.2: Run native tests — expect all 20 green**

```bash
~/.platformio/penv/bin/pio test -e native -f test_input_fsm 2>&1 | tail -30
```
Expected: 20/20 PASSED.

If any fail, read the Unity output carefully. Common gotchas:
- `test_menu_click_clock_enters_clock`: after pressing CLICK on `clock`, make sure the clock entry sequence is `_enter(DISP_CLOCK); CALL0(invalidate_clock);` — in that order.
- `test_reset_arm_expires`: `input_fsm_tick` must check `now_ms >= resetConfirmUntil`, not `now_ms > resetConfirmUntil`, to catch exact-edge expiry.
- `test_prompt_force_home_respects_passkey`: the passkey guard at the top of `input_fsm_force_home_on_prompt` must return BEFORE any mode change.

Iterate on `input_fsm.cpp` until green. Do not edit the tests.

- [ ] **Step 5.3: Firmware build (best-effort)**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -15
```
Expected: `[SUCCESS]`. `input_fsm.cpp` doesn't reference any hardware; should build fine.

- [ ] **Step 5.4: Commit**

```bash
git add src/input_fsm.cpp
git commit -m "input_fsm: implement dispatch to pass all 20 unit tests

Module-internal FsmView state, callback-based side effects, double-confirm
timer for reset, passkey overlay that saves and restores previous mode,
prompt-interrupt force-home that defers to passkey.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `menu_panels.{h,cpp}` — render functions

**Files:**
- Create: `src/menu_panels.h`, `src/menu_panels.cpp`
- Modify: `platformio.ini` (add `+<menu_panels.cpp>` to firmware env filter)

- [ ] **Step 6.1: Write `src/menu_panels.h`**

```cpp
#pragma once

// Each function paints to the shared hw_display_sprite and pushes it.
// Called from main.cpp after input_fsm transitions to the corresponding
// mode, or on invalidate_panel callback.
void draw_main_menu();
void draw_settings();
void draw_reset();
void draw_help();
void draw_about();
void draw_passkey();
```

- [ ] **Step 6.2: Write `src/menu_panels.cpp`**

```cpp
#include "menu_panels.h"
#include <Arduino.h>
#include "hw_display.h"
#include "ble_bridge.h"
#include "input_fsm.h"
#include "stats.h"
#include "data.h"

// --- Palette -------------------------------------------------------------
// Use fixed values; the buddy character palette is not wired through yet.
static const uint16_t BG       = TFT_BLACK;
static const uint16_t TEXT     = TFT_WHITE;
static const uint16_t TEXT_DIM = TFT_DARKGREY;
static const uint16_t HOT      = 0xFA20;   // red-orange
static const uint16_t GREEN    = TFT_GREEN;

// --- Shared panel helpers -----------------------------------------------
static void _panel_title(const char* t, uint16_t color) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);
  sp.setTextDatum(TL_DATUM);
  sp.setTextSize(2);
  sp.setTextColor(color, BG);
  sp.setCursor(40, 30);
  sp.print(t);
  sp.drawFastHLine(40, 52, 160, TEXT_DIM);
  sp.setTextSize(1);
}

static void _hints(const char* a, const char* b) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextSize(1);
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 190); sp.print(a);
  sp.setCursor(40, 204); sp.print(b);
}

static void _draw_item(int y, bool selected, const char* label, const char* value) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextSize(1);
  sp.setTextColor(selected ? TEXT : TEXT_DIM, BG);
  sp.setCursor(40, y);
  sp.print(selected ? "> " : "  ");
  sp.print(label);
  if (value && *value) {
    sp.setCursor(170, y);
    sp.print(value);
  }
}

// --- Main menu -----------------------------------------------------------
void draw_main_menu() {
  _panel_title("Menu", TEXT);
  const FsmView& v = input_fsm_view();
  static const char* LABELS[7] = {
    "settings", "clock", "turn off", "help", "about", "demo", "close"
  };
  int y = 70;
  for (uint8_t i = 0; i < 7; i++) {
    const char* val = nullptr;
    if (i == 5) val = dataDemo() ? "on" : "off";
    _draw_item(y, v.menuSel == i, LABELS[i], val);
    y += 16;
  }
  _hints("CW/CCW: scroll", "CLICK: select");
  hw_display_sprite().pushSprite(0, 0);
}

// --- Settings ------------------------------------------------------------
void draw_settings() {
  _panel_title("Settings", TEXT);
  const FsmView& v = input_fsm_view();
  Settings& s = settings();
  static const char* LABELS[5] = {
    "brightness", "haptic", "transcript", "reset", "back"
  };
  char buf[8];
  int y = 70;
  for (uint8_t i = 0; i < 5; i++) {
    const char* val = nullptr;
    if (i == 0) { snprintf(buf, sizeof(buf), "%u", s.brightness); val = buf; }
    else if (i == 1) { snprintf(buf, sizeof(buf), "%u", s.haptic); val = buf; }
    else if (i == 2) val = s.hud ? "on" : "off";
    _draw_item(y, v.settingsSel == i, LABELS[i], val);
    y += 16;
  }
  _hints("CW/CCW: scroll", "CLICK: change");
  hw_display_sprite().pushSprite(0, 0);
}

// --- Reset ---------------------------------------------------------------
void draw_reset() {
  _panel_title("RESET", HOT);
  const FsmView& v = input_fsm_view();
  static const char* LABELS[3] = { "delete char", "factory reset", "back" };
  int y = 90;
  uint32_t now = millis();
  for (uint8_t i = 0; i < 3; i++) {
    bool armed = (v.resetConfirmIdx == i) && (now < v.resetConfirmUntil);
    const char* label = armed ? "really?" : LABELS[i];
    TFT_eSprite& sp = hw_display_sprite();
    sp.setTextSize(1);
    uint16_t color = armed ? HOT : (v.resetSel == i ? TEXT : TEXT_DIM);
    sp.setTextColor(color, BG);
    sp.setCursor(40, y);
    sp.print(v.resetSel == i ? "> " : "  ");
    sp.print(label);
    y += 16;
  }
  _hints("CW/CCW: scroll", "CLICK: confirm");
  hw_display_sprite().pushSprite(0, 0);
}

// --- Help ---------------------------------------------------------------
void draw_help() {
  _panel_title("Controls", TEXT);
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextSize(1);
  sp.setTextColor(TEXT_DIM, BG);
  int y = 70;
  sp.setCursor(40, y); sp.print("Turn knob   scroll");             y += 12;
  sp.setCursor(40, y); sp.print("Click       select/toggle");      y += 12;
  sp.setCursor(40, y); sp.print("Long press  menu / back home");   y += 12;
  sp.setCursor(40, y); sp.print("Double      (reserved)");         y += 16;
  sp.drawFastHLine(40, y, 160, TEXT_DIM);                           y += 6;
  sp.setCursor(40, y); sp.print("Home:  long press menu");         y += 12;
  sp.setCursor(40, y); sp.print("Clock: menu > clock");
  _hints("CLICK or LONG", "to return");
  sp.pushSprite(0, 0);
}

// --- About --------------------------------------------------------------
void draw_about() {
  _panel_title("About", TEXT);
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextSize(1);
  sp.setTextColor(TEXT_DIM, BG);
  int y = 70;
  sp.setCursor(40, y); sp.print("claude-desktop");          y += 12;
  sp.setCursor(40, y); sp.print("-buddy  X-Knob");          y += 16;
  sp.drawFastHLine(40, y, 160, TEXT_DIM);                    y += 6;
  sp.setCursor(40, y); sp.print("by Felix Rieseberg");      y += 12;
  sp.setCursor(40, y); sp.print("+ community");             y += 12;
  sp.setCursor(40, y); sp.print("X-Knob port: you");        y += 16;
  sp.setCursor(40, y); sp.print("github:ZinkLu/");          y += 12;
  sp.setCursor(40, y); sp.print("  claude-desktop-buddy");
  _hints("CLICK or LONG", "to return");
  sp.pushSprite(0, 0);
}

// --- Passkey ------------------------------------------------------------
void draw_passkey() {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TEXT_DIM, BG);
  sp.setTextSize(2);
  sp.drawString("BT PAIRING", 120, 50);

  uint32_t pk = blePasskey();
  char b[8];
  snprintf(b, sizeof(b), "%06lu", (unsigned long)pk);
  sp.setTextColor(TEXT, BG);
  sp.setTextSize(5);
  sp.drawString(b, 120, 120);

  sp.setTextSize(1);
  sp.setTextColor(TEXT_DIM, BG);
  sp.drawString("Enter on", 120, 170);
  sp.drawString("your computer", 120, 184);

  sp.setTextDatum(TL_DATUM);
  sp.pushSprite(0, 0);
}
```

- [ ] **Step 6.3: Extend `platformio.ini` for the firmware env**

Find `[env:x-knob]`. Its `build_src_filter` should end with `+<input_fsm.cpp>` (added in Task 4.1). Append `+<menu_panels.cpp>`.

Do NOT add `menu_panels.cpp` to the native env — it depends on `hw_display_sprite()` which is hardware-only.

- [ ] **Step 6.4: Build firmware**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -10
```
Expected: `[SUCCESS]`. Common failures:
- `hw_display_sprite` not declared → confirm `#include "hw_display.h"` is at the top of menu_panels.cpp.
- `settings()` not declared → `#include "stats.h"` must be present. Same for `dataDemo()` from `data.h`.
- `blePasskey` not declared → `#include "ble_bridge.h"`.

- [ ] **Step 6.5: Native test regression check**

```bash
~/.platformio/penv/bin/pio test -e native -f test_input_fsm 2>&1 | tail -8
```
Expected: still 20/20 PASSED (no tests depend on menu_panels).

- [ ] **Step 6.6: Commit**

```bash
git add src/menu_panels.h src/menu_panels.cpp platformio.ini
git commit -m "menu_panels: add render functions for all six panels

Main menu (7 items, demo shows its toggle state), settings (5 items
with value column: brightness 0-4, haptic 0-4, transcript on/off),
reset (3 items with double-confirm red labels), help (control hints),
about (credits), and passkey (full-screen 6-digit).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Wire everything into `main.cpp`

The big integration. Removes Phase 2-C's ad-hoc `DisplayMode` and LONG handling; installs input_fsm + menu_panels as the new control flow.

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 7.1: Read the current `main.cpp`**

```bash
cat src/main.cpp
```
Note specifically: the `DisplayMode` enum near the top, the static `displayMode` variable, the `switch (e)` block handling LONG, the render branch on mode, the new-prompt detection block.

- [ ] **Step 7.2: Remove local `DisplayMode` enum and static variable**

Find and delete:
```cpp
// Phase 2-C: home ↔ clock toggle via LONG press.
enum DisplayMode { DISP_HOME, DISP_CLOCK };
static DisplayMode displayMode = DISP_HOME;
```

`DisplayMode` now lives in `input_fsm.h` with the expanded value set.

- [ ] **Step 7.3: Update includes**

After `#include "clock_face.h"`, add:
```cpp
#include "input_fsm.h"
#include "menu_panels.h"
```

- [ ] **Step 7.4: Add main.cpp-owned settings cycling helpers**

Near the top of the file (after the global state declarations but before `setup()`), add:

```cpp
// Main owns the "cycle this setting to its next value" logic; input_fsm
// just tells us "user clicked brightness / haptic / transcript". This
// keeps all NVS writes centralized here.

static void cycle_brightness(uint8_t) {
  Settings& s = settings();
  s.brightness = (uint8_t)((s.brightness + 1) % 5);
  hw_display_set_brightness((s.brightness + 1) * 20);   // 0..4 -> 20..100
  settingsSave();
}

static void cycle_haptic(uint8_t) {
  Settings& s = settings();
  s.haptic = (uint8_t)((s.haptic + 1) % 5);
  settingsSave();
  hw_motor_click_default();   // fire at new strength so user feels it
}

static void toggle_transcript(bool) {
  Settings& s = settings();
  s.hud = !s.hud;
  settingsSave();
}

static void action_turn_off()      { hw_power_off(); }   // never returns
static void action_toggle_demo()   { dataSetDemo(!dataDemo()); }
static void invalidate_panel_cb()  { /* main loop repaints each frame; no-op */ }

static void action_delete_char() {
  // Wipe /characters/ and reboot. Matches upstream's "delete char" behavior.
  File d = LittleFS.open("/characters");
  if (d && d.isDirectory()) {
    File e;
    while ((e = d.openNextFile())) {
      char path[80];
      snprintf(path, sizeof(path), "/characters/%s", e.name());
      if (e.isDirectory()) {
        File f;
        while ((f = e.openNextFile())) {
          char fp[128];
          snprintf(fp, sizeof(fp), "%s/%s", path, f.name());
          f.close();
          LittleFS.remove(fp);
        }
        e.close();
        LittleFS.rmdir(path);
      } else {
        e.close();
        LittleFS.remove(path);
      }
    }
    d.close();
  }
  delay(300);
  ESP.restart();
}

static void action_factory_reset() {
  _prefs.begin("buddy", false);
  _prefs.clear();
  _prefs.end();
  LittleFS.format();
  bleClearBonds();
  delay(300);
  ESP.restart();
}
```

Note: `_prefs` is defined at file scope in `stats.h` (header-only). `main.cpp` is the only TU that includes `stats.h`, so `_prefs` here is the one and only `Preferences` instance.

`action_delete_char` is the same structure as the upstream reset-panel case in the original `main.cpp` from the M5StickC branch; we bring it back because it's core functionality that Phase 1 dropped.

- [ ] **Step 7.5: Register callbacks in `setup()`**

Near the end of `setup()`, immediately before `startBt();`, add:
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
};
input_fsm_init(&fsm_cb);

// Apply persisted brightness now that stats are loaded.
hw_display_set_brightness((settings().brightness + 1) * 20);
```

- [ ] **Step 7.6: Replace the input switch in `loop()`**

Find the existing `switch (e)` block that handles LONG (and from Phase 2-C the mode toggle). Replace the ENTIRE block beginning at the `switch (e)` line through the closing `}` with:

```cpp
  input_fsm_dispatch(e, now);
  input_fsm_tick(now);

  // Log events for serial debugging.
  switch (e) {
    case EVT_ROT_CW:  Serial.println("CW");     break;
    case EVT_ROT_CCW: Serial.println("CCW");    break;
    case EVT_CLICK:   Serial.println("CLICK");  break;
    case EVT_DOUBLE:  Serial.println("DOUBLE"); break;
    case EVT_LONG:    Serial.println("LONG");   break;
    default: break;
  }
```

Also make sure the `if (e != EVT_NONE) hw_motor_click_default();` line still lives BEFORE the dispatch call so the user feels a bump the instant any event arrives.

- [ ] **Step 7.7: Poll passkey each tick**

Find the existing `blePasskey` reference in main.cpp. Phase 1 `main.cpp` had a loop block like:
```cpp
  static uint32_t lastPasskey = 0;
  uint32_t pk = blePasskey();
  if (pk && pk != lastPasskey) {
    Serial.printf("passkey: %06lu\n", (unsigned long)pk);
  }
  lastPasskey = pk;
```

Expand it to also drive the FSM:
```cpp
  static uint32_t lastPasskey = 0;
  uint32_t pk = blePasskey();
  if (pk && !lastPasskey) {
    Serial.printf("passkey: %06lu\n", (unsigned long)pk);
    input_fsm_on_passkey_change(true);
  } else if (!pk && lastPasskey) {
    input_fsm_on_passkey_change(false);
  }
  lastPasskey = pk;
```

- [ ] **Step 7.8: Replace the render branch**

Find the current render block that looks like (Phase 2-C version):
```cpp
  if (displayMode == DISP_CLOCK) {
    clock_face_tick();
  } else {
    if (buddyMode) {
      buddyTick((uint8_t)activeState);
    } else if (characterLoaded()) {
      ...
    }
    if (inPrompt) drawApproval();
    else          drawHudSimple();
    sp.pushSprite(0, 0);
  }
```

Replace with:
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
      if (inPrompt)            drawApproval();    // approvals always show
      else if (settings().hud) drawHudSimple();   // transcript toggle gates only the passive HUD
      sp.pushSprite(0, 0);
      break;
    }
  }
```

Approvals are always rendered so a `transcript off` setting can't hide a pending permission request; the toggle only suppresses the ambient message HUD.

- [ ] **Step 7.9: Update prompt-interrupt block**

Find the new-prompt detection block. Replace the existing `if (displayMode == DISP_CLOCK) { displayMode = DISP_HOME; buddyInvalidate(); }` logic with:
```cpp
    if (tama.promptId[0]) {
      promptArrivedMs = now;
      input_fsm_force_home_on_prompt();
    }
```

The `input_fsm_force_home_on_prompt` call handles the mode switch and the `buddyInvalidate` callback (it invokes `invalidate_buddy` internally). No need for main.cpp to duplicate that.

- [ ] **Step 7.10: Build**

```bash
~/.platformio/penv/bin/pio run 2>&1 | tail -15
```
Expected: `[SUCCESS]`.

Common failures:
- `_prefs` not declared: ensure `stats.h` is included in main.cpp (it already is from Phase 1).
- `LittleFS` not declared: `#include <LittleFS.h>` (already present).
- `hw_display_set_brightness` signature mismatch: check `hw_display.h` — it's `(uint8_t pct)`, range 0..100.

- [ ] **Step 7.11: Native test regression check**

```bash
~/.platformio/penv/bin/pio test -e native -f test_input_fsm 2>&1 | tail -8
```
Expected: still 20/20 PASSED. Also check the other test suites unchanged:
```bash
~/.platformio/penv/bin/pio test -e native 2>&1 | tail -10
```
Expected: all suites (test_clock_format, test_input_fsm, test_input_fsm previous tests, test_encoder) pass.

- [ ] **Step 7.12: Commit**

```bash
git add src/main.cpp
git commit -m "main: migrate to input_fsm dispatch + menu_panels render

DisplayMode enum moves to input_fsm.h. LONG-in-home now opens the
main menu; clock is a menu entry. Settings CLICK cycles values and
persists immediately via settingsSave. Reset submenu actions (delete
char / factory reset) live in main.cpp to keep NVS + LittleFS +
ESP.restart sequencing obvious. Prompt-interrupt delegates to
input_fsm_force_home_on_prompt which handles the buddy invalidate.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: On-device acceptance

Subagents cannot flash. Human runs through spec §9 scenarios.

- [ ] **Step 8.1: Flash + monitor**

```bash
~/.platformio/penv/bin/pio run -t upload && ~/.platformio/penv/bin/pio device monitor
```

If `Device not configured` on monitor: unplug + replug + re-run monitor only.

- [ ] **Step 8.2: Main menu open/close (spec §9 #1, #8, #9)**

- LONG from home → "Menu" panel with `> settings` highlighted, 7 items visible.
- Rotate CW to scroll to `close` (item 6), CLICK → back to home, capybara visible.
- LONG home → menu → LONG → home.

- [ ] **Step 8.3: Rotate scroll (spec §9 #2)**

From menu, rotate CW through all 7 items. Verify highlight advances, `demo` item shows `on`/`off` value on the right. Rotate CCW back to `settings`.

- [ ] **Step 8.4: Clock via menu (spec §9 #3)**

Menu → scroll to `clock`, CLICK → clock face appears. LONG → home (Phase 2-C behavior unchanged).

- [ ] **Step 8.5: Settings brightness (spec §9 #4)**

Menu → `settings` → `brightness`. CLICK cycles 3→4→0→1→... Observe the backlight changing visibly on each click (brightest at 4, dimmest at 0).

- [ ] **Step 8.6: Settings haptic (spec §9 #5)**

In settings, scroll to `haptic`. CLICK cycles. Immediately after each CLICK, you should feel a motor bump at the new strength. At level 0, no bump. At level 4, strongest.

- [ ] **Step 8.7: Settings transcript (spec §9 #6)**

Scroll to `transcript`, toggle to `off`. Click `back` → home. Bottom HUD line is blank. Menu → settings → transcript on → home. HUD returns.

- [ ] **Step 8.8: Reset double-confirm (spec §9 #7)**

Menu → settings → reset → `delete char`. First CLICK turns label red `really?`. Wait 4 seconds → label reverts to `delete char` (not red). Click again → red `really?`. Within 3 s, click again → device reboots. After reboot, LittleFS `/characters/` is empty (ASCII capybara still renders because it's compiled in, not a GIF pack).

- [ ] **Step 8.9: Factory reset**

Menu → settings → reset → scroll to `factory reset`. Double-click to confirm. Device reboots. Brightness resets to default 3, haptic to 3, transcript to on. Any paired BLE bond is gone — re-pair.

- [ ] **Step 8.10: Prompt interrupt (spec §9 #10)**

LONG into menu. Trigger a Claude Desktop permission prompt. Menu dismisses; approval panel visible on home. Approve. Stays on home.

- [ ] **Step 8.11: Passkey UI (spec §9 #11)**

Unpair the device in Claude Desktop. Trigger a fresh pair attempt. X-Knob screen shows `BT PAIRING` with a 6-digit code. Enter the code on desktop. After pair success, the screen returns to home (whatever mode was active before).

- [ ] **Step 8.12: NVS persistence (spec §9 #12)**

Set brightness to 4. Reboot. Verify brightness is still 4 on boot. Similarly verify haptic + transcript persist.

- [ ] **Step 8.13: Push branch**

```bash
git push -u origin phase-2a
```
Open a PR `phase-2a → main` titled `Phase 2-A: input FSM, menus, passkey UI`. Body can reference the spec doc.

---

## Risks & watchpoints

1. **`main.cpp` is now ~450 lines**. If a later task bumps it past ~800, plan a focused split pass. Not this plan's problem.
2. **Menu render runs every loop iteration**. `pushSprite` at 50 Hz with a static menu is wasteful but not broken (~5 ms per push at 40 MHz SPI). If the frame drops feel sluggish, add a dirty flag driven by the `invalidate_panel` callback. Out of scope for Phase 2-A.
3. **Reset arm expiry rendering**: when the 3 s window lapses and the user is still staring at the reset screen, the red `really?` label stays red until the next panel redraw. `input_fsm_tick` calls `invalidate_panel` on expiry, which in our minimal Phase 2-A main loop is a no-op (render already happens every frame). So rendering catches up on the next frame. Consider this intentional for simplicity.
4. **Help/About LONG vs CLICK**: LONG on help/about returns to menu. CLICK also returns to menu. Two gestures for the same action is redundant but harmless; users can find either. Simplify if you prefer (spec §3.4 says either works).
5. **Callback NULL-safety**: FSM uses `CALL0/CALL1` macros that null-check each callback. Saves the caller from having to register every field; missing callbacks become silent no-ops. Good for native tests where many side effects don't matter.
6. **Passkey blocks force-home**: spec §10 #5 notes the fringe case. Guard is in `input_fsm_force_home_on_prompt`.

---

## When Task 8 passes

Merge the PR. Update `phase2_backlog.md`:
- Move sub-project A from active to completed.
- Note that A5 (species cycling UI) remains open, blocked on sub-project E.
- B and D are now unblocked; either is a natural next.
