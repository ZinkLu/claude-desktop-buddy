#include "hw_motor.h"
#include <Arduino.h>

// X-Knob BLDC phases. LEDC channels 1..3 reserved for the motor; channel 0
// is owned by hw_display for backlight PWM.
static const int MO1 = 17;
static const int MO2 = 16;
static const int MO3 = 15;
static const int CH1 = 1;
static const int CH2 = 2;
static const int CH3 = 3;

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
