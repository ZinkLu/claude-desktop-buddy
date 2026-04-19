#include "hw_power.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

static const int ON_OFF_PIN      = 18;
static const int BATTERY_OFF_PIN = 7;
static const int PUSH_BUTTON_PIN = 5;

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
  // trigger the MT3608 shutdown sequence, then deep-sleep. The push button
  // (pin 5) is configured as an EXT0 wake source — pressing it wakes the
  // device. gpio_deep_sleep_hold_en() preserves GPIO state across sleep
  // so ON_OFF stays high and the regulator stays enabled (on USB power
  // or while the button is pressed on battery).
  digitalWrite(BATTERY_OFF_PIN, HIGH);
  delay(100);
  digitalWrite(BATTERY_OFF_PIN, LOW);
  delay(200);
  digitalWrite(BATTERY_OFF_PIN, HIGH);

  rtc_gpio_init((gpio_num_t)PUSH_BUTTON_PIN);
  rtc_gpio_pullup_en((gpio_num_t)PUSH_BUTTON_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)PUSH_BUTTON_PIN);
  gpio_deep_sleep_hold_en();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PUSH_BUTTON_PIN, 0);  // wake on LOW

  esp_deep_sleep_start();
}

time_t hw_power_now() {
  time_t t = 0;
  time(&t);
  return t;
}
