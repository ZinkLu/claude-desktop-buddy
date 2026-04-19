#pragma once
#include <stdint.h>

// Renders the given info page to hw_display_sprite() and pushes.
// Called from main.cpp when displayMode == DISP_INFO each frame (page
// content for CLAUDE and SYSTEM updates live; others are static but we
// just redraw unconditionally — cheap at 50 fps vs the sprite push cost).
void draw_info_page(uint8_t page);
