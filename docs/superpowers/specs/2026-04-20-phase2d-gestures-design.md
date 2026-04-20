# Phase 2-D: Gestures + Effects Design (D2 + D3)

Sub-project D (part 2/4) of Phase 2. Implements two gestures and one animation:

1. **D2-A: Manual Nap gesture** (BTN_LONG_3000) — progressive long-press on home
2. **D2-B: Edge hard-bump** — menu/settings/reset boundary haptic feedback
3. **D3: Level-up celebration** — celebrate animation + strong motor feedback

Source branch: `phase-2d` cut from `main`.

---

## 1. Goals & Non-Goals

### Goals

- Home mode progressive long-press: 600ms enters confirmation, 600-3000ms release = menu, 3000ms = manual nap
- Nap state dims screen, shows sleep animation, pauses normal UI; any input wakes it
- Wake from nap restores full energy (5/5) and records nap duration to stats
- Menu/settings/reset rotation at boundaries triggers hard motor bump
- Level-up detected via `statsPollLevelUp()` triggers celebrate state + motor wiggle

### Non-Goals

| Non-goal | Reason |
|---|---|
| ROT_FAST global dizzy | User opted out; deferred |
| SimpleFOC closed-loop | D1 scope; deferred |
| Clock enrichment (D4) | Separate sub-project |
| Deep-sleep power save | G scope; deferred |

---

## 2. Manual Nap (D2-A)

### Interaction Design

Progressive long-press on home screen:

```
0ms      600ms           3000ms
 |----------|----------------|
   wait    confirmation      nap
           mode active
```

- **0-600ms**: normal long-press detection (unchanged)
- **600ms reached**: enter `LONG_CONFIRM` state
  - Show hint: `"松手 = 菜单 | 继续 = 休眠"` (bottom of screen)
  - Motor: single soft pulse (strength 1) to acknowledge
- **600-3000ms release**: execute normal LONG → menu
- **3000ms reached**: trigger manual nap
  - Enter `NAP_MANUAL` state
  - Dim screen to 10% brightness
  - Render sleep animation (reuse P_SLEEP)
  - Record nap start time
  - Pause all normal UI except wake detection

### Nap State Behavior

```cpp
// In main loop
if (manualNapping) {
  // Any input wakes
  if (e != EVT_NONE) {
    manualNapping = false;
    statsOnNapEnd((now - napStartMs) / 1000);
    statsOnWake();  // restore energy to 5/5
    hw_display_set_brightness((settings().brightness + 1) * 20);
    // Continue to process the waking event normally
  }
  // Skip normal rendering
  delay(50);  // slower poll while napping
  continue;
}
```

### Visual Design

- Screen dims to 10% brightness (PWM level 1)
- ASCII character renders in P_SLEEP state (eyes closed, Z particles)
- Wake hint optional: `"转动唤醒"` (fades after 3s)

### Motor Feedback

| Event | Motor Action |
|---|---|
| 600ms reached (confirmation mode) | Single soft pulse (strength 1) |
| 3000ms reached (nap triggered) | Two pulses (strength 2) |
| Wake from nap | Single pulse (strength 2) |

---

## 3. Edge Hard-bump (D2-B)

### Behavior

In Menu / Settings / Reset modes, when rotation reaches boundary:
- Continue same-direction rotation
- Trigger `on_scroll_edge` callback (already exists from Phase 2-B)
- Motor: 2-pulse hard bump (reuse existing `hw_motor_pulse_series(2, 50, 4)`)

### FSM Integration

```cpp
// In input_fsm.cpp for DISP_MENU, DISP_SETTINGS, DISP_RESET
if (e == EVT_ROT_CW) {
  uint8_t max = (mode == DISP_MENU) ? MENU_N : (mode == DISP_SETTINGS) ? SETTINGS_N : RESET_N;
  if (_v.menuSel == max - 1) {  // at bottom
    CALL1(on_scroll_edge, true);  // bump
  } else {
    _v.menuSel++;
    CALL0(invalidate_panel);
  }
}
// Same for CCW at top
```

### Visual

- No visual change at boundary
- Bump is purely haptic feedback

---

## 4. Level-up Celebration (D3)

### Trigger Detection

In main loop, after `dataPoll(&tama)`:

```cpp
if (statsPollLevelUp()) {
  // Trigger one-shot celebrate
  triggerCelebrate();
}
```

### Celebrate State

```cpp
void triggerCelebrate() {
  celebrateUntil = millis() + 3000;  // 3 seconds
  activeState = P_CELEBRATE;
  // Motor: wiggle + pulse series
  hw_motor_wiggle();
  hw_motor_pulse_series(3, 100, settings().haptic);
}
```

### Animation

- Reuse existing `P_CELEBRATE` state (confetti + bouncing, upstream animation)
- Runs for 3 seconds
- Prompt arrival preempts: if new prompt arrives during celebrate, force home immediately

### Motor Pattern

Sequence (total ~400ms):
1. `hw_motor_wiggle()` — L-R-L wiggle (~180ms)
2. Pause 50ms
3. `hw_motor_pulse_series(3, 100, level)` — 3 pulses at gap 100ms

---

## 5. File Changes

### Modified

- `src/main.cpp`:
  - Add `manualNapping` state tracking
  - Add `longPressConfirmMode` state
  - Add `napStartMs` timestamp
  - Modify button handling for progressive long-press (home mode only)
  - Add nap wake detection
  - Add celebrate trigger logic
  - Add energy restore on wake

- `src/input_fsm.h`:
  - Expose `isAtMenuBoundary()` helper if needed

- `src/input_fsm.cpp`:
  - Add boundary check before scroll in menu/settings/reset
  - Fire `on_scroll_edge` when at boundary and continuing to scroll

### No New Files

This sub-project is small enough to modify existing files without adding new modules.

---

## 6. Testing Plan

### On-Device Scenarios

1. **Progressive long-press 600ms**: feel soft pulse, see hint, release → menu opens
2. **Progressive long-press 3000ms**: feel 2-pulse, screen dims, sleep animation
3. **Wake from nap**: rotate knob → screen brightens, energy shows 5/5
4. **Nap stats**: check info page shows nap duration increased
5. **Menu edge bump**: at last menu item, continue rotating CW → feel hard bump
6. **Settings edge bump**: same at settings bottom
7. **Reset edge bump**: same at reset bottom
8. **Level-up celebrate**: mock token increase, see celebrate animation + feel motor
9. **Celebrate preempt**: trigger prompt during celebrate → home immediately

### Build Verification

```bash
~/.platformio/penv/bin/pio run -e x-knob
~/.platformio/penv/bin/pio test -e native
```

---

## 7. Acceptance

D2+D3 complete when:
- [ ] All 9 on-device scenarios pass
- [ ] Native tests pass
- [ ] Firmware builds clean
- [ ] No regression in A/B/C behavior

---

## 8. Risks

1. **600ms pulse timing**: User might not feel the confirmation pulse. Mitigation: use strength 1 (gentle but noticeable).

2. **Nap vs screen-off confusion**: Users might confuse manual nap with auto-dim (G scope). Mitigation: sleep animation clearly different.

3. **Energy restore exploit**: Could nap-wake-nap-wake to farm energy. Acceptable — energy is cosmetic only, no gameplay advantage.

---

(End of design doc)
