#pragma once
#include <stdint.h>

void hw_motor_init();
void hw_motor_click(uint8_t strength);   // strength 0..255, ~30 ms open-loop pulse
void hw_motor_off();

// Convenience wrapper: reads the current haptic level from settings() and
// fires a click at the configured strength. Level 0 is a silent no-op.
void hw_motor_click_default();

// --- Continuous / queued patterns ----------------------------------------
// All continuous patterns are driven by hw_motor_tick() — main loop MUST
// call it every iteration or these won't animate.

// Start a low-frequency alternating-direction oscillation. Feels like a
// cat purring. level 0..4; level 0 is a silent no-op.
void hw_motor_purr_start(uint8_t level);
void hw_motor_purr_stop();
bool hw_motor_purr_active();

// One-shot directional kick. direction: 0 = forward vector, 1 = reverse.
// Stronger than click (used to "push back" on tickling).
void hw_motor_kick(uint8_t direction, uint8_t level);

// One-shot short L-R-L wiggle pattern (~180 ms total).
void hw_motor_wiggle();

// Queue N clicks spaced gap_ms apart. Returns immediately; pulses fired
// by hw_motor_tick. A second call before the first finishes replaces
// the queue.
void hw_motor_pulse_series(uint8_t n, uint16_t gap_ms, uint8_t level);

// Continuous vibration for duration_ms. level 0..4.
void hw_motor_vibrate(uint16_t duration_ms, uint8_t level);

// Drive purr / pulse_series / vibrate state machines. Call every main
// loop iteration. Cheap when all states are idle.
void hw_motor_tick(uint32_t now_ms);
