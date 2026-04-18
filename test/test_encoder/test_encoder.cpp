#include <unity.h>
#include "../../src/hw_input.h"

using namespace hw_input_internal;

void setUp() {}
void tearDown() {}

void test_no_motion_no_event() {
  EncoderFSM s;
  TEST_ASSERT_EQUAL(EVT_NONE, encoderStep(s, 0.0f));
  TEST_ASSERT_EQUAL(EVT_NONE, encoderStep(s, 5.0f));
  TEST_ASSERT_EQUAL(EVT_NONE, encoderStep(s, 14.9f));
}

void test_cw_detent() {
  EncoderFSM s;
  TEST_ASSERT_EQUAL(EVT_ROT_CW, encoderStep(s, 15.0f));
  // Need another 15 deg to emit again
  TEST_ASSERT_EQUAL(EVT_NONE, encoderStep(s, 20.0f));
  TEST_ASSERT_EQUAL(EVT_ROT_CW, encoderStep(s, 30.0f));
}

void test_ccw_detent() {
  EncoderFSM s;
  TEST_ASSERT_EQUAL(EVT_ROT_CCW, encoderStep(s, -15.0f));
  TEST_ASSERT_EQUAL(EVT_ROT_CCW, encoderStep(s, -30.0f));
}

void test_reversal() {
  EncoderFSM s;
  TEST_ASSERT_EQUAL(EVT_ROT_CW,  encoderStep(s, 15.0f));
  TEST_ASSERT_EQUAL(EVT_NONE,    encoderStep(s, 10.0f));
  // From emit point 15, go to 0 = -15 delta → CCW
  TEST_ASSERT_EQUAL(EVT_ROT_CCW, encoderStep(s, 0.0f));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_motion_no_event);
  RUN_TEST(test_cw_detent);
  RUN_TEST(test_ccw_detent);
  RUN_TEST(test_reversal);
  return UNITY_END();
}
