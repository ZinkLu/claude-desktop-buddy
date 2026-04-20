#pragma once
#include <stdint.h>
#include "hw_input.h"

enum PetGesture {
  PGEST_NONE = 0,
  PGEST_STROKE,
  PGEST_TICKLE,
};

// Feed each rotation event. Returns a gesture classification or NONE.
// A cooldown prevents double-firing of the same gesture within 300 ms.
// `e` must be EVT_ROT_CW, EVT_ROT_CCW, or anything else (which is ignored).
PetGesture pet_gesture_step(InputEvent e, uint32_t now_ms);

// Reset the rolling history buffer and cooldown. Call when entering pet mode.
void pet_gesture_reset();

namespace pet_gesture_internal {
  // Size of the rolling event buffer.
  constexpr uint8_t BUF_SZ = 8;

  // Reset; for use by unit tests.
  void reset_for_tests();
}
