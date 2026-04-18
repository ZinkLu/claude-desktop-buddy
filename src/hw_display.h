#pragma once
#include <TFT_eSPI.h>

void          hw_display_init();
void          hw_display_set_brightness(uint8_t pct);  // 0..100
TFT_eSPI&     hw_display_tft();
TFT_eSprite&  hw_display_sprite();
