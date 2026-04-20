# Phase 2-D: Gestures + Effects Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement D2 (Manual Nap gesture, Edge hard-bump) and D3 (Level-up celebration) features

**Architecture:** Extend existing input FSM with progressive long-press state machine; add nap state management in main loop; integrate celebrate trigger into existing stats polling

**Tech Stack:** ESP32 Arduino, PlatformIO, TFT_eSPI, existing hw_motor/input/display modules

---

## File Structure

| File | Responsibility |
|---|---|
| `src/main.cpp` | Nap state management, progressive long-press handling, celebrate trigger, wake detection |
| `src/input_fsm.cpp` | Boundary detection for menu/settings/reset scroll |
| `src/input_fsm.h` | Boundary helper (if needed) |

---

## Task 1: Add Manual Nap State Variables to main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add nap state variables after existing pet state**

After line 286 in main.cpp (after `petSquishUntilMs`), add:

```cpp
// Manual nap state (D2)
static bool manualNapping = false;
static uint32_t napStartMs = 0;
static bool longPressConfirmMode = false;
static uint32_t longPressStartMs = 0;
static const uint32_t LONG_CONFIRM_THRESHOLD_MS = 600;
static const uint32_t NAP_TRIGGER_THRESHOLD_MS = 3000;
static const uint32_t NAP_HINT_DURATION_MS = 3000;
```

- [ ] **Step 2: Add celebrate state tracking after nap variables**

```cpp
// Level-up celebrate state (D3)
static uint32_t celebrateUntilMs = 0;
```

---

## Task 2: Implement Progressive Long-Press Detection (Home Mode)

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Modify button handling in main loop to track long-press start**

In the `loop()` function, find the section that handles `EVT_LONG` for home mode (around line 217). Replace the simple LONG handling with progressive detection.

Before the existing long-press handling code, add state tracking:

```cpp
// Progressive long-press detection (D2-A)
static bool trackingLongPress = false;
static uint32_t longPressBeginMs = 0;

void loop() {
  // ... existing setup code ...
  
  // Handle nap state first (blocks other processing)
  if (manualNapping) {
    // Any input wakes from nap
    if (e != EVT_NONE) {
      manualNapping = false;
      uint32_t napDuration = (now - napStartMs) / 1000;
      statsOnNapEnd(napDuration);
      statsOnWake();
      hw_display_set_brightness((settings().brightness + 1) * 20);
      hw_motor_click_default();  // wake feedback
      Serial.printf("[nap] woke after %lu seconds\n", (unsigned long)napDuration);
    }
    // Skip normal rendering during nap
    hw_motor_tick(now);
    delay(50);
    return;
  }
  
  // ... rest of loop ...
}
```

- [ ] **Step 2: Add progressive long-press logic in input handling**

In the input dispatch section (around where EVT_LONG is handled), modify to support progressive detection:

```cpp
// Progressive long-press for home mode (D2-A)
if (_v.mode == DISP_HOME && e == EVT_LONG) {
  if (!longPressConfirmMode) {
    // First time reaching 600ms
    longPressConfirmMode = true;
    longPressStartMs = now;
    hw_motor_click(40);  // soft pulse to acknowledge
    Serial.println("[long-press] confirmation mode active");
    return;  // Don't dispatch yet
  }
}
```

- [ ] **Step 3: Add timer check for confirmation mode and nap trigger**

Add at the beginning of `loop()`, before input polling:

```cpp
// Check progressive long-press timeouts
if (longPressConfirmMode) {
  uint32_t heldDuration = now - longPressStartMs;
  
  if (heldDuration >= NAP_TRIGGER_THRESHOLD_MS) {
    // Trigger nap
    longPressConfirmMode = false;
    manualNapping = true;
    napStartMs = now;
    hw_motor_pulse_series(2, 100, 2);  // two pulses
    Serial.println("[nap] manual nap triggered");
  }
}
```

- [ ] **Step 4: Handle button release during confirmation mode**

Modify the input handling to check if we're in confirmation mode:

```cpp
// In the button handling section, before dispatching to FSM
if (longPressConfirmMode) {
  // Check if button was released (EVT_NONE after being pressed)
  // This requires tracking previous button state
  // For now, we'll handle release detection differently
}
```

Actually, we need a different approach. Let's use the existing hw_input_poll() pattern but add state tracking.

**Revised Step 4:** Add a function to handle the confirmation hint display:

```cpp
// Add before loop()
static void drawNapHint() {
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextSize(1);
  sp.drawString("松手=菜单 | 继续=休眠", 120, 200);
  sp.setTextDatum(TL_DATUM);
}
```

- [ ] **Step 5: Add nap rendering to main render switch**

In the render switch statement (around line 511), add a case for manual nap before the other cases:

```cpp
// Render
TFT_eSprite& sp = hw_display_sprite();
if (firstFrame) { sp.fillSprite(TFT_BLACK); firstFrame = false; }

// Nap mode takes precedence
if (manualNapping) {
  sp.fillSprite(TFT_BLACK);
  // Dim brightness
  hw_display_set_brightness(20);  // 20% = level 0
  // Draw sleep animation using buddy
  buddyTick((uint8_t)P_SLEEP);
  // Optional hint
  if (now - napStartMs < NAP_HINT_DURATION_MS) {
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.setTextSize(1);
    sp.drawString("转动或点击唤醒", 120, 220);
    sp.setTextDatum(TL_DATUM);
  }
  sp.pushSprite(0, 0);
} else {
  // ... existing render switch ...
}
```

- [ ] **Step 6: Add FsmCallback for long-press release**

We need to detect when the long-press is released during confirmation mode. Since we don't have raw button state in main loop, we'll use a different approach: track in the FSM callbacks.

Add a new callback to FsmCallbacks in `input_fsm.h`:

```cpp
void (*on_long_press_release)();  // Called when long press ends (button released)
```

Then in main.cpp, implement it:

```cpp
static void cb_on_long_press_release() {
  if (longPressConfirmMode) {
    uint32_t heldDuration = millis() - longPressStartMs;
    if (heldDuration < NAP_TRIGGER_THRESHOLD_MS) {
      // Released before 3s, execute menu
      longPressConfirmMode = false;
      // Manually trigger menu open since we consumed the LONG event
      input_fsm_dispatch(EVT_LONG, millis());  // Re-dispatch
    }
  }
}
```

Actually, this is getting complex. Let's simplify: **Use existing EVT_LONG pattern with timing checks.**

**Revised approach for Task 2:**

Instead of complex progressive detection, use the existing button FSM but add a delayed action:

1. On first EVT_LONG (600ms), enter confirmation mode and show hint
2. Start a timer
3. If no new LONG event arrives within 50ms (meaning button was released), execute menu
4. If we reach 3000ms while still in confirmation mode, trigger nap

This requires modifying how we handle EVT_LONG in main.cpp.

Let me rewrite Task 2 with the simplified approach.

**Simplified Task 2:**

- [ ] **Step 1: Add nap state variables**

```cpp
// After line 286 (petSquishUntilMs)
// D2-A: Manual nap state
static bool manualNapping = false;
static uint32_t napStartMs = 0;
static bool napHintVisible = false;
static uint32_t napHintUntilMs = 0;

// D3: Celebrate state
static uint32_t celebrateUntilMs = 0;
```

- [ ] **Step 2: Implement nap state check at top of loop**

```cpp
void loop() {
  // ... existing declarations ...
  
  // D2-A: Nap state blocks normal processing
  if (manualNapping) {
    // Any input wakes from nap
    if (e != EVT_NONE) {
      manualNapping = false;
      uint32_t napDurationMs = now - napStartMs;
      statsOnNapEnd(napDurationMs / 1000);
      statsOnWake();
      hw_display_set_brightness((settings().brightness + 1) * 20);
      hw_motor_click_default();
      Serial.printf("[nap] woke after %lu seconds\n", (unsigned long)(napDurationMs / 1000));
    } else {
      // Render nap screen
      TFT_eSprite& sp = hw_display_sprite();
      if (firstFrame) { sp.fillSprite(TFT_BLACK); firstFrame = false; }
      hw_display_set_brightness(20);  // 10% brightness
      buddyTick((uint8_t)P_SLEEP);
      if (now < napHintUntilMs) {
        sp.setTextDatum(MC_DATUM);
        sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
        sp.setTextSize(1);
        sp.drawString("转动或点击唤醒", 120, 220);
        sp.setTextDatum(TL_DATUM);
      }
      sp.pushSprite(0, 0);
      hw_motor_tick(now);
      delay(50);
      return;
    }
  }
  
  // ... rest of loop ...
}
```

- [ ] **Step 3: Add progressive long-press handler for home mode**

This is the tricky part. We need to intercept LONG events on home and implement the progressive behavior.

The issue is that hw_input_poll() returns discrete events, not continuous state. We need to work with what's available.

**Approach:** Use a state machine in main.cpp that tracks long-press progression:

```cpp
// Add near top of file with other statics
enum LongPressState {
  LP_IDLE = 0,
  LP_CONFIRMING,    // 600ms reached, showing hint
  LP_TRIGGERED      // 3000ms reached, nap triggered
};
static LongPressState lpState = LP_IDLE;
static uint32_t lpStartMs = 0;
```

But wait — we don't get continuous updates. We only get events when thresholds are crossed.

**Better approach:** Modify hw_input to support progressive long-press, or handle it differently.

Actually, the cleanest solution is: **Use timing in the main loop to detect if button is still held after EVT_LONG.**

But we don't have access to raw button state...

**Alternative:** Change the requirement slightly. Instead of progressive detection, use:
- Single LONG (600ms) = menu
- DOUBLE_LONG (two LONGs within 2 seconds) = nap

This is easier to implement with existing infrastructure.

But the user specifically asked for progressive 600ms/3000ms...

Let me think... We can approximate this by:
1. On EVT_LONG, check if we're in home mode
2. If yes, enter a "waiting for release or 3s" state
3. Poll button state directly using hw_input_internal or add a new API

Actually, looking at hw_input.cpp, we can add a function to check if button is currently pressed.

**Let's add a helper function to hw_input:**

- [ ] **Step 4: Add button state query to hw_input**

In `src/hw_input.h`, add:

```cpp
bool hw_input_button_pressed();  // Returns true if button is currently held
```

In `src/hw_input.cpp` (Arduino section), add:

```cpp
bool hw_input_button_pressed() {
  return (digitalRead(BTN_PIN) == LOW);
}
```

- [ ] **Step 5: Implement progressive long-press using button query**

In main.cpp, modify the LONG handling:

```cpp
// In the EVT_LONG handling section for home mode
if (e == EVT_LONG && _v.mode == DISP_HOME) {
  // Progressive long-press: 600ms reached, now track to 3000ms
  lpState = LP_CONFIRMING;
  lpStartMs = now;
  hw_motor_click(40);  // soft acknowledgement
  Serial.println("[long-press] confirmation started");
  
  // Show hint immediately
  // (will be rendered this frame)
}
```

Then add at the start of loop():

```cpp
// Check progressive long-press progression
if (lpState == LP_CONFIRMING) {
  if (!hw_input_button_pressed()) {
    // Button released before 3s
    lpState = LP_IDLE;
    // Execute menu action
    _enter(DISP_MENU);
    _v.menuSel = 0;
    _clear_reset_arm();
    CALL0(invalidate_panel);
  } else if (now - lpStartMs >= 2400) {  // 3000 - 600 = 2400ms additional
    // Reached 3s total, trigger nap
    lpState = LP_TRIGGERED;
    manualNapping = true;
    napStartMs = now;
    napHintUntilMs = now + NAP_HINT_DURATION_MS;
    hw_motor_pulse_series(2, 100, 2);
    Serial.println("[nap] triggered");
  }
}
```

Wait, this won't work because we consume the LONG event and don't enter menu...

Let me rethink. The cleanest approach is:

1. EVT_LONG at 600ms enters confirmation mode
2. In confirmation mode, we show hint and wait
3. Next loop iteration checks button state
4. If released → menu
5. If held for additional 2400ms → nap

But the problem is that once we're in confirmation mode, we don't want to process other inputs...

Actually, let's use a simpler approach: **Render the hint when in confirmation mode, and block other processing.**

```cpp
if (lpState == LP_CONFIRMING) {
  // Block other processing while in confirmation mode
  if (!hw_input_button_pressed()) {
    // Released, go to menu
    lpState = LP_IDLE;
    // ... trigger menu ...
  } else if (now - lpStartMs >= 2400) {
    // Trigger nap
    lpState = LP_IDLE;
    manualNapping = true;
    // ...
  } else {
    // Still confirming, render hint
    // ... render hint overlay ...
    delay(20);
    return;
  }
}
```

This is getting complex. Let me complete Task 2 with this approach and move on.

- [ ] **Step 6: Add hint rendering during confirmation**

When in confirmation mode, render the hint overlay on top of home screen.

---

Given the complexity, let me create a simpler implementation plan that focuses on the core functionality without over-engineering.

## Revised Simplified Plan

### Task 1: Add Manual Nap (Simplified)

Instead of complex progressive detection, implement:
- Home mode: LONG press 600ms = enter "confirmation mode"
- In confirmation mode: show hint for 2.4s
- If button still held at 3s total = nap
- If button released = menu

### Task 2: Add Celebrate Trigger

- In main loop, check `statsPollLevelUp()`
- If true, trigger celebrate animation + motor

### Task 3: Add Edge Bump

- Modify input_fsm to fire on_scroll_edge at menu boundaries

Let me write the complete plan now.
