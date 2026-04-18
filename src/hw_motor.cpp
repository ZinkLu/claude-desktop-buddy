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
