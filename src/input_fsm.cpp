#include "input_fsm.h"
#include <string.h>

// Menu item indices
//   Main:      0 settings, 1 clock, 2 turn off, 3 help, 4 about, 5 demo, 6 close
//   Settings:  0 brightness, 1 haptic, 2 transcript, 3 reset, 4 back
//   Reset:     0 delete char, 1 factory reset, 2 back

static const uint8_t MENU_N     = 7;
static const uint8_t SETTINGS_N = 5;
static const uint8_t RESET_N    = 3;
static const uint8_t INFO_N        = 4;
static const uint8_t HUD_MAX_SCROLL = 30;
static const uint32_t RESET_CONFIRM_WINDOW_MS = 3000;

// Module state
static FsmCallbacks _cb = {};
static FsmView _v;
static DisplayMode _previousForPasskey = DISP_HOME;
static bool _passkeyActive = false;

static void _reset_state() {
  _v.mode = DISP_HOME;
  _v.menuSel = 0;
  _v.settingsSel = 0;
  _v.resetSel = 0;
  _v.resetConfirmIdx = 0xFF;
  _v.resetConfirmUntil = 0;
  _v.infoPage = 0;
  _v.hudScroll = 0;
  _previousForPasskey = DISP_HOME;
  _passkeyActive = false;
}

void input_fsm_init(const FsmCallbacks* cb) {
  if (cb) _cb = *cb;
  _reset_state();
}

namespace input_fsm_internal {
void reset_for_tests() { _reset_state(); _cb = {}; }
}

const FsmView& input_fsm_view() { return _v; }

// Safe callback invocation — any null pointer is silently ignored.
#define CALL0(fn)      do { if (_cb.fn) _cb.fn(); } while (0)
#define CALL1(fn, a)   do { if (_cb.fn) _cb.fn(a); } while (0)

static void _enter(DisplayMode m) {
  _v.mode = m;
}

static void _clear_reset_arm() {
  _v.resetConfirmIdx = 0xFF;
  _v.resetConfirmUntil = 0;
}

static void _go_home() {
  if (_v.mode == DISP_PET || _v.mode == DISP_PET_STATS) {
    CALL0(on_exit_pet);
  }
  _v.mode = DISP_HOME;
  _clear_reset_arm();
  CALL0(invalidate_buddy);
}

// Menu item 0..6; returns the side-effect for CLICK.
static void _menu_click(uint8_t idx) {
  switch (idx) {
    case 0: _enter(DISP_SETTINGS); _v.settingsSel = 0; _clear_reset_arm(); CALL0(invalidate_panel); break;
    case 1: _enter(DISP_CLOCK); CALL0(invalidate_clock); break;
    case 2: CALL0(turn_off); break;    // never returns; FSM state irrelevant after
    case 3: _enter(DISP_INFO); _v.infoPage = 0; CALL0(invalidate_panel); CALL1(on_info_page_change, 0); break;  // help -> info 0
    case 4: _enter(DISP_INFO); _v.infoPage = 3; CALL0(invalidate_panel); CALL1(on_info_page_change, 3); break;  // about -> info 3
    case 5: CALL0(toggle_demo); CALL0(invalidate_panel); break;   // stays in menu
    case 6: _go_home(); break;
    default: break;
  }
}

// Settings items 0..4
static void _settings_click(uint8_t idx) {
  switch (idx) {
    case 0: CALL1(brightness_changed, 0); CALL0(invalidate_panel); break;   // main owns cycling
    case 1: CALL1(haptic_changed, 0);     CALL0(invalidate_panel); break;
    case 2: CALL1(transcript_changed, false); CALL0(invalidate_panel); break;
    case 3: _enter(DISP_RESET); _v.resetSel = 0; _clear_reset_arm(); CALL0(invalidate_panel); break;
    case 4: _enter(DISP_MENU); _v.menuSel = 0; CALL0(invalidate_panel); break;   // 'back' → main menu
    default: break;
  }
}

// Reset items 0..2 with double-confirm
static void _reset_click(uint8_t idx, uint32_t now) {
  if (idx == 2) {                      // back
    _clear_reset_arm();
    _enter(DISP_SETTINGS);
    CALL0(invalidate_panel);
    return;
  }
  bool armed = (_v.resetConfirmIdx == idx) && (now < _v.resetConfirmUntil);
  if (!armed) {
    _v.resetConfirmIdx = idx;
    _v.resetConfirmUntil = now + RESET_CONFIRM_WINDOW_MS;
    CALL0(invalidate_panel);
    return;
  }
  _clear_reset_arm();
  if (idx == 0) CALL0(delete_char);
  else          CALL0(factory_reset);
}

void input_fsm_dispatch(InputEvent e, uint32_t now_ms) {
  // Passkey overlay swallows all input.
  if (_passkeyActive) return;

  // --- Home CLICK cycle / HUD scroll (Phase 2-B) --------------------------
  if (_v.mode == DISP_HOME) {
    if (e == EVT_CLICK) {
      // Only enter pet if no prompt is active. (Prompt handling is in
      // main.cpp, which checks inPrompt before calling dispatch for CLICK.
      // Here we assume the caller has already decided to route the click
      // to the FSM — i.e., not in prompt.)
      _enter(DISP_PET);
      CALL0(on_enter_pet);
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_ROT_CW) {
      if (_v.hudScroll < HUD_MAX_SCROLL) _v.hudScroll++;
      CALL1(on_hud_scroll_change, _v.hudScroll);
      return;
    }
    if (e == EVT_ROT_CCW) {
      if (_v.hudScroll > 0) _v.hudScroll--;
      CALL1(on_hud_scroll_change, _v.hudScroll);
      return;
    }
    // EVT_LONG falls through to existing "home -> menu" handler below.
  }

  // --- Pet mode ------------------------------------------------------------
  if (_v.mode == DISP_PET) {
    if (e == EVT_ROT_CW || e == EVT_ROT_CCW) {
      CALL1(on_pet_rotation, (e == EVT_ROT_CW));
      return;
    }
    if (e == EVT_CLICK) {
      CALL0(on_exit_pet);
      _enter(DISP_INFO); _v.infoPage = 0;
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_DOUBLE) {
      _enter(DISP_PET_STATS);
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_LONG) {
      CALL0(on_pet_long_press);
      // _go_home() handles on_exit_pet since mode is still DISP_PET here.
      _go_home();
      return;
    }
  }

  // --- Pet stats sub-page --------------------------------------------------
  if (_v.mode == DISP_PET_STATS) {
    if (e == EVT_CLICK || e == EVT_DOUBLE) {
      _enter(DISP_PET);
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_LONG) {
      // _go_home() handles on_exit_pet since mode is still DISP_PET_STATS here.
      _go_home();
      return;
    }
    // Rotation is a no-op.
    return;
  }

  // --- Info mode -----------------------------------------------------------
  if (_v.mode == DISP_INFO) {
    if (e == EVT_ROT_CW) {
      _v.infoPage = (_v.infoPage + 1) % INFO_N;
      CALL1(on_info_page_change, _v.infoPage);
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_ROT_CCW) {
      _v.infoPage = (_v.infoPage + INFO_N - 1) % INFO_N;
      CALL1(on_info_page_change, _v.infoPage);
      CALL0(invalidate_panel);
      return;
    }
    if (e == EVT_CLICK || e == EVT_LONG) {
      _go_home();
      return;
    }
    return;
  }

  // LONG from anywhere (except HOME) is "get out to home".
  if (e == EVT_LONG) {
    switch (_v.mode) {
      case DISP_HOME:
        _enter(DISP_MENU);
        _v.menuSel = 0;
        _clear_reset_arm();
        CALL0(invalidate_panel);
        return;
      case DISP_HELP:
      case DISP_ABOUT:
        // LONG from help/about returns to menu, not home — users got there from menu.
        _enter(DISP_MENU);
        CALL0(invalidate_panel);
        return;
      default:
        _go_home();
        return;
    }
  }

  // Rotation
  if (e == EVT_ROT_CW || e == EVT_ROT_CCW) {
    int dir = (e == EVT_ROT_CW) ? 1 : -1;
    switch (_v.mode) {
      case DISP_MENU:
        _v.menuSel = (uint8_t)((_v.menuSel + dir + MENU_N) % MENU_N);
        CALL0(invalidate_panel);
        return;
      case DISP_SETTINGS:
        _v.settingsSel = (uint8_t)((_v.settingsSel + dir + SETTINGS_N) % SETTINGS_N);
        CALL0(invalidate_panel);
        return;
      case DISP_RESET:
        _v.resetSel = (uint8_t)((_v.resetSel + dir + RESET_N) % RESET_N);
        _clear_reset_arm();  // scrolling cancels arm
        CALL0(invalidate_panel);
        return;
      default:
        return;  // home/clock/help/about/passkey ignore rotation
    }
  }

  // CLICK
  if (e == EVT_CLICK) {
    switch (_v.mode) {
      case DISP_MENU:     _menu_click(_v.menuSel); return;
      case DISP_SETTINGS: _settings_click(_v.settingsSel); return;
      case DISP_RESET:    _reset_click(_v.resetSel, now_ms); return;
      case DISP_HELP:
      case DISP_ABOUT:
        _enter(DISP_MENU);
        CALL0(invalidate_panel);
        return;
      default:
        return;  // home click is handled by approval flow in main.cpp;
                 // FSM ignores it here. Clock click is ignored too.
    }
  }

  // DOUBLE is unused across all modes.
}

void input_fsm_tick(uint32_t now_ms) {
  if (_v.resetConfirmIdx != 0xFF && now_ms >= _v.resetConfirmUntil) {
    _clear_reset_arm();
    if (_v.mode == DISP_RESET) CALL0(invalidate_panel);
  }
}

void input_fsm_on_passkey_change(bool active) {
  if (active && !_passkeyActive) {
    _previousForPasskey = _v.mode;
    _v.mode = DISP_PASSKEY;
    _passkeyActive = true;
    CALL0(invalidate_panel);
  } else if (!active && _passkeyActive) {
    _v.mode = _previousForPasskey;
    _passkeyActive = false;
    // Repaint whatever comes back.
    if (_v.mode == DISP_HOME)  CALL0(invalidate_buddy);
    else if (_v.mode == DISP_CLOCK) CALL0(invalidate_clock);
    else CALL0(invalidate_panel);
  }
}

void input_fsm_force_home_on_prompt() {
  if (_passkeyActive) return;   // passkey wins
  if (_v.mode == DISP_HOME) return;
  _go_home();
}
