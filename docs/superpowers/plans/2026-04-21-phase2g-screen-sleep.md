# Phase 2-G Implementation Plan

> **Date**: 2026-04-21

## Task 1: Create hw_idle module

**Files**: `src/hw_idle.h`, `src/hw_idle.cpp`

`hw_idle.h`:
```cpp
#pragma once
#include <stdint.h>

void hw_idle_init();
void hw_idle_activity();           // Reset idle timer
bool hw_idle_tick(uint32_t now);   // Check timeout, returns true if just entered dim
bool hw_idle_is_dimmed();          // Query current dim state
void hw_idle_set_enabled(bool en); // Enable/disable feature
```

`hw_idle.cpp`:
```cpp
#include "hw_idle.h"

static uint32_t lastActivityMs = 0;
static bool dimmed = false;
static bool enabled = true;
static const uint32_t IDLE_TIMEOUT_MS = 30000;

void hw_idle_init() { lastActivityMs = 0; dimmed = false; }
void hw_idle_activity() { lastActivityMs = millis(); dimmed = false; }
bool hw_idle_is_dimmed() { return enabled && dimmed; }
void hw_idle_set_enabled(bool en) { enabled = en; if (!en) dimmed = false; }

bool hw_idle_tick(uint32_t now) {
  if (!enabled || dimmed) return false;
  if ((int32_t)(now - lastActivityMs) >= (int32_t)IDLE_TIMEOUT_MS) {
    dimmed = true;
    return true;  // Just entered dim
  }
  return false;
}
```

## Task 2: Add autoDim to PersistentState

**File**: `src/stats.h`

Add to `PersistentState` struct (after `brightness`):
```cpp
bool autoDim;      // true = enable auto screen dim
```

Add default in `statsInit()`:
```cpp
s->autoDim = true;
```

Add to NVS load/save in `statsLoad()` / `statsSave()`.

## Task 3: Add auto dim toggle to settings menu

**File**: `src/menu_panels.cpp`

Add "auto dim" as a settings item (cycle ON/OFF).
Callback updates `settings().autoDim` and saves.

## Task 4: Integrate into main.cpp

**File**: `src/main.cpp`

In `loop()`:
1. After `hw_input_poll()`, if `e != EVT_NONE`:
   - Call `hw_idle_activity()`
   - If was dimmed, restore brightness before processing event

2. At start of loop (before rendering):
   - If `hw_idle_tick(now)` returns true:
     - Set brightness to 10%
     - If home/pet mode, force P_SLEEP buddy

3. In dim state rendering:
   - Continue normal render, but buddy is P_SLEEP (home/pet)
   - Other modes render normally (just dimmed)

## Task 5: Build and test

- `pio run -e x-knob`
- `pio test -e native`

## Checklist

- [ ] hw_idle module created
- [ ] autoDim field in PersistentState
- [ ] Settings menu toggle
- [ ] main.cpp integration
- [ ] Firmware builds
- [ ] Native tests pass
