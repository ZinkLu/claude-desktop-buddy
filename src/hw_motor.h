#pragma once
#include <stdint.h>

void hw_motor_init();
void hw_motor_click(uint8_t strength);   // strength 0..255, ~30 ms open-loop pulse
void hw_motor_off();
