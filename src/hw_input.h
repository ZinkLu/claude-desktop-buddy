#pragma once
#include <stdint.h>

enum InputEvent {
  EVT_NONE = 0,
  EVT_ROT_CW,
  EVT_ROT_CCW,
  EVT_CLICK,
  EVT_DOUBLE,
  EVT_LONG,
};

void       hw_input_init();
InputEvent hw_input_poll();   // Returns one pending event, or EVT_NONE
bool       hw_input_button_pressed();  // True if button is currently held

// Internal helpers, exposed for unit tests.
namespace hw_input_internal {
  // Button FSM: feed it (pressed, now_ms). Returns one of EVT_NONE / EVT_CLICK /
  // EVT_DOUBLE / EVT_LONG; stateful across calls. Thresholds:
  //   LONG_MS   = 600
  //   CLICK_MAX = 500
  //   DOUBLE_GAP_MS = 300
  struct ButtonFSM {
    bool    prevPressed    = false;
    uint32_t pressStartMs  = 0;
    uint32_t lastReleaseMs = 0;
    bool    longFired      = false;
    bool    awaitingDouble = false;
    uint32_t awaitExpireMs = 0;
  };
  InputEvent buttonStep(ButtonFSM& s, bool pressed, uint32_t now);

  // Encoder delta: tracks accumulated raw angle (degrees, float). Every time
  // accumulated angle crosses ±DETENT_DEG (15.0) from last emit, emit ROT_CW
  // or ROT_CCW. Returns EVT_NONE if no detent crossed.
  struct EncoderFSM {
    float lastEmitDeg = 0.0f;
  };
  InputEvent encoderStep(EncoderFSM& s, float currentDeg);
}
