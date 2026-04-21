# Phase 2-E1: ASCII Pet Selector + Species Enablement Design

> **Date**: 2026-04-21  
> **Scope**: Enable all 18 ASCII species and add interactive pet selector UI

## Goal

Re-enable the 17 dormant ASCII species files and add an interactive species selector reachable from the settings menu. The selector shows a live preview of each species with idle animation, allowing the user to browse and confirm their choice.

## Architecture

### New Display Mode: DISP_PET_SELECTOR

A new top-level display mode for the species selection screen. Entry from Settings menu; exit saves to NVS and returns to Settings (or cancels on LONG).

### Species File Cleanup Pattern

Each of the 17 dormant species files needs the same fix applied in Phase 1 to capybara:
- Remove `#include <M5StickCPlus.h>`
- Remove `extern TFT_eSprite spr;` (if present)
- Ensure the file uses `buddy_common.h` helpers and the shared `spr` macro from `buddy.cpp`

### Species Registry

`buddy.cpp` maintains the `SPECIES_TABLE` array. Currently only capybara. After E1, all 18 species are registered with `extern const Species` declarations.

## Interaction Design

### Entry Path

```
Home → LONG → Menu → scroll to "ascii pet" → CLICK
```

### Selector Screen Layout

```
┌─────────────────────────────────────┐
│  Choose Your Buddy                  │
│                                     │
│      [idle animation preview]       │
│                                     │
│        > capybara <                 │
│                                     │
│  CW/CCW: browse   CLICK: confirm    │
└─────────────────────────────────────┘
```

### Controls

- **ROT_CW / ROT_CCW**: Cycle to next/previous species (wraps around)
- **CLICK**: Confirm selection, save to NVS, return to Settings
- **LONG**: Cancel, discard changes, return to Settings

### Motor Feedback

- Each rotation in the selector fires `hw_motor_click_default()` for tactile feedback

## Files Modified

| File | Change |
|------|--------|
| `src/buddies/*.cpp` (17 files) | Remove M5StickCPlus.h dependency |
| `src/buddy.cpp` | Expand SPECIES_TABLE to all 18 species |
| `src/buddy.h` | Add `buddySpeciesNameByIdx()` for selector display |
| `src/input_fsm.h` | Add `DISP_PET_SELECTOR` to DisplayMode enum |
| `src/input_fsm.cpp` | Handle selector mode inputs |
| `src/menu_panels.cpp` | Add "ascii pet" settings item |
| `src/main.cpp` | Render selector, handle confirm/cancel, NVS save |
| `platformio.ini` | Add `+buddies/*.cpp` to build_src_filter |

## State Persistence

- Species index stored in NVS via existing `speciesIdxLoad/Save` in `stats.h`
- `buddyInit()` loads saved index on boot
- Selector confirms via `speciesIdxSave()` then `buddySetSpeciesIdx()`

## Testing

- Firmware builds with all 18 species linked
- Native tests: verify FSM mode transitions for selector
- Device tests: verify all species render correctly, selector cycles, NVS persists

## Notes

- Selector preview uses `buddyRenderTo()` at 1× scale on the sprite
- This is E1 only; E2 (CJK font) is a separate sub-project
