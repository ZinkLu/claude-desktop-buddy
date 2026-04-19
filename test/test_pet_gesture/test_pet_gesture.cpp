#include <unity.h>
#include "../../src/pet_gesture.h"
#include "../../src/hw_input.h"

void setUp() { pet_gesture_internal::reset_for_tests(); }
void tearDown() {}

// Helper to feed N events with spacing gap_ms.
static PetGesture feed(InputEvent events[], int n, uint32_t t0, uint32_t gap_ms) {
  PetGesture last = PGEST_NONE;
  for (int i = 0; i < n; i++) {
    PetGesture g = pet_gesture_step(events[i], t0 + i * gap_ms);
    if (g != PGEST_NONE) last = g;
  }
  return last;
}

void test_no_events_returns_none() {
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_NONE, 1000));
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_CLICK, 1100));
}

void test_single_rotation_returns_none() {
  TEST_ASSERT_EQUAL(PGEST_NONE, pet_gesture_step(EVT_ROT_CW, 1000));
}

void test_three_alternations_returns_none() {
  // 3 events is below the stroke threshold (4+).
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_NONE, feed(seq, 3, 1000, 50));
}

void test_four_alternations_within_400ms_is_stroke() {
  // 4 events in 300 ms, directions alternate twice.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq, 4, 1000, 80));
}

void test_four_uniform_within_400ms_is_not_stroke() {
  // 4 events same direction, fast. Only 1 direction change = 0 alternations
  // — this is a borderline case but by our rule "2+ alternations" it's not stroke.
  // Also not tickle unless we hit 5 events within 250 ms.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_NONE, feed(seq, 4, 1000, 80));
}

void test_five_uniform_within_250ms_is_tickle() {
  // 5 same-direction events in 200 ms.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  TEST_ASSERT_EQUAL(PGEST_TICKLE, feed(seq, 5, 1000, 40));
}

void test_five_spread_over_600ms_is_not_tickle() {
  // 5 same-direction events spread slowly — not tickle.
  InputEvent seq[] = { EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW, EVT_ROT_CW };
  // 5 events over 600 ms = gap 150 ms, which is outside 250 ms window.
  TEST_ASSERT_EQUAL(PGEST_NONE, feed(seq, 5, 1000, 150));
}

void test_cooldown_prevents_double_stroke() {
  // First stroke fires, then another set of alternations 100 ms later should not.
  InputEvent seq1[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq1, 4, 1000, 80));
  // Now feed more events within 300 ms cooldown — must return NONE despite pattern.
  InputEvent seq2[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_NONE, feed(seq2, 4, 1350, 40));
}

void test_cooldown_expires() {
  InputEvent seq1[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq1, 4, 1000, 80));
  // 500 ms after the stroke fired — well past the 300 ms cooldown.
  InputEvent seq2[] = { EVT_ROT_CW, EVT_ROT_CCW, EVT_ROT_CW, EVT_ROT_CCW };
  TEST_ASSERT_EQUAL(PGEST_STROKE, feed(seq2, 4, 1800, 80));
}

void test_non_rotation_events_ignored() {
  // Feeding CLICK between rotations should not break pattern detection.
  pet_gesture_step(EVT_ROT_CW,  1000);
  pet_gesture_step(EVT_CLICK,   1020);   // ignored
  pet_gesture_step(EVT_ROT_CCW, 1080);
  pet_gesture_step(EVT_ROT_CW,  1160);
  PetGesture g = pet_gesture_step(EVT_ROT_CCW, 1240);
  TEST_ASSERT_EQUAL(PGEST_STROKE, g);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_events_returns_none);
  RUN_TEST(test_single_rotation_returns_none);
  RUN_TEST(test_three_alternations_returns_none);
  RUN_TEST(test_four_alternations_within_400ms_is_stroke);
  RUN_TEST(test_four_uniform_within_400ms_is_not_stroke);
  RUN_TEST(test_five_uniform_within_250ms_is_tickle);
  RUN_TEST(test_five_spread_over_600ms_is_not_tickle);
  RUN_TEST(test_cooldown_prevents_double_stroke);
  RUN_TEST(test_cooldown_expires);
  RUN_TEST(test_non_rotation_events_ignored);
  return UNITY_END();
}
