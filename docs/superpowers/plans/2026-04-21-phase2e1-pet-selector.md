# Phase 2-E1 Implementation Plan

> **Date**: 2026-04-21

## Task 1: Clean up 17 dormant species files

For each file in `src/buddies/` except capybara.cpp:
- Remove `#include <M5StickCPlus.h>`
- Remove `extern TFT_eSprite spr;` if present
- Verify the file uses `buddy_common.h` and the shared rendering helpers

**Files to fix**: axolotl, blob, cactus, cat, chonk, dragon, duck, ghost, goose, mushroom, octopus, owl, penguin, rabbit, robot, snail, turtle

## Task 2: Expand species registry in buddy.cpp

**File**: `src/buddy.cpp`

Add extern declarations for all 18 species:
```cpp
extern const Species CAPYBARA_SPECIES;
extern const Species AXOLOTL_SPECIES;
// ... etc for all 18
```

Expand `SPECIES_TABLE`:
```cpp
static const Species* SPECIES_TABLE[] = {
  &CAPYBARA_SPECIES, &AXOLOTL_SPECIES, &BLOB_SPECIES,
  // ... all 18
};
```

## Task 3: Add helper for species name lookup

**File**: `src/buddy.h`

Add:
```cpp
const char* buddySpeciesNameByIdx(uint8_t idx);  // nullptr if out of range
```

**File**: `src/buddy.cpp`

```cpp
const char* buddySpeciesNameByIdx(uint8_t idx) {
  if (idx >= N_SPECIES) return nullptr;
  return SPECIES_TABLE[idx]->name;
}
```

## Task 4: Add DISP_PET_SELECTOR to FSM

**File**: `src/input_fsm.h`

Add `DISP_PET_SELECTOR` to `DisplayMode` enum.

Add callback to `FsmCallbacks`:
```cpp
void (*on_pet_selector_change)(uint8_t idx);  // preview changed
```

**File**: `src/input_fsm.cpp`

- Add selector to mode transitions
- Handle ROT_CW/CCW in selector mode (cycles preview index)
- Handle CLICK (confirms and returns to SETTINGS)
- Handle LONG (cancels and returns to SETTINGS)

## Task 5: Add "ascii pet" to settings menu

**File**: `src/menu_panels.cpp`

Add "ascii pet" as settings item 4 (shifting reset to 5, back to 6).
Value shows current species name via `panel_species_name()` bridge.

## Task 6: Integrate selector rendering in main.cpp

**File**: `src/main.cpp`

Add selector render case:
```cpp
case DISP_PET_SELECTOR:
  draw_pet_selector();
  break;
```

Implement `draw_pet_selector()`:
- Fill sprite black
- Title "Choose Your Buddy"
- Preview current species idle animation via `buddyRenderTo()`
- Show species name centered
- Bottom hints

Handle confirm/cancel callbacks:
- Confirm: `speciesIdxSave(idx); buddySetSpeciesIdx(idx);`
- Cancel: restore previous species

## Task 7: Update build filter

**File**: `platformio.ini`

Change:
```
+<buddies/capybara.cpp>
```
to:
```
+<buddies/*.cpp>
```

## Task 8: Update tests

**File**: `test/test_menu_fsm/test_menu_fsm.cpp`

Update settings item count from 6 to 7, adjust test indices.

## Task 9: Build and test

- `pio run -e x-knob`
- `pio test -e native`

## Checklist

- [ ] 17 species files cleaned up
- [ ] buddy.cpp registry expanded
- [ ] buddySpeciesNameByIdx() added
- [ ] DISP_PET_SELECTOR FSM mode added
- [ ] Settings menu shows "ascii pet"
- [ ] Selector render + controls working
- [ ] NVS save/load connected
- [ ] build filter updated
- [ ] Tests updated
- [ ] Firmware builds
- [ ] Native tests pass
