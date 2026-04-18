#include "hw_power.h"
#include <Arduino.h>

static const int ON_OFF_PIN      = 18;
static const int BATTERY_OFF_PIN = 7;

void hw_power_init() {
  // X-Knob's physical power switch momentarily raises ON_OFF; firmware must
  // then hold it high to keep the regulator enabled after the switch releases.
  pinMode(ON_OFF_PIN, OUTPUT);
  digitalWrite(ON_OFF_PIN, HIGH);

  // Keep battery connected.
  pinMode(BATTERY_OFF_PIN, OUTPUT);
  digitalWrite(BATTERY_OFF_PIN, HIGH);
}

void hw_power_off() {
  digitalWrite(ON_OFF_PIN, LOW);
  delay(1000);
  // Still alive = running on USB. Halt.
  while (true) { delay(1000); }
}

time_t hw_power_now() {
  time_t t = 0;
  time(&t);
  return t;
}
