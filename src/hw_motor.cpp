#include "hw_motor.h"
#include <Arduino.h>

// X-Knob BLDC phases. Arduino-ESP32 binds LEDC channels to timers as
// timer = channel / 2, so channels 0 and 1 share timer 0. hw_display owns
// channel 0 (backlight @ 5kHz); if the motor touched channel 1 at 20kHz
// it would silently reconfigure timer 0 and kill backlight PWM.
// Use channels 2, 4, 6 — each maps to a distinct timer (1, 2, 3).
static const int MO1 = 17;
static const int MO2 = 16;
static const int MO3 = 15;
static const int CH1 = 2;
static const int CH2 = 4;
static const int CH3 = 6;

static const int PWM_FREQ = 20000;   // Above audible
static const int PWM_BITS = 8;

void hw_motor_init() {
  ledcSetup(CH1, PWM_FREQ, PWM_BITS);
  ledcSetup(CH2, PWM_FREQ, PWM_BITS);
  ledcSetup(CH3, PWM_FREQ, PWM_BITS);
  ledcAttachPin(MO1, CH1);
  ledcAttachPin(MO2, CH2);
  ledcAttachPin(MO3, CH3);
  hw_motor_off();
}

void hw_motor_click(uint8_t strength) {
  // Fixed 3-phase vector for a mechanical "bump". No SimpleFOC closed loop
  // in Phase 1 — just apply a short torque impulse and release.
  uint8_t s  = strength;
  uint8_t s2 = strength / 2;
  ledcWrite(CH1, s);
  ledcWrite(CH2, s2);
  ledcWrite(CH3, 0);
  delay(30);
  hw_motor_off();
}

void hw_motor_off() {
  ledcWrite(CH1, 0);
  ledcWrite(CH2, 0);
  ledcWrite(CH3, 0);
}

// Haptic level 0..4 maps to motor strength. Level 0 skips the click entirely.
static const uint8_t HAPTIC_STRENGTH[5] = { 0, 40, 80, 120, 200 };

// stats.h uses a file-scope static _settings, so including it from this TU
// would give us a second private copy that never sees settings menu updates.
// main.cpp owns the live copy and exposes it via this extern function.
extern uint8_t current_haptic_level();

void hw_motor_click_default() {
  uint8_t level = current_haptic_level();
  if (level > 4) level = 3;
  if (level == 0) return;
  hw_motor_click(HAPTIC_STRENGTH[level]);
}

// --- Continuous-mode state ------------------------------------------------

// Purr state: alternating-direction pulses at ~150 ms cadence.
static bool     _purr_active        = false;
static uint8_t  _purr_level         = 0;
static uint32_t _purr_next_at_ms    = 0;
static bool     _purr_next_direction= false;
static const uint32_t PURR_GAP_MS   = 150;

// Pulse-series queue state.
static uint8_t  _series_remaining   = 0;
static uint16_t _series_gap_ms      = 0;
static uint8_t  _series_level       = 0;
static uint32_t _series_next_at_ms  = 0;

// Vibrate state.
static bool     _vib_active         = false;
static uint32_t _vib_end_ms         = 0;
static uint8_t  _vib_level          = 0;
static uint32_t _vib_next_pulse_ms  = 0;
static const uint32_t VIB_GAP_MS    = 40;   // 25 Hz pulse cadence for continuous feel

static uint8_t level_to_strength(uint8_t level) {
  if (level == 0) return 0;
  if (level > 4)  level = 3;
  return HAPTIC_STRENGTH[level];
}

void hw_motor_purr_start(uint8_t level) {
  if (level == 0) return;
  if (level > 4) level = 3;
  _purr_active = true;
  _purr_level = level;
  _purr_next_at_ms = millis();
  _purr_next_direction = false;
}

void hw_motor_purr_stop() {
  _purr_active = false;
}

bool hw_motor_purr_active() { return _purr_active; }

// Private: apply a directional 3-phase pulse. direction 0 uses the same
// vector as hw_motor_click (CH1 full, CH2 half, CH3 zero); direction 1
// rotates the vector (CH1 zero, CH2 half, CH3 full) to reverse torque.
static void motor_directional_pulse(uint8_t strength, uint8_t direction, uint8_t ms) {
  if (direction == 0) {
    ledcWrite(CH1, strength);
    ledcWrite(CH2, strength / 2);
    ledcWrite(CH3, 0);
  } else {
    ledcWrite(CH1, 0);
    ledcWrite(CH2, strength / 2);
    ledcWrite(CH3, strength);
  }
  delay(ms);
  hw_motor_off();
}

void hw_motor_kick(uint8_t direction, uint8_t level) {
  uint8_t s = level_to_strength(level);
  if (s == 0) return;
  motor_directional_pulse(s, direction, 40);
}

void hw_motor_wiggle() {
  uint8_t s = HAPTIC_STRENGTH[3];
  motor_directional_pulse(s, 0, 60);
  delay(20);
  motor_directional_pulse(s, 1, 60);
  delay(20);
  motor_directional_pulse(s, 0, 60);
}

void hw_motor_pulse_series(uint8_t n, uint16_t gap_ms, uint8_t level) {
  if (level == 0 || n == 0) return;
  if (level > 4) level = 3;
  _series_remaining = n;
  _series_gap_ms = gap_ms;
  _series_level = level;
  _series_next_at_ms = millis();
}

void hw_motor_vibrate(uint16_t duration_ms, uint8_t level) {
  if (level == 0 || duration_ms == 0) return;
  if (level > 4) level = 3;
  _vib_active = true;
  _vib_level = level;
  _vib_end_ms = millis() + duration_ms;
  _vib_next_pulse_ms = millis();
}

void hw_motor_tick(uint32_t now_ms) {
  // Purr
  if (_purr_active && now_ms >= _purr_next_at_ms) {
    uint8_t s = level_to_strength(_purr_level);
    motor_directional_pulse(s, _purr_next_direction ? 1 : 0, 20);
    _purr_next_direction = !_purr_next_direction;
    _purr_next_at_ms = now_ms + PURR_GAP_MS;
  }

  // Pulse series
  if (_series_remaining > 0 && now_ms >= _series_next_at_ms) {
    uint8_t s = level_to_strength(_series_level);
    motor_directional_pulse(s, 0, 25);
    _series_remaining--;
    _series_next_at_ms = now_ms + _series_gap_ms;
  }

  // Vibrate (alternating quick pulses until end_ms)
  if (_vib_active) {
    if (now_ms >= _vib_end_ms) {
      _vib_active = false;
    } else if (now_ms >= _vib_next_pulse_ms) {
      uint8_t s = level_to_strength(_vib_level);
      motor_directional_pulse(s, (now_ms & 0x20) ? 1 : 0, 10);
      _vib_next_pulse_ms = now_ms + VIB_GAP_MS;
    }
  }
}
