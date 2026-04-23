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

// --- Software spring / detent API (D1) ----------------------------------
// When spring mode is enabled the motor behaves like a sprung knob:
//   • Within the range, torque rises based on distance from center
//   • At the boundary the stiffness jumps (endstop_strength)
//   • Releasing the knob snaps it back to center
//   • Supports "virtual detents" — position snaps at regular intervals

// Enable spring mode with specified parameters.
// center_deg: center position in degrees (0-360)
// range_deg: total working range (±range_deg/2 from center)
// max_strength: max torque strength (0-255 mapped from voltage)
// curve_exp: torque curve exponent (1.0=linear, 1.5=default "ease-in")
void hw_motor_set_spring(float center_deg, float range_deg,
                         float max_strength, float curve_exp);
void hw_motor_disable_spring();
bool hw_motor_spring_enabled();

// Virtual detent configuration (optional, for "notched" feel)
// When enabled, the knob snaps to discrete positions within the range.
void hw_motor_set_detents(float position_width_deg, float snap_point,
                          float detent_strength, float endstop_strength);
void hw_motor_disable_detents();

// Current spring position (0 = center, can be negative)
float hw_motor_spring_position();

// Reset spring center to current angle
void hw_motor_spring_recenter();

// Read current encoder angle (0-360°)
float hw_motor_get_current_angle();

// Get current shaft angle from FOC (in degrees). Safe to call from any core.
float hw_motor_foc_angle_deg();

// --- SimpleFOC closed-loop API (D1) --------------------------------------
// Initialize SimpleFOC motor control in a FreeRTOS task on Core 1.
// This replaces open-loop pulses with smooth torque control.

void hw_motor_init_foc();
bool hw_motor_foc_enabled();
int32_t hw_motor_position();

// Configure detent feel (width, strength, snap threshold)
void hw_motor_set_detent_config(float width_deg, float strength, float snap);
