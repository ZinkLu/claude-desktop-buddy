#include "pet_gesture.h"
#include <string.h>

namespace {

struct Event {
  uint32_t t;
  int8_t   dir;   // +1 CW, -1 CCW
};

static const uint32_t STROKE_WINDOW_MS   = 400;
static const uint8_t  STROKE_MIN_EVENTS  = 4;
static const uint8_t  STROKE_MIN_ALTS    = 2;
static const uint32_t TICKLE_WINDOW_MS   = 250;
static const uint8_t  TICKLE_MIN_EVENTS  = 5;
static const uint32_t COOLDOWN_MS        = 300;

Event    _buf[pet_gesture_internal::BUF_SZ];
uint8_t  _head = 0;
uint8_t  _count = 0;
uint32_t _cooldown_until = 0;

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
  _cooldown_until = 0;
  memset(_buf, 0, sizeof(_buf));
}

namespace pet_gesture_internal {
void reset_for_tests() { pet_gesture_reset(); }
}  // namespace pet_gesture_internal

PetGesture pet_gesture_step(InputEvent e, uint32_t now_ms) {
  if (e != EVT_ROT_CW && e != EVT_ROT_CCW) return PGEST_NONE;
  push(now_ms, (e == EVT_ROT_CW) ? 1 : -1);

  if (now_ms < _cooldown_until) return PGEST_NONE;

  // Try TICKLE: 5+ uniform events in 250 ms.
  if (_count >= TICKLE_MIN_EVENTS) {
    uint32_t oldest = recent(TICKLE_MIN_EVENTS - 1).t;
    if (now_ms - oldest <= TICKLE_WINDOW_MS) {
      int8_t dir0 = recent(0).dir;
      bool uniform = true;
      for (uint8_t i = 1; i < TICKLE_MIN_EVENTS; i++) {
        if (recent(i).dir != dir0) { uniform = false; break; }
      }
      if (uniform) {
        _cooldown_until = now_ms + COOLDOWN_MS;
        return PGEST_TICKLE;
      }
    }
  }

  // Try STROKE: 4+ events in 400 ms with 2+ alternations.
  if (_count >= STROKE_MIN_EVENTS) {
    uint32_t oldest = recent(STROKE_MIN_EVENTS - 1).t;
    if (now_ms - oldest <= STROKE_WINDOW_MS) {
      uint8_t alternations = 0;
      for (uint8_t i = 0; i + 1 < STROKE_MIN_EVENTS; i++) {
        if (recent(i).dir != recent(i + 1).dir) alternations++;
      }
      if (alternations >= STROKE_MIN_ALTS) {
        _cooldown_until = now_ms + COOLDOWN_MS;
        return PGEST_STROKE;
      }
    }
  }

  return PGEST_NONE;
}
