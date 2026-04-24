#pragma once
#include <stdint.h>

// --- SimpleFOC closed-loop motor control (D1) ----------------------------
// Motor runs on Core 1 in a FreeRTOS task at 1 kHz. Spring/detent feel is
// always active. Effects are queued from Core 0 and applied as torque
// overlays in the motor loop.

void hw_motor_init_foc();
bool hw_motor_foc_enabled();
float hw_motor_foc_angle_deg();

// Timestamp (millis) until which encoder rotation should be suppressed
// to filter out motor-induced shaft movement.
uint32_t hw_motor_busy_until();

// True when a one-shot or continuous effect is playing.
bool hw_motor_effect_active();

// --- One-shot haptic effects ---------------------------------------------
// All enqueue immediately and return; Core 1 plays them non-blocking.

void hw_motor_click(uint8_t strength);   // strength 0..255, ~30 ms pulse
void hw_motor_click_default();           // click at current haptic level
void hw_motor_wiggle();                  // L-R-L pattern (~220 ms)
void hw_motor_pulse_series(uint8_t n, uint16_t gap_ms, uint8_t level);
void hw_motor_vibrate(uint16_t duration_ms, uint8_t level);

// --- Continuous effects --------------------------------------------------

void hw_motor_purr_start(uint8_t level); // alternating pulse, level 0..4
void hw_motor_purr_stop();
bool hw_motor_purr_active();

// --- Position / state ----------------------------------------------------

// Detent position counter maintained by the motor task. Changes by ±1
// each time the shaft crosses a snap point. Use this (not raw angle) for
// rotation events when FOC is active.
int32_t hw_motor_position();

// --- Control -------------------------------------------------------------

// Set haptic intensity (0..4). Scales spring detent strength and effect
// power. Level 0 = free spin + silent effects. Called when user changes
// the haptic setting.
void hw_motor_set_haptic(uint8_t level);

void hw_motor_off();                     // cancel all effects
