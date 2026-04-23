#pragma once
#include <stdint.h>
#include <Arduino_GFX_Library.h>

// Initialize CJK font system (call once at startup)
void cjk_font_init();

// Set target for rendering
class Arduino_GFX;
void cjk_set_target(Arduino_GFX* canvas);

// Draw UTF-8 string with mixed ASCII/CJK rendering
// font_size: 1 = small (HUD, 12px CJK), 2 = large (prompts, 24px scaled CJK)
void cjk_draw_string(int x, int y, const char* utf8_str,
                     uint16_t color, uint16_t bg, uint8_t font_size);

// Calculate pixel width of UTF-8 string
int cjk_text_width(const char* utf8_str, uint8_t font_size);

// Count UTF-8 codepoints (not bytes)
int cjk_utf8_strlen(const char* utf8_str);