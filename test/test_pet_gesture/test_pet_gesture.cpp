#include <unity.h>
#include "../../src/pet_gesture.h"
#include "../../src/hw_input.h"

void setUp() { pet_gesture_internal::reset_for_tests(); }
void tearDown() {}

static PetGesture feed(InputEvent events[], int n, uint32_t t0, uint32_t gap_ms) {
  PetGesture last = PGEST_NONE;
  for (int i = 0; i < n; i++) {
    PetGesture g = pet_gesture_step(events[i], t0 + i * gap_ms);
    if (g != PGEST_NONE) last = g;
  }
  return last;
}

// Non-rotation events are ignored.
void test_non_rotation_returns_none() {
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_NONE, 1000));
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_CLICK, 1100));
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_LONG, 1200));
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_DOUBLE, 1300));
}

// Any rotation = stroke. New simpler model: slow rotation is the natural
// petting gesture; no alternation or timing pattern required.
void test_single_rotation_is_stroke() {
  TEST_ASSERT_EQUAL(PGEST_STROKE, pet_gesture_step(EVT_ROT_CW, 1000));
}

void test_slow_single_direction_is_stroke() {
  // Slow rotation, well under tickle threshold — classic petting.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq, 3, 1000, 300));
}

void test_alternating_rotation_is_stroke() {
  // Back-and-forth counts as stroke too.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq, 4, 1000, 80));
}

// Tickle: 5+ same-direction events within 250 ms.
void test_rapid_same_direction_is_tickle() {
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_TICKLE, feed(seq, 5, 1000, 40));
}

void test_slow_same_direction_is_not_tickle() {
  // 5 same-direction events, but spread over 600 ms → still stroke, not tickle.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq, 5, 1000, 150));
}

void test_rapid_alternating_is_stroke_not_tickle() {
  // 5 rapid events but directions alternate — tickle requires uniform direction.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq, 5, 1000, 40));
}

// Tickle cooldown prevents follow-up classification for 500 ms.
void test_tickle_cooldown_suppresses_stroke() {
  InputEvent tickle[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_TICKLE, feed(tickle, 5, 1000, 40));
  // 100 ms after the tickle (still well within 500 ms cooldown) — NONE.
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_ROT_CW, 1260));
}

void test_tickle_cooldown_expires() {
  InputEvent tickle[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_TICKLE, feed(tickle, 5, 1000, 40));
  // 600 ms past the tickle (cooldown is 500 ms) — stroke again.
  TEST_ASSERT_EQUAL(PGEST_STROKE, pet_gesture_step(EVT_ROT_CW, 1800));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_non_rotation_returns_none);
  RUN_TEST(test_single_rotation_is_stroke);
  RUN_TEST(test_slow_single_direction_is_stroke);
  RUN_TEST(test_alternating_rotation_is_stroke);
  RUN_TEST(test_rapid_same_direction_is_tickle);
  RUN_TEST(test_slow_same_direction_is_not_tickle);
  RUN_TEST(test_rapid_alternating_is_stroke_not_tickle);
  RUN_TEST(test_tickle_cooldown_suppresses_stroke);
  RUN_TEST(test_tickle_cooldown_expires);
  return UNITY_END();
}
