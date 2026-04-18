#pragma once
#include <stdint.h>

void hw_motor_init();
void hw_motor_click(uint8_t strength);   // strength 0..255, ~30 ms open-loop pulse
void hw_motor_off();

// Convenience wrapper: reads the current haptic level from settings() and
// fires a click at the configured strength. Level 0 is a silent no-op.
void hw_motor_click_default();
