#include <unity.h>
#include <time.h>
#include <string.h>
#include "../../src/clock_face.h"

using namespace clock_face_internal;

static struct tm mktm(int hour, int min, int sec, int mday, int mon, int wday) {
  struct tm t{};
  t.tm_hour = hour;
  t.tm_min  = min;
  t.tm_sec  = sec;
  t.tm_mday = mday;
  t.tm_mon  = mon;   // 0-based
  t.tm_wday = wday;  // 0 = Sunday
  return t;
}

void setUp() {}
void tearDown() {}

void test_fmt_typical() {
  struct tm t = mktm(12, 34, 56, 18, 3 /* Apr */, 5 /* Fri */);
  char hm[6], ss[3], date[16];
  fmt_time_fields(t, hm, ss, date);
  TEST_ASSERT_EQUAL_STRING("12:34", hm);
  TEST_ASSERT_EQUAL_STRING("56", ss);
  TEST_ASSERT_EQUAL_STRING("Fri Apr 18", date);
}

void test_fmt_zero_pad_day() {
  struct tm t = mktm(9, 5, 3, 1, 0 /* Jan */, 0 /* Sun */);
  char hm[6], ss[3], date[16];
  fmt_time_fields(t, hm, ss, date);
  TEST_ASSERT_EQUAL_STRING("09:05", hm);
  TEST_ASSERT_EQUAL_STRING("03", ss);
  TEST_ASSERT_EQUAL_STRING("Sun Jan 01", date);
}

void test_fmt_midnight() {
  struct tm t = mktm(0, 0, 0, 31, 11 /* Dec */, 6 /* Sat */);
  char hm[6], ss[3], date[16];
  fmt_time_fields(t, hm, ss, date);
  TEST_ASSERT_EQUAL_STRING("00:00", hm);
  TEST_ASSERT_EQUAL_STRING("00", ss);
  TEST_ASSERT_EQUAL_STRING("Sat Dec 31", date);
}

void test_needs_redraw_nothing_changed() {
  struct tm t = mktm(12, 34, 56, 18, 3, 5);
  int cm = 34, cs = 56, cd = 18;
  TEST_ASSERT_FALSE(needs_redraw(t, cm, cs, cd));
  TEST_ASSERT_EQUAL(34, cm);
  TEST_ASSERT_EQUAL(56, cs);
  TEST_ASSERT_EQUAL(18, cd);
}

void test_needs_redraw_second_tick() {
  struct tm t = mktm(12, 34, 57, 18, 3, 5);
  int cm = 34, cs = 56, cd = 18;
  TEST_ASSERT_TRUE(needs_redraw(t, cm, cs, cd));
  TEST_ASSERT_EQUAL(57, cs);
  TEST_ASSERT_EQUAL(34, cm);
  TEST_ASSERT_EQUAL(18, cd);
}

void test_needs_redraw_minute_rolls() {
  struct tm t = mktm(12, 35, 0, 18, 3, 5);
  int cm = 34, cs = 59, cd = 18;
  TEST_ASSERT_TRUE(needs_redraw(t, cm, cs, cd));
  TEST_ASSERT_EQUAL(35, cm);
  TEST_ASSERT_EQUAL(0, cs);
}

void test_needs_redraw_day_rolls() {
  struct tm t = mktm(0, 0, 0, 19, 3, 6);
  int cm = 59, cs = 59, cd = 18;
  TEST_ASSERT_TRUE(needs_redraw(t, cm, cs, cd));
  TEST_ASSERT_EQUAL(19, cd);
}

void test_needs_redraw_first_call_with_sentinel_cache() {
  struct tm t = mktm(0, 0, 0, 1, 0, 0);
  int cm = -1, cs = -1, cd = -1;
  TEST_ASSERT_TRUE(needs_redraw(t, cm, cs, cd));
  TEST_ASSERT_EQUAL(0, cm);
  TEST_ASSERT_EQUAL(0, cs);
  TEST_ASSERT_EQUAL(1, cd);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fmt_typical);
  RUN_TEST(test_fmt_zero_pad_day);
  RUN_TEST(test_fmt_midnight);
  RUN_TEST(test_needs_redraw_nothing_changed);
  RUN_TEST(test_needs_redraw_second_tick);
  RUN_TEST(test_needs_redraw_minute_rolls);
  RUN_TEST(test_needs_redraw_day_rolls);
  RUN_TEST(test_needs_redraw_first_call_with_sentinel_cache);
  return UNITY_END();
}
