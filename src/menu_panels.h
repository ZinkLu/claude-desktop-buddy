#pragma once

// Each function paints to the shared hw_display_sprite and pushes it.
// Called from main.cpp after input_fsm transitions to the corresponding
// mode, or on invalidate_panel callback.
void draw_main_menu();
void draw_settings();
void draw_reset();
void draw_help();
void draw_about();
void draw_passkey();
