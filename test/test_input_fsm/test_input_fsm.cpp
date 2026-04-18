#include <unity.h>
#include "../../src/hw_input.h"

using namespace hw_input_internal;

void setUp() {}
void tearDown() {}

void test_no_press_no_event() {
  ButtonFSM s;
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, false, 0));
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, false, 100));
}

void test_short_press_emits_click() {
  ButtonFSM s;
  // Press at t=0
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 0));
  // Still pressed at t=100
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 100));
  // Released at t=200 — below 500 ms click max, above 0 duration
  // First release does NOT emit immediately; we wait for DOUBLE window.
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, false, 200));
  // After DOUBLE gap (300 ms) elapsed, emit CLICK
  TEST_ASSERT_EQUAL(EVT_CLICK, buttonStep(s, false, 501));
}

void test_long_press_emits_long_once() {
  ButtonFSM s;
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 0));
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 500));
  // At 600 ms LONG fires
  TEST_ASSERT_EQUAL(EVT_LONG, buttonStep(s, true, 600));
  // Holding continues — must NOT refire
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 1000));
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 2000));
  // Release should NOT emit CLICK either (LONG consumed the press)
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, false, 2100));
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, false, 2500));
}

void test_double_click() {
  ButtonFSM s;
  // First click
  buttonStep(s, true, 0);
  buttonStep(s, false, 100);
  // Second press before double-gap
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 200));
  // Second release — emit DOUBLE immediately
  TEST_ASSERT_EQUAL(EVT_DOUBLE, buttonStep(s, false, 300));
}

void test_two_slow_clicks_emit_two_clicks() {
  ButtonFSM s;
  buttonStep(s, true, 0);
  buttonStep(s, false, 100);
  // DOUBLE window (300 ms) expires -> emit CLICK
  TEST_ASSERT_EQUAL(EVT_CLICK, buttonStep(s, false, 401));
  // Second click starts well after
  buttonStep(s, true, 1000);
  buttonStep(s, false, 1100);
  TEST_ASSERT_EQUAL(EVT_CLICK, buttonStep(s, false, 1401));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_press_no_event);
  RUN_TEST(test_short_press_emits_click);
  RUN_TEST(test_long_press_emits_long_once);
  RUN_TEST(test_double_click);
  RUN_TEST(test_two_slow_clicks_emit_two_clicks);
  return UNITY_END();
}
