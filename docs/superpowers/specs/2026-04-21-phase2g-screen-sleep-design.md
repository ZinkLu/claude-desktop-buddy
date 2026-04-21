# Phase 2-G: Screen Sleep + Backlight Timeout Design

> **Date**: 2026-04-21  
> **Scope**: Auto-dim after 30s idle, wake on any input

## Goal

After 30 seconds without user input (rotation or button), automatically dim the display to save power. Any input immediately restores normal brightness and buddy state.

## Behavior

### Idle Timeout

- **Timeout**: 30 seconds (fixed)
- **Trigger**: No EVT_ROT_CW / EVT_ROT_CCW / EVT_CLICK / EVT_DOUBLE / EVT_LONG
- **What counts as activity**: Any input event resets the timer

### Dim State

When idle timeout triggers:
- **Brightness**: Drop to 10% (`hw_display_set_brightness(10)`)
- **Home mode**: Buddy switches to `P_SLEEP` state
- **Pet mode**: Buddy switches to `P_SLEEP` state
- **Clock mode**: Keep time display, just dim
- **Other modes** (menu, settings, info, passkey): Keep current content, just dim

### Wake

On any input event:
1. Restore brightness to user's setting `(settings().brightness + 1) * 20`
2. If in home/pet mode, restore normal buddy state (will be handled by next render tick)
3. Process the input event normally

### Settings

- Add "auto dim" toggle in settings menu (default: ON)
- Stored in NVS via `PersistentState.autoDim`
- When disabled, never enter dim state

## Architecture

```
hw_idle module (new)
├── hw_idle_init()           -- nothing yet
├── hw_idle_activity()       -- reset timer (called on any input)
├── hw_idle_tick(now)        -- check timeout, returns true if just entered dim
├── hw_idle_is_dimmed()      -- query current dim state
└── hw_idle_set_enabled(bool)-- enable/disable feature

main.cpp
├── loop():
│   ├── if (e != EVT_NONE) hw_idle_activity();
│   ├── if (hw_idle_tick(now)) { dim display, sleep buddy }
│   └── if (was dimmed && e != EVT_NONE) { restore brightness }
│
└── Settings callback:
    └── toggle autoDim in PersistentState

stats.h
└── PersistentState + autoDim field

menu_panels.cpp
└── Settings page + autoDim toggle item
```

## Files Modified

| File | Change |
|------|--------|
| `src/hw_idle.h` | New — module interface |
| `src/hw_idle.cpp` | New — implementation |
| `src/main.cpp` | Activity tracking, dim/wake logic |
| `src/stats.h` | Add `autoDim` to `PersistentState` |
| `src/menu_panels.cpp` | Add auto dim toggle in settings |

## Testing

- Native tests: hw_idle timer logic
- Device tests: Verify 30s timeout, wake on input, settings persist

## Notes

- This is **screen sleep only** (backlight dim), NOT deep sleep
- BLE connection stays alive
- System clock continues running
- Low priority until user runs on battery regularly (per handoff doc)
