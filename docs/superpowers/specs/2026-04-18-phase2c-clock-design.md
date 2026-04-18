# Phase 2-C: Clock Mode Design

Sub-project C of Phase 2 (see Phase 1 spec §7 for the full Phase 2 decomposition; full backlog in memory `phase2_backlog.md`).

Goal: toggle the X-Knob between the normal buddy view and a clock face via long-press, display the current time when BLE has synced it, and preserve the approval-first priority so no prompt is missed.

Source branch: `main` (Phase 1 landed).

---

## 1. Goals & Non-Goals

### Goals

- LONG press (600 ms) toggles between **home** and **clock** display modes.
- Clock face shows: `HH:MM` (big), `:SS` (mid), `Day Mon DD` (small).
- When BLE has never synced time, display `--:--` + `waiting for Claude` placeholder instead of a meaningless date.
- Prompt arrival while in clock forces return to home with the approval panel (same behavior as Phase 1).
- Clock logic lives in its own module `src/clock_face.{h,cpp}`; `main.cpp` only routes the LONG event and owns the mode enum.

### Non-Goals (deferred to future sub-projects)

| Non-goal | Reason | Where it lives |
|---|---|---|
| Tamagotchi-style small buddy rendered under the clock | Scope-scoping sub-project C; animation polish is sub-project D | Sub-project D |
| Friday / weekend / late-night mood animations in clock | Same as above | Sub-project D |
| Auto-return-to-home after N minutes | Dependent on UX feedback; wait until user says clock is "sticky" | Follow-up |
| Landscape / portrait clock orientation toggle | 240×240 circular screen has no orientation | Permanently abandoned |
| NTP / WiFi as an independent time source | Needs credential UI (blocked by menus from sub-project A) | **Sub-project F** |
| Changes to BLE protocol, NVS keys, or hw_* module interfaces | Compatibility with Claude Desktop and the rest of the firmware | Permanent constraint |

### Compatibility anchors (must not change)

- `data.h` behavior for the `time` JSON and the `_rtcValid` flag.
- The approval flow (`drawApproval` + `sendCmd` permission round-trip).
- `hw_input_poll`'s event semantics — only the main-loop response to `EVT_LONG` changes.
- `buddy.cpp` / `character.cpp` rendering paths (they are simply not called when in clock mode).

---

## 2. Clock Face Layout (240×240 round LCD)

Screen regions in the circular visible area (`y ∈ [24, 215]`):

```
        y=70   HH:MM          setTextSize(6), MC_DATUM, x=120
        y=135  :SS            setTextSize(3), MC_DATUM, x=120
        y=180  Day Mon DD     setTextSize(2), MC_DATUM, x=120
```

Valid-time palette (uses current character palette so colors track the installed GIF / ASCII mood):
- `HH:MM` in `palette.text`
- `:SS` in `palette.textDim`
- Date/day in `palette.textDim`

Invalid-time fallback (first time after boot, before any BLE `time` packet):

```
        y=80   --:--          setTextSize(6), MC_DATUM
        y=130  (no time)      setTextSize(1), textDim
        y=150  waiting for    setTextSize(1), textDim
               Claude
```

### Refresh cadence

To avoid redrawing a 240×240 sprite at 50 Hz with the same pixels:

- Track `lastDisplayedMinute`, `lastDisplayedSecond`, `lastDisplayedDay`.
- Each main-loop tick, read the current time with `localtime_r`. Compare against the three tracked fields.
- If any changed → `fillSprite(bg)` + redraw all three rows; push.
- If none changed → skip `pushSprite` entirely (screen keeps last frame).

On mode entry, force a full repaint regardless by setting the last-displayed fields to sentinel values. Same on invalid-time state transitions (valid → invalid on extreme edge cases does not happen, but invalid → valid definitely does when the first time packet arrives — trigger full repaint then).

### What does NOT render in clock mode

- No `buddyTick` or `characterTick` is called. The buddy's canvas region stays blank (or whatever the fillSprite just wrote).
- No HUD / message line. Clock owns the whole sprite.
- Approval panel: only drawn when in home mode. Clock mode exits to home on prompt arrival (see §4), so the approval panel never overlaps the clock.

---

## 3. State Machine

New top-level `DisplayMode` enum in `main.cpp`:

```cpp
enum DisplayMode { DISP_HOME, DISP_CLOCK };
static DisplayMode displayMode = DISP_HOME;
```

Transition rules:

| From | Event | To |
|---|---|---|
| `DISP_HOME` | `EVT_LONG` | `DISP_CLOCK` |
| `DISP_CLOCK` | `EVT_LONG` | `DISP_HOME` |
| any | prompt arrival (new `tama.promptId`) | `DISP_HOME` |

Transition side effects:

- On `DISP_HOME → DISP_CLOCK`: call `clock_face_invalidate()` to force first-frame redraw.
- On `DISP_CLOCK → DISP_HOME`: call `buddyInvalidate()` (or `characterInvalidate()`) so the home view re-renders immediately instead of waiting for its 5 fps tick.
- On prompt-triggered force-home: same as above plus the existing `responseSent = false; approvalChoice = true;` setup.

Input events while in `DISP_CLOCK`:
- `EVT_LONG` → toggle back (above)
- `EVT_ROT_CW/CCW`, `EVT_CLICK`, `EVT_DOUBLE` → **no-op in clock**. (Rotation on the home screen still cycles the persona state as a demo; on clock it does nothing. Click/double will be repurposed by sub-project A.)
- Motor click on every input event stays in place.

---

## 4. Time Handling

### Valid / invalid detection

Extend `data.h` or use its existing `dataRtcValid()`:

```cpp
// data.h already exposes:
inline bool dataRtcValid();   // true once a BLE time packet has landed
```

`clock_face` reads this each render and picks the valid vs invalid layout accordingly.

If the device reboots, `_rtcValid` resets to `false` until the next BLE sync. Accepted regression documented in Phase 1 spec.

### Time read

Use standard C `time()` + `localtime_r`:

```cpp
time_t now;
time(&now);
struct tm lt;
localtime_r(&now, &lt);
```

`tm` yields: `tm_hour`, `tm_min`, `tm_sec`, `tm_mday`, `tm_mon` (0..11), `tm_wday` (0=Sun).

### Date and day formatting

- Days of week: `{"Sun","Mon","Tue","Wed","Thu","Fri","Sat"}[lt.tm_wday]`
- Months: `{"Jan","Feb",...,"Dec"}[lt.tm_mon]`
- Output format: `"%s %s %02d"` → `"Fri Apr 18"`

### Invalid → valid transition

When `dataRtcValid()` first returns true while in clock mode, force a full clock repaint (clear the `waiting for Claude` block, paint the real time). Handled inside `clock_face_tick` by tracking the previous validity.

---

## 5. File and Module Organization

### New files

`src/clock_face.h`:
```cpp
#pragma once
#include <stdint.h>

// Call once on mode entry OR on validity transition to force next tick to
// repaint from scratch (reset the last-displayed cache).
void clock_face_invalidate();

// Call every main-loop iteration while DisplayMode==DISP_CLOCK. Queries
// time(), compares against cached minute/second/day, redraws and pushes
// sprite if any changed. If dataRtcValid() returns false, paints the
// waiting-for-time placeholder instead.
void clock_face_tick();
```

`src/clock_face.cpp`:
- Owns static `lastDisplayedMinute`, `lastDisplayedSecond`, `lastDisplayedDay`, `lastValid` caches.
- Implements the layout in §2.
- Calls `hw_display_sprite()` for render target and `sprite.pushSprite(0, 0)` at the end.
- Depends on `hw_display.h`, `data.h` (for `dataRtcValid`), and standard `<time.h>`.

### Modified files

`src/main.cpp`:
- Add `#include "clock_face.h"`.
- Add the `DisplayMode` enum and static variable.
- In the main loop, check `displayMode`:
  - `DISP_HOME`: existing Phase 1 render path (buddy / character / hud / approval).
  - `DISP_CLOCK`: just `clock_face_tick()`; skip buddy and HUD.
- On `EVT_LONG`: toggle `displayMode` + call the appropriate invalidate.
- On new-prompt detection: force `displayMode = DISP_HOME` + invalidate home renderer.

`platformio.ini`:
- Add `+<clock_face.cpp>` to `build_src_filter`.

No changes to any hw_* module, buddy.cpp, character.cpp, ble_bridge.cpp, data.h, stats.h, xfer.h.

---

## 6. Testing

### Host-side unit tests

Pure logic worth a native test (add under `test/test_clock_format/`):
- Time formatting: given specific `struct tm` inputs, assert the three rendered strings match. Tests protect against off-by-one in month/day-of-week indexing and zero-padding.
- Invalidation edge-case: given a sequence of ticks with changing minutes/seconds/days, assert the `needsRedraw` logic fires only when a tracked field actually changes.

Put the formatting helpers in `clock_face.cpp` behind a small namespace (e.g., `clock_face_internal::fmt_time_fields(const struct tm&, char* hm, char* ss, char* date)`) so native tests can feed synthetic inputs.

### On-device verification (manual, human runs)

1. Boot fresh (not yet BLE-paired): LONG → clock should show `--:--` + `waiting for Claude`.
2. Pair with Claude Desktop so it sends the `time` packet: clock should repaint with real time within 1 second of sync.
3. Let a minute pass: minute digit updates cleanly, no flicker, seconds advance smoothly.
4. LONG again: returns to home with capybara visible immediately (not after a 5fps gate).
5. While in clock, have Claude Desktop request a permission: device should auto-switch to home and show the approval panel with the correct prompt.
6. Resolve the prompt: stays on home (does not auto-return to clock).
7. LONG → clock → observe CW/CCW rotation: no state cycling happens (rotation is a no-op in clock mode).

---

## 7. Open Questions / Risks

- **Font size 6 rendering width**: TFT_eSPI's GLCD font at size=6 has per-character width of ~30 px. `"12:34"` is 5 chars → 150 px wide. With `MC_DATUM` centered on x=120, left edge is x=45, right edge x=195. The visible area at y=70 in the 240×240 circle goes roughly x=30..210 — fits, but barely. If the rendered width overflows in practice, drop to size 5. Verify on first flash.
- **Deep-sleep interaction**: none in Phase 2-C. Display stays backlit in clock mode; the Phase 1 background brightness (50%) continues. Dedicated auto-dim / deep-sleep is a separate follow-up.
- **Timezone correctness**: `data.h` already folds `tz_offset` into the epoch before `settimeofday`, so `localtime_r` reads wall-clock local time. No TZ config needed on device. If Claude Desktop changes TZ during a session (rare), a new `time` packet arrives and everything resyncs.

---

## 8. Phase 2-C Acceptance

Sub-project C is considered complete when:
- All 7 on-device scenarios in §6 pass.
- Native tests for time formatting pass.
- Toggle latency (LONG → visible mode change) is ≤ 200 ms.
- No regression in Phase 1 functionality (capybara still renders on home, approvals still round-trip).
- Final commit tagged or branched for user review before merging into main.
