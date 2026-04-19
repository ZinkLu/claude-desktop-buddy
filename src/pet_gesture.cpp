#include "pet_gesture.h"
#include <string.h>

namespace {

struct Event {
  uint32_t t;
  int8_t   dir;   // +1 CW, -1 CCW
};

// Tickle: a burst of same-direction events. Picks up "spinning the knob".
// 4 events in 400 ms = ~10 Hz — a normal quick flick is enough.
static const uint32_t TICKLE_WINDOW_MS       = 400;
static const uint8_t  TICKLE_MIN_EVENTS      = 4;
static const uint32_t TICKLE_COOLDOWN_MS     = 500;

Event    _buf[pet_gesture_internal::BUF_SZ];
uint8_t  _head = 0;
uint8_t  _count = 0;
uint32_t _tickle_cooldown_until = 0;

void push(uint32_t t, int8_t dir) {
  _buf[_head] = {t, dir};
  _head = (_head + 1) % pet_gesture_internal::BUF_SZ;
  if (_count < pet_gesture_internal::BUF_SZ) _count++;
}

// Read the i-th most recent event (0 = most recent).
const Event& recent(uint8_t i) {
  uint8_t idx = (_head + pet_gesture_internal::BUF_SZ - 1 - i) % pet_gesture_internal::BUF_SZ;
  return _buf[idx];
}

}  // namespace

void pet_gesture_reset() {
  _head = 0;
  _count = 0;
  _tickle_cooldown_until = 0;
  memset(_buf, 0, sizeof(_buf));
}

namespace pet_gesture_internal {
void reset_for_tests() { pet_gesture_reset(); }
}  // namespace pet_gesture_internal

// Design note
// ===========
// Stroking a rotary knob is usually a slow single-direction motion, not the
// back-and-forth "shake" I originally coded for. So in this model ANY
// rotation event counts as a stroke — that's how people actually pet a knob.
// Tickle is the outlier: rapid continuous spinning in one direction.
//
//   tickle = 5+ same-direction events within 250 ms
//   stroke = any rotation event (below tickle threshold)
//
// After tickle fires, a 500 ms cooldown suppresses further classification
// so the user's still-spinning-knob doesn't cascade into stroke → tickle →
// stroke. Stroke itself has no cooldown — it fires on every rotation, so
// the caller can keep refreshing its "still being stroked" timer.
PetGesture pet_gesture_step(InputEvent e, uint32_t now_ms) {
  if (e != EVT_ROT_CW && e != EVT_ROT_CCW) return PGEST_NONE;
  push(now_ms, (e == EVT_ROT_CW) ? 1 : -1);

  // During tickle cooldown, report nothing — gives the user a moment to
  // calm down before more gestures register.
  if (now_ms < _tickle_cooldown_until) return PGEST_NONE;

  // Try TICKLE first: 5+ uniform-direction events within 250 ms.
  if (_count >= TICKLE_MIN_EVENTS) {
    uint32_t oldest = recent(TICKLE_MIN_EVENTS - 1).t;
    if (now_ms - oldest <= TICKLE_WINDOW_MS) {
      int8_t dir0 = recent(0).dir;
      bool uniform = true;
      for (uint8_t i = 1; i < TICKLE_MIN_EVENTS; i++) {
        if (recent(i).dir != dir0) { uniform = false; break; }
      }
      if (uniform) {
        _tickle_cooldown_until = now_ms + TICKLE_COOLDOWN_MS;
        return PGEST_TICKLE;
      }
    }
  }

  // Default: any rotation event is a stroke.
  return PGEST_STROKE;
}
