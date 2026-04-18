#include "hw_power.h"
#include <Arduino.h>
#include <esp_sleep.h>

static const int ON_OFF_PIN      = 18;
static const int BATTERY_OFF_PIN = 7;

void hw_power_init() {
  // X-Knob's physical power switch momentarily raises ON_OFF; firmware must
  // then hold it high to keep the regulator enabled after the switch releases.
  pinMode(ON_OFF_PIN, OUTPUT);
  digitalWrite(ON_OFF_PIN, HIGH);

  // BATTERY_OFF gates the MT3608 boost converter for battery power. Logic
  // is inverted vs the name: LOW keeps the converter ENABLED. Phase 1
  // originally drove this HIGH on init which killed battery output the
  // moment USB was unplugged (matches X-Knob hal/power.cpp exactly).
  pinMode(BATTERY_OFF_PIN, OUTPUT);
  digitalWrite(BATTERY_OFF_PIN, LOW);
}

void hw_power_off() {
  // Mirror X-Knob hal/power.cpp: toggle BATTERY_OFF HIGH-LOW-HIGH to
  // trigger the MT3608 shutdown sequence, then deep-sleep with the push
  // button as an EXT0 wake source. On USB the regulator stays energized
  // and we just halt in deep sleep.
  digitalWrite(BATTERY_OFF_PIN, HIGH);
  delay(100);
  digitalWrite(BATTERY_OFF_PIN, LOW);
  delay(200);
  digitalWrite(BATTERY_OFF_PIN, HIGH);
  esp_deep_sleep_start();
}

time_t hw_power_now() {
  time_t t = 0;
  time(&t);
  return t;
}
