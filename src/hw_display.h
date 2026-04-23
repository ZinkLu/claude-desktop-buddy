#pragma once
#include <Arduino_GFX_Library.h>

void hw_display_init();
Arduino_GFX* hw_display_canvas();
void hw_display_flush();
void hw_display_set_brightness(uint8_t percent);
