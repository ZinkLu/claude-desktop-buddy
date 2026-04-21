# Phase 2-D4: Clock Enrichment Design

> **Date**: 2026-04-21  
> **Scope**: Add mood-based buddy display to clock mode

## Goal

Display a small ASCII buddy in the clock mode's lower area, with mood-based state determined by the current time. This enriches the clock view with character personality.

## Time-Based State Rules

| Time Condition | PersonaState | Description |
|----------------|--------------|-------------|
| 01:00 - 07:00 | P_SLEEP | Early morning sleep |
| Friday 12:00+ | P_CELEBRATE | Friday afternoon celebration |
| Saturday/Sunday | P_HEART | Weekend hearts |
| 22:00 - 23:59 | P_DIZZY | Late night tiredness |
| Other times | P_IDLE | Default idle state |

**Priority (high to low)**: sleep > celebrate > heart > dizzy > idle

This ensures:
- Late Friday night (Sat 01:00) → sleep (not celebrate or heart)
- Saturday afternoon → heart
- Sunday 23:00 → dizzy

## Invalid Time Behavior

When clock time is invalid (displaying `--:--`), still show a buddy in P_IDLE state so the screen isn't empty.

## Implementation Approach

**Chosen**: Internal integration in `clock_face.cpp`

- Add private function `deriveClockBuddyState(const struct tm&)` to determine state from time
- Modify `paintValid()` to render buddy below the date (y ≈ 210)
- Modify `paintInvalid()` to render idle buddy
- Use `buddyRenderTo(&sp, state)` for rendering
- No API changes to `clock_face.h` needed

## Visual Layout

```
      HH:MM  (y=70, size 6)
       :SS   (y=135, size 3)
    Day Mon DD (y=180, size 2)
    [buddy]    (y=210, size 1x)
```

Buddy is centered horizontally (x=120) using `buddyRenderTo`.

## Files Modified

- `src/clock_face.cpp` — Add state derivation and buddy rendering

## Testing Notes

- Native test build should compile without errors
- On-device testing should verify buddy appears in all time conditions
- D2 nap mode takes precedence over clock mode (already implemented)

## Related Documents

- Phase 2-D handoff: `docs/superpowers/2026-04-19-phase2-handoff.md`
- Clock face module: `src/clock_face.cpp`
- Buddy rendering API: `src/buddy.h`
