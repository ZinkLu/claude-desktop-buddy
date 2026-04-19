# Phase 2-B: Info Pages, Pet Mode, HUD History Design

Sub-project B of Phase 2. Adds three features unblocked by A's input FSM:

1. **Info** display mode — 4-page reference / status screen
2. **Pet** display mode — interactive "pet the character" experience leveraging the BLDC motor (the centerpiece of this sub-project and what makes X-Knob's buddy feel alive in a way the M5StickC original can't)
3. **HUD history scrolling** on the home screen

Source branch: cut `phase-2b` from `main` at the start.

---

## 1. Goals & Non-Goals

### Goals

- Home `CLICK` (when no prompt pending) cycles: **home → Pet → Info → home**
- `Pet` mode is interactive. Small-amplitude rotation is detected as **stroking**; the motor responds with a **low-frequency purr**; the character shows hearts and a blissful pose; `mood` stat temporarily boosts
- `Pet` mode has a secondary **stats sub-page** (double-click) that mirrors upstream's full stats readout
- Fast / large-angle rotation in Pet mode is detected as **tickling**; the motor kicks back; the character shows dizzy animation
- Long-press in Pet mode gives a **squish** response (500 ms vibration + squish animation)
- `Info` mode has **4 pages** (`ABOUT`, `CLAUDE`, `SYSTEM`, `CREDITS`); rotation pages forward/back
- Menu `help` and `about` items become shortcuts into specific Info pages (page 0 and page 3)
- On home without a prompt, rotation scrolls HUD transcript history (CW = older, CCW = newer, max 30 lines back). Current offset shown as `-N` in the corner when > 0
- New `hw_motor` APIs for continuous modes: purr, kick, wiggle, pulse-series, vibrate
- Motor safety: purr auto-stops after 3 s without detected stroking, or 30 s of continuous stroking (character shows "fell asleep" with a Z animation)

### Non-Goals

| Non-goal | Where it lives |
|---|---|
| SimpleFOC closed-loop motor control (finer force-feedback) | Sub-project D |
| Actual `nap` gesture (`BTN_LONG_3000`) | Sub-project D |
| Actual `dizzy` trigger outside Pet mode (e.g., `ROT_FAST` on home) | Sub-project D |
| `celebrate` level-up animation | Sub-project D |
| 17 other ASCII species | Sub-project E |
| CJK font | Sub-project E |
| WiFi / NTP | Sub-project F |
| Deep-sleep / auto-dim | Future follow-up |
| Changes to BLE protocol, NVS schema, hw_input / ble_bridge | Permanent constraint |

### Compatibility anchors (unchanged)

- BLE protocol and `ble_bridge.*`
- `input_fsm` public API and FsmCallbacks struct shape (extended, not rewritten)
- Approval flow
- Existing Phase 2-A menus / settings / reset / passkey UI
- Existing Phase 2-C clock mode
- NVS keys (nothing new needed for B)

---

## 2. Display Mode Additions

Extend the existing `DisplayMode` enum in `src/input_fsm.h`:

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
  DISP_PET,          // NEW: interactive pet mode, main sub-page
  DISP_PET_STATS,    // NEW: pet mode, stats sub-page
  DISP_INFO,         // NEW: 4-page info display
};
```

`DISP_HELP` and `DISP_ABOUT` are kept for Phase 2-A compatibility, but main menu `help` / `about` clicks no longer enter them directly — they now enter `DISP_INFO` at a specific page. `DISP_HELP` and `DISP_ABOUT` remain reachable by nothing (dead modes for now), to be removed in a future cleanup pass.

Rationale: keeping the enum values keeps Phase 2-A binaries rebootable without wiping NVS; they just become unreachable paths. A Phase 2-B+1 cleanup can drop them.

New FSM state fields in the internal `FsmView`:

```cpp
struct FsmView {
  DisplayMode mode;
  uint8_t menuSel;
  uint8_t settingsSel;
  uint8_t resetSel;
  uint8_t resetConfirmIdx;
  uint32_t resetConfirmUntil;

  uint8_t infoPage;      // 0..3
  uint8_t hudScroll;     // 0..30, only meaningful in DISP_HOME
};
```

`hudScroll` lives here so it's reachable by `drawHudSimple()` (via main.cpp's view getter) without main.cpp juggling its own state.

---

## 3. Input Routing Extensions

New per-mode event handling (added to `input_fsm_dispatch`):

| Mode | CW | CCW | CLICK | DOUBLE | LONG |
|---|---|---|---|---|---|
| `DISP_HOME` | HUD scroll +1 (older) | HUD scroll −1 (newer) | enter `DISP_PET` (unless `inPrompt`) | (no-op) | open menu (unchanged) |
| `DISP_PET` | forwarded to Pet gesture recognizer | same | enter `DISP_INFO` at page 0 | enter `DISP_PET_STATS` | home |
| `DISP_PET_STATS` | (no-op) | (no-op) | back to `DISP_PET` | back to `DISP_PET` | home |
| `DISP_INFO` | `infoPage = (infoPage + 1) % 4` | `infoPage = (infoPage + 3) % 4` | home | (no-op) | home |

All other modes (menu, settings, reset, clock, passkey) behavior unchanged from Phase 2-A / 2-C.

**Home CLICK cycle**: the cycle is **home → Pet → Info → home**. CLICK in Pet enters Info at page 0 (not back to home); CLICK in Info returns home. That's the three-tap cycle users discover naturally.

**Prompt preempt** (unchanged invariant): any new prompt forces `DISP_HOME` regardless of current mode. Pet purr auto-stops as a side-effect (see §5).

---

## 4. Info Mode (4 Pages)

### Page 0 — ABOUT

```
  claude-desktop-buddy
  X-Knob port
  =====================

  I watch your Claude
  desktop sessions.
  Sleep when idle, wake
  when you start work,
  get impatient when
  approvals pile up.

  Controls:
    turn knob   scroll
    click       select
    long press  menu
  ---------------------
  page 1/4
```

### Page 1 — CLAUDE

Live snapshot of the `TamaState`:

```
  CLAUDE              page 2/4

  sessions     42
  running      2
  waiting      1

  LINK
    via       bt
    ble       encrypted
    last msg  3s
    state     attention
```

Values update every frame. Values read from `tama` via existing accessors or via a new bridge (since `main.cpp` owns `tama` — use the same `extern` bridge pattern we used for Phase 2-A).

### Page 2 — SYSTEM

Merged Device + Bluetooth info:

```
  SYSTEM              page 3/4

  uptime    1h 24m
  heap      142KB
  bright    3/4
  haptic    3/4

  bluetooth
    Claude-AB12
    AB:CD:EF:12:34:56
    linked
```

No battery / current / temp (no AXP on X-Knob). `bright` and `haptic` show the Settings value. BT status: `linked` if `dataBtActive()`, else `discover` if `bleConnected()`, else `off`. MAC from `esp_read_mac(ESP_MAC_BT)`.

### Page 3 — CREDITS

```
  CREDITS             page 4/4

  made by
    Felix Rieseberg

  source
    github.com/
    anthropics/
    claude-desktop-buddy

  X-Knob port
    ZinkLu/
    claude-desktop-buddy

  hardware
    X-Knob
    ESP32-S3
```

### Layout and rendering

Each page rendered by a function in `menu_panels.cpp` or a new `info_pages.cpp`. I propose `info_pages.{h,cpp}` — the existing `menu_panels.cpp` is already getting wide, and Info page rendering involves live data not relevant to the simple menu panels.

```cpp
// src/info_pages.h
void draw_info_page(uint8_t page);  // page = 0..3; called from main loop render branch
```

Each draw function uses `hw_display_sprite()`, `_panel_title(...)` helper (move shared helpers to a common header or duplicate for now — see §9 Risks), `fillSprite(bg)`, `pushSprite` at end.

Page 1 (CLAUDE) redraws every frame because values change. Pages 0 / 2 / 3 could skip redraw once drawn but simpler to redraw unconditionally — the sprite push dominates cost and we're already doing it at 50 Hz on home.

---

## 5. Pet Mode — The Main Event

The only place on this device where the motor acts as an expressive output, not just detent feedback. This section is long on purpose.

### 5.1 Layout

**DISP_PET (main)**: character rendered larger than home (size 3 instead of 2 via `buddySetPeek(false)` + a new helper `buddySetScale(3)`, or simply re-invoke the peek mechanism with a third scale level).

```
        ┌─────────────────┐
        │    pet me       │   y=25, size 1, textDim, fades after 3s
        │                 │
        │    character    │   y=40..180, size 3
        │     (bigger)    │
        │                 │
        │  mood ♥♥♥  lv3  │   y=195, size 1, compact stats footer
        └─────────────────┘
```

Hints (`pet me`) show for first 3 s of mode entry, then fade. Compact stats footer stays always.

**DISP_PET_STATS**: full stats readout (matches upstream's pet-stats page):

```
  mood    ♥♥♥░    (hearts)
  fed     ●●●●●○○○○○  (10 circles, filled = tokens/5K)
  energy  ██░░░   (5 bars)

  Lv 3                      (level pill)

  approved  42
  denied    3
  napped    2h 15m
  tokens    127.4K
  today     8.2K
```

Exact layout mirrors upstream `drawPetStats` with Phase 2-A's font palette.

### 5.2 Gesture recognition (the hard part)

The pet-mode rotation handler in `input_fsm` does NOT update any FSM sel state. Instead it calls a new module `pet_gesture.cpp` that classifies rotation events over a short window.

```cpp
// src/pet_gesture.h
enum PetGesture {
  PGEST_NONE,
  PGEST_STROKE,       // small-amplitude back-and-forth
  PGEST_TICKLE,       // fast, one-direction, large angle
};

void pet_gesture_reset();                              // called on mode entry
PetGesture pet_gesture_step(InputEvent e, uint32_t now_ms);
// Also provide a host-testable pure function:
namespace pet_gesture_internal {
  // Given a recent rotation history buffer, classify the gesture.
}
```

Classification logic (pure, testable):

- Keep a ring buffer of the last 8 rotation events with `{timestamp, direction}` (direction = +1 for CW, -1 for CCW).
- On each rotation event, call classify:
  - If 4+ events in the last 400 ms AND directions alternate at least twice → **STROKE**
  - Else if 5+ events in the last 250 ms AND direction uniform → **TICKLE**
  - Else → **NONE**

Classification fires once per gesture; a "cool-down" of 300 ms prevents double-firing.

### 5.3 Motor responses

New `hw_motor` APIs (in `src/hw_motor.{h,cpp}`):

```cpp
// Start continuous low-frequency oscillation. Main loop must call
// hw_motor_tick() every iteration to drive the pulse cadence.
// level 0..4; 0 is a no-op.
void hw_motor_purr_start(uint8_t level);
void hw_motor_purr_stop();
bool hw_motor_purr_active();

// One-shot directional kick (stronger than click). direction: 0 = forward, 1 = reverse.
void hw_motor_kick(uint8_t direction, uint8_t level);

// One-shot short L-R-L wiggle.
void hw_motor_wiggle();

// Series of N clicks spaced gap_ms apart. Returns immediately — pulses
// are fired by hw_motor_tick over time. At most one series queued at once.
void hw_motor_pulse_series(uint8_t n, uint16_t gap_ms, uint8_t level);

// Continuous vibration for duration_ms. hw_motor_tick drives it.
void hw_motor_vibrate(uint16_t duration_ms, uint8_t level);

// Call every main loop iteration; drives purr / pulse_series / vibrate state machines.
void hw_motor_tick(uint32_t now_ms);
```

Implementation: each continuous-mode API sets internal state (`_purr_active`, `_purr_last_pulse_ms`, `_series_remaining`, etc.). `hw_motor_tick` checks state each call and fires `hw_motor_click(strength)` when timing is due. No FreeRTOS tasks — single-threaded state machine piggybacks on main loop.

Purr pattern: alternate direction every pulse, gap 150 ms, strength = floor(level * 50) with level 1 → 50 (gentle), level 2 → 100, etc. At 150 ms cadence over 30 s that's 200 pulses max — well within motor thermal budget at these low strengths.

### 5.4 Pet mode event flow

```cpp
// Pseudocode of main loop when displayMode == DISP_PET
on_enter_pet() {
  pet_gesture_reset();
  hw_motor_pulse_series(3, 100, settings().haptic);  // greeting
  petHintUntil = now + 3000;
  petStrokeLastMs = 0;
  petStrokeTotalMs = 0;
}

on_event_in_pet(e) {
  if (e == EVT_LONG || e == EVT_CLICK) {
    // exit handled by input_fsm
  }
  if (e == EVT_ROT_CW || e == EVT_ROT_CCW) {
    PetGesture g = pet_gesture_step(e, now);
    if (g == PGEST_STROKE) {
      if (!hw_motor_purr_active()) hw_motor_purr_start(1);
      petStrokeLastMs = now;
      petStrokeTotalMs += 150;  // approximate accumulator
    } else if (g == PGEST_TICKLE) {
      hw_motor_kick(1, settings().haptic + 1);  // reverse direction kick
      triggerDizzy = now;
    }
  }
}

on_tick() {
  // Purr safety: stop if no stroke in 3s or total stroke > 30s
  if (hw_motor_purr_active()) {
    if (now - petStrokeLastMs > 3000) hw_motor_purr_stop();
    else if (petStrokeTotalMs > 30000) {
      hw_motor_purr_stop();
      petFellAsleep = true;   // renderer draws Z's
    }
  }
}

on_long_press_in_pet() {
  // Long press = squish
  hw_motor_vibrate(500, settings().haptic);
  triggerSquish = now;
}
```

`triggerDizzy` / `triggerSquish` / `petFellAsleep` are flags the Pet page renderer reads each frame to overlay animations.

### 5.5 Persona states during Pet mode

The character animation uses existing `buddyTick(state)` with these states:

- Idle pet (no interaction): `P_IDLE`
- Being stroked: **`P_HEART`** — existing heart animation intensified (the buddy renderer already has P_HEART as a state; we reuse it)
- Tickled: `P_DIZZY` for 1 s, then back
- Squished (long press): `P_HEART` with a brief y-shift animation (new tiny effect in main, no buddy code change)
- Fell asleep (purr timeout): `P_SLEEP` + Z overlay (default buddy sleep animation)

No new buddy species code. Just main picks the persona state for each pet event.

### 5.6 Stats footer and sub-page

**Footer (always on main pet page)**: two tiny readouts:
```
  mood ♥♥♥  lv 3
```
`mood` = count of hearts from `statsMoodTier()` (0..4 drawn as filled/empty). `lv` = `stats().level`. No other stats on main page to keep the character hero.

**Stats sub-page (DISP_PET_STATS)**: full upstream-style readout. Implemented in a new `pet_pages.{h,cpp}` with:

```cpp
void draw_pet_main();      // character + hint + footer
void draw_pet_stats();     // full stats grid
```

Both use `hw_display_sprite()`, call `buddyTick` where appropriate.

### 5.7 Entry / exit motor pulses

- Entry (home → pet): `hw_motor_pulse_series(3, 100, haptic_level)` — three quick pulses
- Exit (pet → home or pet → info): `hw_motor_pulse_series(2, 80, haptic_level)` — two quick pulses
- No pulse on pet_stats entry/exit (it's sibling, not boundary of pet experience)

Entry pulses fire from `FsmCallbacks.on_enter_pet` — a new callback we'll add. Exit pulses fire from `on_exit_pet`. Both are optional callbacks (null-safe).

---

## 6. HUD History Scrolling

### 6.1 Gesture

In `DISP_HOME` without an active prompt:
- `EVT_ROT_CW`: `hudScroll = min(hudScroll + 1, 30)`
- `EVT_ROT_CCW`: `hudScroll = (hudScroll > 0) ? hudScroll - 1 : 0`

`hudScroll` is part of `FsmView`. Main's `drawHudSimple()` reads it.

Motor bump is still fired on each rotation (current Phase 2-A behavior).

### 6.2 Rendering

Current `drawHudSimple` shows only the latest `tama.msg` or `tama.lines[last]`. Extend to show up to **3 lines** of wrapped transcript text with `hudScroll` as the offset from newest:

```cpp
void drawHudSimple() {
  // Wrap all tama.lines into a flat display buffer (same helper as upstream).
  // Compute visible window: [nDisp - hudScroll - 3, nDisp - hudScroll).
  // Render 3 lines from that window.
  // If hudScroll > 0, draw "-N" in bottom-right corner.
}
```

Existing `wrapInto` from upstream's main.cpp is public-domain C and can be copied into `main.cpp` if not present. Phase 1 simplified this to one line; we now need the 3-line version.

### 6.3 Prompt priority

Approvals still always render on top of HUD. HUD scroll only affects the non-prompt state. When a prompt arrives, `hudScroll` is NOT reset (leaves user's scroll state intact); after approval clears, if HUD is still visible, it renders at the saved scroll.

### 6.4 Reset on new transcript line

When `tama.lineGen` increments (new transcript line arrived) and `hudScroll == 0`, keep at 0 (user sees newest). If `hudScroll > 0` (user is reading history), LEAVE them alone — don't jump to newest just because a new message arrived. That would be jarring. 

Edge case: `tama.nLines` shifts because the oldest line dropped out of the 8-line ring. `hudScroll` may now point to a different line than before. Acceptable — user scrolls up if they want to keep reading.

---

## 7. File Organization

### New files

- `src/info_pages.h` / `info_pages.cpp` — 4 page drawing functions, read live state via bridges
- `src/pet_pages.h` / `pet_pages.cpp` — pet main + stats drawing
- `src/pet_gesture.h` / `pet_gesture.cpp` — stroke/tickle classifier with native tests
- `test/test_pet_gesture/test_pet_gesture.cpp` — unit tests for the classifier
- `test/test_hud_scroll/test_hud_scroll.cpp` — unit tests for scroll offset logic (simple but worth a tiny suite)

### Modified files

- `src/input_fsm.h` — extend `DisplayMode` enum with `DISP_PET`, `DISP_PET_STATS`, `DISP_INFO`; extend `FsmView` with `infoPage`, `hudScroll`; extend `FsmCallbacks` with `on_enter_pet`, `on_exit_pet`
- `src/input_fsm.cpp` — route home CLICK to enter pet, pet CLICK to enter info, info CLICK to home, info rotation to page, home rotation to HUD scroll, pet rotation forwarded out via a new callback (or inline via direct call — see §9)
- `src/main.cpp` — register new callbacks; expose tama bridge for Info/Pet pages; extend `drawHudSimple` to render multi-line with `hudScroll` offset; route pet gestures; integrate motor tick in main loop; handle the Pet-mode persona state override (dizzy / squish / fell-asleep triggers)
- `src/menu_panels.cpp` — update main menu `help` CLICK to enter `DISP_INFO` at page 0, `about` CLICK to enter page 3. These become thin wrappers over "enter info mode at page X".
- `src/hw_motor.h` / `hw_motor.cpp` — new continuous APIs listed in §5.3 plus `hw_motor_tick`
- `platformio.ini` — extend both env filters with `+<info_pages.cpp>`, `+<pet_pages.cpp>`, `+<pet_gesture.cpp>` (native gets `pet_gesture.cpp` only)

### Untouched

`ble_bridge.*`, `hw_input.*`, `hw_display.*`, `hw_power.*`, `hw_leds.*`, `buddy.*`, `buddies/*.cpp`, `character.*`, `data.h`, `stats.h`, `xfer.h`, `clock_face.*`

---

## 8. Testing

### Host-side unit tests

- **pet_gesture classifier**: fabricate rotation event sequences and assert NONE / STROKE / TICKLE correctly. Covers: no events = NONE, 4 alternating within 400 ms = STROKE, 5 same-direction within 250 ms = TICKLE, borderline cases (3 events = NONE, 6 events spread over 600 ms = NONE).
- **hud scroll math**: pure int arithmetic. Given current offset and rotation direction, assert the new offset, including clamp at 0 and at 30.

Estimated ~12 tests added.

### On-device acceptance

Spec §9 scenarios:

1. Home CLICK → Pet page shows character bigger. Motor gives 3 pulses on entry.
2. In Pet, rotate slowly back-and-forth → motor starts purring (feel the gentle vibration), hearts float above character.
3. Stop rotating for 3 s → purr stops.
4. Resume stroking, keep going 30 s → character switches to sleeping animation.
5. In Pet, turn fast a quarter turn one way → motor kicks back, character does dizzy.
6. In Pet, hold button 600 ms → motor vibrates 500 ms, character does a squish frame.
7. Double-click in Pet → stats sub-page. Hearts count and level match expected.
8. Double-click again → back to pet main.
9. CLICK in Pet → Info page 0 (ABOUT). Two-pulse exit motor.
10. Rotate CW in Info → page 1 (CLAUDE). Shows current session counts if BLE connected.
11. Rotate to page 2 (SYSTEM). BT name and MAC shown. Heap / uptime live.
12. Rotate to page 3 (CREDITS). Single CLICK → home.
13. Menu → `help` → Info page 0 (not a separate HELP screen anymore).
14. Menu → `about` → Info page 3.
15. On home without prompt, rotate CW → HUD shows older line. `-1` appears bottom-right.
16. Rotate CCW → back to newest. Indicator disappears.
17. Trigger a prompt from Claude Desktop while in Pet or Info → device returns to home with approval panel. Purr stops if active.

### Acceptance

Phase 2-B is complete when all native tests pass, firmware builds clean, all 17 on-device scenarios pass, and merging to main does not regress A / C / 1 behavior.

---

## 9. Risks & Open Questions

1. **Motor thermal** — 200 pulses at strength 50 over 30 s. Phase 1 noted "motor shouldn't get hot". This is the first continuous-use pattern. Mitigation: built-in 30 s auto-stop; haptic strength defaults to level 1 for purr (lower than default click strength of level 3). If users report warming, drop purr to level 0 (single-pulse detent every stroke event, no continuous cadence).

2. **Gesture classifier false positives** — slow browsing in Pet might not trigger anything, but could mis-classify as tickle if the user turns the knob steadily. Thresholds (5 events in 250 ms, 4 alternating in 400 ms) are first guesses; may need adjustment on hardware. Classifier is a pure function — tune via native tests without reflashing.

3. **input_fsm + pet_gesture coupling** — input_fsm routes rotation to pet_gesture, but pet_gesture's side effects (motor calls) happen in main, not the FSM. One clean pattern: pet_gesture's step fn returns a gesture enum; FSM sets a flag or calls a callback; main reads and acts. The spec uses the "callback in FSM" pattern for consistency with Phase 2-A's action callbacks, BUT for simplicity the plan may inline pet-mode rotation handling in main instead of FSM, similar to how home approvals are handled in main not FSM. Deferring to plan for the final architecture call.

4. **Shared panel helpers** — `_panel_title`, `_hints`, `_draw_item` currently live in `menu_panels.cpp` as `static`. Info and Pet rendering wants similar helpers. Options: duplicate (cheap, local clarity), move to a shared `panel_helpers.{h,cpp}`, or keep duplicating in each file for now. Plan decides.

5. **`buddySetScale(3)`** — current buddy.cpp has scale 1 and 2 (peek vs home). Pet mode wants scale 3 for a bigger character. Easy extension but verify the 5-line body (up to 240px tall at scale 3) fits inside y=40..180 region without overflow. If the math doesn't work, fall back to scale 2 with specific vertical offset.

6. **Multi-line HUD text wrapping** — need to port upstream's `wrapInto` helper into main.cpp. ~30 lines of C, zero hardware dependencies. If it bloats main.cpp further, consider extracting a `text_wrap.{h,cpp}` module.

7. **DISP_HELP / DISP_ABOUT dead code** — kept in enum for binary compatibility but no longer reachable. Plan to remove in a follow-up cleanup.

---

## 10. Phase 2-B Acceptance Summary

Delivered:
- Info mode with 4 pages, reachable via home CLICK cycle or menu `help`/`about` shortcuts
- Interactive Pet mode with motor-driven stroke purr, tickle kick, squish vibration, stats sub-page, and fall-asleep safety
- HUD history scrolling on home, 3 lines visible, up to 30 lines back
- New `hw_motor` continuous-mode APIs (purr / kick / wiggle / pulse_series / vibrate / tick)
- New `pet_gesture` classifier with native tests
- Updated menu shortcuts for help and about

Out of scope and deferred:
- SimpleFOC closed-loop motor (sub-project D)
- nap / dizzy / celebrate global gestures (sub-project D)
- Additional species, CJK font (sub-project E)
- WiFi + NTP (sub-project F)
