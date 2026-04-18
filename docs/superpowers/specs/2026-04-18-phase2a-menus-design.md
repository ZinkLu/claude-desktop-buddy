# Phase 2-A: Input FSM, Menus, and Passkey UI Design

Sub-project A of Phase 2. Implements the full interactive shell for X-Knob: context-sensitive input routing, three-level menu panels (main / settings / reset), and a dedicated full-screen UI for BLE pairing passkeys. A5 (ASCII species cycling UI) is deferred to sub-project E where the 17 additional species come back.

Source branch: cut `phase-2a` from `main` at the start.

---

## 1. Goals & Non-Goals

### Goals

- LONG-press becomes a **context-sensitive mode toggle**, replacing Phase 2-C's direct LONG=clock.
- Full main menu with 7 items: `settings`, `clock`, `turn off`, `help`, `about`, `demo`, `close`.
- Full settings menu with 5 items: `brightness`, `haptic`, `transcript`, `reset`, `back`.
- Reset submenu with 3 items: `delete char`, `factory reset`, `back`, using double-confirm ("really?" on first click, execute on second within 3 s).
- Full-screen passkey UI that appears automatically whenever `blePasskey()` returns non-zero (i.e., during BLE pairing) and disappears on auth complete or disconnect.
- Settings persist to NVS: `brightness`, `haptic`, `transcript` (HUD on/off).
- Clock mode moves from direct-LONG gesture to a menu entry; all existing Phase 2-C behavior is preserved otherwise.
- Help and About render static single-screen text (Info multi-page system is sub-project B).

### Non-Goals

| Non-goal | Reason / Where it lives |
|---|---|
| ASCII species cycling UI (`ascii pet` settings item) | Only 1 species right now (capybara). Deferred to sub-project E. |
| Info pages multi-screen (6 pages) | Sub-project B; help/about here are simple single-screen replacements. |
| `sound` setting | No buzzer on X-Knob. Replaced by `haptic` strength setting. |
| `bluetooth` / `wifi` / `led` / `clock rot` settings | No functional effect on X-Knob (BLE always on, no WiFi until F, LEDs not visible, round screen). |
| Deep sleep UI / screen-off timeout | Separate concern; current Phase 1 leaves backlight always on. |
| New input events | All gestures stay within Phase 1's set: `CW/CCW/CLICK/DOUBLE/LONG`. |
| Changes to BLE, NVS schema keys outside the three new settings, hw_* modules, buddy/character renderers | Out of scope. |

### Compatibility anchors (must not change)

- BLE protocol and `ble_bridge.*` API.
- Approval flow semantics (`drawApproval` / `sendCmd` / `statsOnApproval` / `statsOnDenial`).
- Existing NVS keys from `stats.h`: `nap`, `appr`, `deny`, `vidx`, `vcnt`, `lvl`, `tok`, `vel`, `s_snd`, `s_bt`, `s_wifi`, `s_led`, `s_hud`, `s_crot`, `petname`, `owner`, `species`. We add three new keys (see §6) and leave the rest untouched.
- Phase 2-C clock_face module API and rendering behavior.
- `hw_input_poll` event set.

---

## 2. Input State Machine

### Top-level display modes

```cpp
enum DisplayMode {
  DISP_HOME,       // existing — buddy/character + HUD + approval
  DISP_CLOCK,      // existing — clock_face
  DISP_MENU,       // NEW — main menu panel
  DISP_SETTINGS,   // NEW — settings submenu panel
  DISP_RESET,      // NEW — reset submenu panel
  DISP_PASSKEY,    // NEW — full-screen pairing UI (overlay; see §5)
};
```

`DISP_PASSKEY` is special: it is not user-entered. It is forced on whenever `blePasskey() != 0`, and auto-forced off when passkey becomes zero (auth complete or disconnect). It overrides any other mode visually, and input events while passkey is shown are ignored (pairing proceeds on the desktop side, not the device).

### Event routing table

| Mode | CW | CCW | CLICK | DOUBLE | LONG |
|---|---|---|---|---|---|
| `DISP_HOME` | (no-op; reserved for B) | (no-op) | approve prompt (if `inPrompt`) | (no-op) | → `DISP_MENU` |
| `DISP_CLOCK` | (no-op; motor bump only) | (no-op) | (no-op) | (no-op) | → `DISP_HOME` |
| `DISP_MENU` | next item | prev item | execute item (see §3) | (no-op) | close menu → `DISP_HOME` |
| `DISP_SETTINGS` | next item | prev item | execute item (see §4) | (no-op) | → `DISP_HOME` (straight out, not back to `DISP_MENU`) |
| `DISP_RESET` | next item | prev item | execute item with double-confirm | (no-op) | → `DISP_HOME` |
| `DISP_PASSKEY` | ignored | ignored | ignored | ignored | ignored |

Rationale:
- **LONG from menu goes straight to home** (not back one level). The `back` / `close` item in each menu handles "up one level" navigation. LONG is the global "get out" escape.
- **DOUBLE is unused** across all menu modes. Reserved for sub-project D (where it might become "cancel / undo" or similar).
- **Rotation is the only scrolling input**. CW always moves toward the next item (or increases a value when a settings item cycles through enum values — see §4).

### Prompt preemption (invariant across the whole state machine)

When a new non-empty `tama.promptId` arrives, force `displayMode = DISP_HOME`, invalidate the home renderer, and reset `approvalChoice = true` / `responseSent = false`. This preempts any menu, settings, reset, or clock mode. Matches Phase 1 and Phase 2-C behavior; extended to the new menu modes.

Passkey UI is the only exception — if a passkey is active, the overlay stays (pairing takes priority, and no prompt can arrive during unpaired state anyway).

---

## 3. Main Menu (DISP_MENU)

**Items (top to bottom, wraps both ways via rotation):**

| # | Label | CLICK action |
|---|---|---|
| 0 | `settings` | → `DISP_SETTINGS`, `settingsSel = 0` |
| 1 | `clock` | → `DISP_CLOCK`; call `clock_face_invalidate()` |
| 2 | `turn off` | call `hw_power_off()` (never returns) |
| 3 | `help` | → `DISP_HELP` (static screen — see §3.4) |
| 4 | `about` | → `DISP_ABOUT` (static screen — see §3.4) |
| 5 | `demo` | `dataSetDemo(!dataDemo())`; menu stays open; label shows current state |
| 6 | `close` | → `DISP_HOME` |

7 items total.

### 3.1 Layout (240×240 round LCD)

```
        ┌────────────────┐
        │                │  y=30 (top visible edge of circle is ~y=24)
        │ Menu           │  y=40, size 2, left-aligned, x=40
        │                │
        │  > settings    │  y=70, size 1, current selection highlighted
        │    clock       │  y=86
        │    turn off    │  y=102
        │    help        │  y=118
        │    about       │  y=134
        │    demo    on  │  y=150 (demo shows current toggle state)
        │    close       │  y=166
        │                │
        │ CW/CCW: scroll │  y=190, size 1, textDim (hint line)
        │ CLICK: select  │  y=204
        │                │  (y=215 is lower visible edge)
        └────────────────┘
```

- Panel is painted on a full-sprite `fillSprite(bg)` background.
- Selected item is rendered in `palette.text` with `"> "` prefix.
- Non-selected items rendered in `palette.textDim` with `"  "` prefix.
- Item width allows for right-aligned value where applicable (only `demo` has a value in this menu).

### 3.2 Demo label state

`demo` item shows `on` or `off` at the right side (`x=200, same y`) based on current `dataDemo()`. Click toggles and redraws in place.

### 3.3 Menu invalidate and render cadence

- Menu render is immediate on mode entry and on every CW/CCW/CLICK event.
- No per-frame redraw — menu is static unless user input arrived. Saves sprite work.
- On passkey overlay activation/deactivation (from outside the menu), the menu state is preserved but the overlay takes priority.

### 3.4 help and about static screens

Two new top-level modes: `DISP_HELP` and `DISP_ABOUT`. Each is a single static screen. CLICK or LONG on either returns to `DISP_MENU` (back to where user came from).

`DISP_HELP` content:
```
  Controls
  --------
  Turn knob    scroll
  Click        select/toggle
  Long press   open/close menu
               (back to home)
  Double       — (reserved)
  ------------------
  Home: long press
    to open menu
  Clock: menu > clock
```

`DISP_ABOUT` content:
```
  claude-desktop
  -buddy  X-Knob
  =================
  by Felix Rieseberg
  + community
  X-Knob port: you
  -----------------
  git: ZinkLu/
    claude-desktop
    -buddy
```

Both are size-1 text, centered horizontally, fitted within y=30..200. Exact spacing in the implementation plan.

### 3.5 Adding help/about to DisplayMode

These are listed separately from `DISP_MENU` to keep state transitions clean. Their LONG and CLICK behavior: return to `DISP_MENU`.

---

## 4. Settings Menu (DISP_SETTINGS)

**Items:**

| # | Label | Value column | CLICK action |
|---|---|---|---|
| 0 | `brightness` | `0..4` | `brightLevel = (brightLevel + 1) % 5`; call `hw_display_set_brightness(brightLevel * 20 + 20)` |
| 1 | `haptic` | `0..4` | `hapticLevel = (hapticLevel + 1) % 5`; fire `hw_motor_click(strengthTable[hapticLevel])` so the user feels the new strength on the spot |
| 2 | `transcript` | `on/off` | toggle `settings().hud`; calls `settingsSave()` |
| 3 | `reset` | (submenu) | → `DISP_RESET`, `resetSel = 0`, clear `resetConfirmIdx` |
| 4 | `back` | — | → `DISP_HOME` |

5 items. (Note: `back` goes to home, not to `DISP_MENU`. Simpler for the user; fewer nested escapes.)

### 4.1 Haptic strength table

```cpp
static const uint8_t HAPTIC_STRENGTH[5] = { 0, 40, 80, 120, 200 };
```

- `0` means skip `hw_motor_click` entirely (don't fire PWM).
- `3` (120) is the Phase 1 default.
- `4` (200) is noticeable but not jarring.

All motor calls in the firmware use `hapticLevel`-indexed strength instead of a hardcoded `120`. Centralize: add `hw_motor_click_default()` that reads the current level from `settings().haptic`.

### 4.2 Brightness table

Matches Phase 1 convention: level 0..4 maps to PWM 20%..100% in 20% steps.

```cpp
static const uint8_t BRIGHT_PCT[5] = { 20, 40, 60, 80, 100 };
```

Applied via `hw_display_set_brightness(BRIGHT_PCT[brightLevel])`.

### 4.3 Transcript toggle

When `transcript == off`, `drawHudSimple()` on home becomes a no-op (the space is just blank). Approval UI is unaffected — approvals always show regardless of transcript setting.

### 4.4 Layout

Same panel layout as main menu (§3.1). Right-aligned value column at `x=160`:

```
        Settings
        ------------
      > brightness   3
        haptic       3
        transcript  on
        reset
        back
        ------------
        CW/CCW: scroll
        CLICK: change
```

The hint at the bottom is important — "change" not "select", because most items modify a value in place rather than navigating.

---

## 5. Reset Menu (DISP_RESET)

**Items:**

| # | Label | First-click | Second-click (within 3s) |
|---|---|---|---|
| 0 | `delete char` | label → `really?` (red) | wipe `/characters/`, reboot |
| 1 | `factory reset` | label → `really?` (red) | NVS `buddy` namespace clear + `LittleFS.format()` + `bleClearBonds()`, reboot |
| 2 | `back` | → `DISP_SETTINGS` | — |

### 5.1 Double-confirm semantics

State kept in two variables:
```cpp
static uint8_t  resetConfirmIdx = 0xFF;
static uint32_t resetConfirmUntil = 0;
```

On first CLICK of an action item (0 or 1):
- `resetConfirmIdx = idx`
- `resetConfirmUntil = millis() + 3000`
- Label swaps to `really?` in red, but otherwise menu stays open
- Motor bump at haptic level clamped to `min(settings().haptic + 1, 4)` (one step stronger than the user's current setting, capped at max) to signal "you're about to do something destructive"

On second CLICK of the same item while armed (i.e., `resetConfirmIdx == idx && millis() < resetConfirmUntil`):
- Execute the action (wipe chars or factory reset)
- Action blocks until `ESP.restart()` — no recovery path to abort once second click lands

Scrolling to another item clears the arm: `resetConfirmIdx = 0xFF`.

After 3 s elapses without a second click, `resetConfirmIdx = 0xFF` silently. Label reverts to its normal text on next render.

### 5.2 Actions

- `delete char`: walks `/characters/`, removes all files and the directory itself. Per Phase 1 pattern: restart device so GIF state cleans up.
- `factory reset`: opens `Preferences` `buddy` namespace, `clear()`, closes, `LittleFS.format()`, `bleClearBonds()`, `delay(300)`, `ESP.restart()`. All NVS values (stats, settings, owner, petname, species, brightness, haptic, transcript) reset to defaults on next boot.

### 5.3 Layout

Same panel style; red border instead of text-dim border to emphasize destructiveness.

```
        RESET  (red)
        ------------
      > delete char
        factory reset
        back
        ------------
        CW/CCW: scroll
        CLICK: confirm
```

---

## 6. New NVS Keys

Three new keys under the `buddy` Preferences namespace:

| Key | Type | Default | Read via | Written via |
|---|---|---|---|---|
| `s_bright` | uint8 | `3` | `settingsLoad()` | `settingsSave()` on change |
| `s_haptic` | uint8 | `3` | `settingsLoad()` | `settingsSave()` on change |
| `s_hud` | bool | `true` | already exists (maps to `transcript` setting) | already exists |

`s_hud` already exists from upstream. New keys are only `s_bright` and `s_haptic`.

Extend `Settings` struct:
```cpp
struct Settings {
  bool     sound;       // upstream, unused on X-Knob; kept for NVS compat
  bool     bt;          // upstream, unused
  bool     wifi;        // upstream, unused
  bool     led;         // upstream, unused
  bool     hud;         // used: transcript toggle
  uint8_t  clockRot;    // upstream, unused
  uint8_t  brightness;  // NEW — 0..4
  uint8_t  haptic;      // NEW — 0..4
};
```

Do not delete the upstream-only fields. Keeps NVS reads stable if someone upgrades firmware without wiping; factory reset handles a clean slate.

---

## 7. Passkey UI (DISP_PASSKEY)

### 7.1 Trigger logic

`ble_bridge.h` already provides `uint32_t blePasskey()`. Main loop polls it each iteration. Transition:

- `blePasskey() != 0 && displayMode != DISP_PASSKEY` → save `previousMode = displayMode`, set `displayMode = DISP_PASSKEY`, invalidate the passkey renderer so the first frame paints immediately.
- `blePasskey() == 0 && displayMode == DISP_PASSKEY` → restore `displayMode = previousMode`, invalidate that mode's renderer.

### 7.2 Layout

```
        BT PAIRING
        ==========


        1 2 3 4 5 6     <- 6 big digits, centered
        (size 5)


        Enter on
        your computer
```

- Full-sprite `fillSprite(palette.bg)`.
- `BT PAIRING` top, size 2, textDim color.
- Passkey digits at y=100..140, size 5, `palette.text` color.
- Two-line instruction at y=170..190, size 1, textDim.

Digits are drawn one at a time with `setTextPadding` or pre-formatted as `"%06lu"` via snprintf.

### 7.3 Motor bump on passkey arrival

Single moderate bump (haptic level 3) when passkey first appears — draws the user's attention to the screen.

### 7.4 Input ignored

All input events are consumed silently while `DISP_PASSKEY` is active. Rationale: pairing input happens on the desktop, not the device. The device user shouldn't accidentally enter menu / clock / etc. during pairing.

---

## 8. File and Module Organization

### New files

- `src/input_fsm.{h,cpp}` — the context-sensitive input routing logic. Exposes:
  ```cpp
  // Internal state lives in main.cpp (displayMode, menuSel, etc.). This
  // module is the routing function only; all state is passed in.
  void input_fsm_dispatch(InputEvent e);
  ```
  Rationale for extracting: keeps `main.cpp` from bloating into an unreadable switch tree. The dispatcher calls back into main-owned functions (`openMenu`, `menuConfirm`, etc.) rather than owning state itself.
- `src/menu_panels.{h,cpp}` — drawing functions for main menu, settings, reset, help, about, passkey. Each is a pure render function that reads state owned by main.cpp and writes to the shared sprite.
  ```cpp
  void drawMainMenu();
  void drawSettings();
  void drawReset();
  void drawHelp();
  void drawAbout();
  void drawPasskey();
  ```

### Modified files

- `src/main.cpp` — add new `DisplayMode` values (`DISP_MENU`, `DISP_SETTINGS`, `DISP_RESET`, `DISP_HELP`, `DISP_ABOUT`, `DISP_PASSKEY`), state variables (`menuSel`, `settingsSel`, `resetSel`, `resetConfirmIdx`, `resetConfirmUntil`, `previousModeForPasskey`), move existing event routing into calls to `input_fsm_dispatch`, branch render path on all modes.
- `src/stats.h` — extend `Settings` struct with `brightness`, `haptic` fields; extend `settingsLoad` / `settingsSave` to read/write `s_bright` and `s_haptic`; set defaults.
- `src/hw_motor.h` + `hw_motor.cpp` — add `void hw_motor_click_default()` that reads the current haptic level from settings and calls `hw_motor_click(HAPTIC_STRENGTH[level])`. All existing call sites in `main.cpp` that used `hw_motor_click(120)` migrate to `hw_motor_click_default()`.
- `src/hw_display.cpp` — no code change, but add a note that brightness level 0 still maps to 20% not 0% (screen-off is handled by a separate future feature, not by setting level 0).
- `platformio.ini` — extend both env filters with `+<input_fsm.cpp>` and `+<menu_panels.cpp>`.

### Untouched

`ble_bridge.*`, `hw_input.*`, `hw_power.*`, `hw_leds.*`, `buddy.*`, `buddies/*.cpp`, `character.*`, `data.h`, `xfer.h`, `clock_face.*`.

---

## 9. Testing

### Host-side unit tests

Pure logic worth a native test (new `test/test_input_fsm_dispatch/`):

- Given `displayMode = DISP_HOME`, `LONG` event → transitions to `DISP_MENU`.
- Given `DISP_MENU` with `menuSel = 2` (turn off), `CW` event → `menuSel = 3`.
- Given `DISP_MENU` with `menuSel = 0`, `CCW` event → `menuSel = 6` (wraps).
- Given `DISP_SETTINGS` with `settingsSel = 0` (brightness) value 4, `CLICK` → brightness becomes 0 (wraps).
- Given `DISP_RESET` with `resetSel = 0`, first `CLICK` → `resetConfirmIdx = 0`, second `CLICK` within 3 s → action fires (mock the ESP.restart).
- Given `DISP_RESET` with arm state, `CW` event → `resetConfirmIdx` clears (arm is cancelled).
- Given any mode except `DISP_PASSKEY`, passkey activation → transitions to `DISP_PASSKEY` and stores previous mode.

To keep tests hardware-independent, `input_fsm_dispatch` is written to call out via function pointers or conditional-compiled mock stubs (`#ifdef HOST_TESTS`). Detailed technique decided in the plan; spec keeps it at this level.

### On-device manual verification

1. Boot home; LONG → main menu appears with `> settings` highlighted.
2. Rotate CW through all 7 items; labels highlight correctly, `demo` shows its toggle state.
3. From menu, CLICK on `clock` → clock face shows; LONG → back to home (old Phase 2-C LONG-from-clock behavior still works).
4. From menu, scroll to `settings`, CLICK → settings panel; scroll; CLICK `brightness` → screen visibly dims/brightens, label increments.
5. Settings → `haptic`; CLICK cycles; motor click fires at new strength on each CLICK.
6. Settings → `transcript` off; back to home (via `back` item); HUD line is blank. Toggle back on; HUD returns.
7. Settings → `reset` → reset panel. CLICK `delete char` → label turns red with `really?`. Wait 4 s; label reverts. CLICK again; label turns red. CLICK again within 3 s; device reboots; `/characters/` is empty on next boot.
8. From home, LONG → menu → `close` → home; verify no residual menu pixels.
9. From home, LONG → menu → LONG → home (verify LONG-from-menu escape).
10. Trigger a Claude Desktop prompt while in menu → menu dismisses; approval panel appears on home.
11. Trigger BLE pairing from a fresh device → passkey UI takes over any current mode; 6 digits visible; enter the code on desktop; passkey UI clears; previous mode restored.
12. NVS persistence: change brightness to 4, reboot, brightness is still 4.

### Acceptance

Sub-project A is complete when all 12 scenarios pass, all native tests pass, firmware builds clean, and merging to main does not regress Phase 1 or Phase 2-C behavior.

---

## 10. Risks & Open Questions

1. **`main.cpp` is growing unbounded**. Phase 2-A adds another ~200 lines of state + dispatch. The `input_fsm` and `menu_panels` extractions help; future sub-projects should continue this pattern. If main.cpp tips over 800 lines it is worth a dedicated split pass (not in this plan).
2. **Text layout at size 1 fitting within the round LCD**. Each menu item is one line at size 1 (~8 px tall). 7 items at y-step 16 = 112 px vertical. Fits inside the circle's safe region (y=24..216). If longer menus ever show up, we may need scrolling; not a concern for 7/5/3-item menus.
3. **`ESP.restart()` loses uncommitted settings changes**. `statsSave()` is called from each settings toggle before any restart path, so all changes are already persisted at the moment of reset.
4. **Haptic level 0** disables motor feedback entirely. Users might complain they can't tell rotation detents apart on the screen. Mitigation: level 0 is opt-in; the default is 3.
5. **Passkey overlay + prompt race**: in practice, Claude Desktop does not send permission prompts during an unpaired BLE session. Passkey and prompt can't co-occur. If they ever do, passkey wins (per §2). Accepting this trade-off — it's a fringe case and the user can always resolve pairing and then see the prompt.

---

## 11. Phase 2-A Acceptance Summary

Delivered: input FSM (new top-level modes), main menu, settings, reset, passkey UI. `hw_motor_click_default()` centralizes haptic strength. Three new NVS keys persist user preferences. Clock migrates from LONG-direct to menu entry. Phase 1 and 2-C features unchanged in their existing code paths.

Out of scope and deferred: ASCII species UI (sub-project E), Info multi-page system (sub-project B), WiFi settings (sub-project F), deep-sleep / screen-off (future).
