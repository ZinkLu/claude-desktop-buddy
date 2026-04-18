#pragma once
#include <stdint.h>

enum LedMode {
  LED_OFF = 0,
  LED_ATTENTION_BREATH,
  LED_APPROVE_FLASH,
  LED_DENY_FLASH,
};

void hw_leds_init();
// Set mode. Calls during an active flash (APPROVE/DENY) are ignored until
// the flash completes and auto-reverts to LED_OFF.
void hw_leds_set_mode(LedMode m);
// Advance animation. Call once per main-loop tick.
void hw_leds_tick();
