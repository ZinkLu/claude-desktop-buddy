#pragma once
#include <stdint.h>
#include "hw_input.h"

enum DisplayMode {
  DISP_HOME = 0,
  DISP_CLOCK,
  DISP_MENU,
  DISP_SETTINGS,
  DISP_RESET,
  DISP_HELP,
  DISP_ABOUT,
  DISP_PASSKEY,
  DISP_PET,
  DISP_PET_STATS,
  DISP_INFO,
};

// Side-effect callbacks the FSM invokes in response to menu actions.
// All are optional (any NULL pointer is silently skipped). Register once
// at init.
struct FsmCallbacks {
  // Action items
  void (*turn_off)();
  void (*delete_char)();
  void (*factory_reset)();
  void (*toggle_demo)();

  // Settings changes — payload = the new level (0..4) or new bool.
  void (*brightness_changed)(uint8_t level);
  void (*haptic_changed)(uint8_t level);
  void (*transcript_changed)(bool on);

  // Render invalidations
  void (*invalidate_clock)();
  void (*invalidate_buddy)();
  void (*invalidate_panel)();   // repaint the active menu panel

  // NEW in Phase 2-B
  void (*on_enter_pet)();                      // greeting pulse, gesture reset
  void (*on_exit_pet)();                       // bye pulse, stop purr
  void (*on_pet_rotation)(bool cw);            // forwarded for gesture classifier
  void (*on_pet_long_press)();                 // squish vibration
  void (*on_info_page_change)(uint8_t page);   // repaint info page
  void (*on_hud_scroll_change)(uint8_t ofs);   // repaint home HUD strip
};

// Snapshot of FSM state for renderers. All fields read-only from outside.
struct FsmView {
  DisplayMode mode;
  uint8_t menuSel;
  uint8_t settingsSel;
  uint8_t resetSel;
  uint8_t resetConfirmIdx;    // 0xFF if not armed
  uint32_t resetConfirmUntil; // millis() expiry; 0 if not armed
  uint8_t infoPage;      // 0..3
  uint8_t hudScroll;     // 0..30 (newest..oldest)
};

// API
void input_fsm_init(const FsmCallbacks* cb);
void input_fsm_dispatch(InputEvent e, uint32_t now_ms);
void input_fsm_tick(uint32_t now_ms);    // clears expired reset arm; called each main loop
void input_fsm_on_passkey_change(bool active);  // called when blePasskey() (non-)zero transitions
void input_fsm_force_home_on_prompt();
const FsmView& input_fsm_view();

// Internal helpers exposed for unit tests only.
namespace input_fsm_internal {
  // Reset all FSM state to defaults. Used by test setUp() and input_fsm_init.
  void reset_for_tests();
}
