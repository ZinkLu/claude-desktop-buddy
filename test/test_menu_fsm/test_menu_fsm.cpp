#include <unity.h>
#include "../../src/input_fsm.h"
#include "../../src/hw_input.h"

// --- Test doubles for the callback struct -------------------------------

struct MockCalls {
  int turn_off;
  int delete_char;
  int factory_reset;
  int toggle_demo;
  int brightness_changed;
  uint8_t last_brightness;
  int haptic_changed;
  uint8_t last_haptic;
  int transcript_changed;
  bool last_transcript;
  int invalidate_clock;
  int invalidate_buddy;
  int invalidate_panel;
};

static MockCalls mock;
static void mk_turn_off()                       { mock.turn_off++; }
static void mk_delete_char()                    { mock.delete_char++; }
static void mk_factory_reset()                  { mock.factory_reset++; }
static void mk_toggle_demo()                    { mock.toggle_demo++; }
static void mk_brightness(uint8_t l)            { mock.brightness_changed++; mock.last_brightness = l; }
static void mk_haptic(uint8_t l)                { mock.haptic_changed++;     mock.last_haptic = l; }
static void mk_transcript(bool on)              { mock.transcript_changed++; mock.last_transcript = on; }
static void mk_invalidate_clock()               { mock.invalidate_clock++; }
static void mk_invalidate_buddy()               { mock.invalidate_buddy++; }
static void mk_invalidate_panel()               { mock.invalidate_panel++; }

static const FsmCallbacks CB = {
  mk_turn_off, mk_delete_char, mk_factory_reset, mk_toggle_demo,
  mk_brightness, mk_haptic, mk_transcript,
  mk_invalidate_clock, mk_invalidate_buddy, mk_invalidate_panel,
};

// --- Host-side shim for settings().haptic / brightness / hud ------------
// The FSM reads settings values via callbacks, not via stats.h, so tests
// don't need a shim.

void setUp() {
  mock = MockCalls{};
  input_fsm_internal::reset_for_tests();
  input_fsm_init(&CB);
}
void tearDown() {}

// --- Tests --------------------------------------------------------------

void test_init_is_home() {
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_long_home_to_menu() {
  input_fsm_dispatch(EVT_LONG, 1000);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().menuSel);
}

void test_long_menu_to_home() {
  input_fsm_dispatch(EVT_LONG, 1000);   // home -> menu
  input_fsm_dispatch(EVT_LONG, 1100);   // menu -> home
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.invalidate_buddy);  // home renderer invalidated on exit
}

void test_menu_scroll_wraps() {
  input_fsm_dispatch(EVT_LONG, 1000);   // -> menu, sel=0
  // 6 CCW should wrap past 0
  input_fsm_dispatch(EVT_ROT_CCW, 1100);
  TEST_ASSERT_EQUAL(6, input_fsm_view().menuSel);  // wrapped to last item (close)
}

void test_menu_click_clock_enters_clock() {
  input_fsm_dispatch(EVT_LONG, 1000);   // menu
  input_fsm_dispatch(EVT_ROT_CW, 1100); // sel 0 -> 1 (clock)
  input_fsm_dispatch(EVT_CLICK, 1200);
  TEST_ASSERT_EQUAL(DISP_CLOCK, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.invalidate_clock);
}

void test_menu_click_turn_off_calls_callback() {
  input_fsm_dispatch(EVT_LONG, 1000);
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);  // sel = 2 (turn off)
  input_fsm_dispatch(EVT_CLICK, 1300);
  TEST_ASSERT_EQUAL(1, mock.turn_off);
}

void test_menu_click_settings_opens_settings() {
  input_fsm_dispatch(EVT_LONG, 1000);
  // menuSel already at 0 (settings)
  input_fsm_dispatch(EVT_CLICK, 1100);
  TEST_ASSERT_EQUAL(DISP_SETTINGS, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().settingsSel);
}

void test_clock_long_returns_home() {
  input_fsm_dispatch(EVT_LONG, 1000);    // menu
  input_fsm_dispatch(EVT_ROT_CW, 1100);  // sel = 1 (clock)
  input_fsm_dispatch(EVT_CLICK, 1200);   // enter clock
  input_fsm_dispatch(EVT_LONG, 1300);    // exit to home
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_clock_rotation_is_noop() {
  input_fsm_dispatch(EVT_LONG, 1000);
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  input_fsm_dispatch(EVT_CLICK, 1200);   // into clock
  DisplayMode before = input_fsm_view().mode;
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CCW, 1400);
  TEST_ASSERT_EQUAL(before, input_fsm_view().mode);
}

void test_settings_brightness_cycles() {
  input_fsm_dispatch(EVT_LONG, 1000);     // menu
  input_fsm_dispatch(EVT_CLICK, 1100);    // into settings, sel=0 (brightness)
  input_fsm_dispatch(EVT_CLICK, 1200);    // click brightness
  TEST_ASSERT_EQUAL(1, mock.brightness_changed);
  // Level cycles via the callback; FSM doesn't track value, main.cpp does.
  // Just verify the callback fires on every CLICK.
  input_fsm_dispatch(EVT_CLICK, 1300);
  TEST_ASSERT_EQUAL(2, mock.brightness_changed);
}

void test_settings_back_returns_menu() {
  input_fsm_dispatch(EVT_LONG, 1000);     // menu
  input_fsm_dispatch(EVT_CLICK, 1100);    // settings
  // scroll to 'back' (item 4)
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CW, 1400);
  input_fsm_dispatch(EVT_ROT_CW, 1500);
  TEST_ASSERT_EQUAL(4, input_fsm_view().settingsSel);
  input_fsm_dispatch(EVT_CLICK, 1600);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().menuSel);
}

void test_settings_long_returns_home() {
  input_fsm_dispatch(EVT_LONG, 1000);
  input_fsm_dispatch(EVT_CLICK, 1100);
  input_fsm_dispatch(EVT_LONG, 1200);
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_reset_arm_then_execute() {
  input_fsm_dispatch(EVT_LONG, 1000);     // menu
  input_fsm_dispatch(EVT_CLICK, 1100);    // settings
  // scroll to 'reset' (item 3)
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CW, 1400);
  TEST_ASSERT_EQUAL(3, input_fsm_view().settingsSel);
  input_fsm_dispatch(EVT_CLICK, 1500);    // into reset submenu
  TEST_ASSERT_EQUAL(DISP_RESET, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().resetSel);  // delete char
  // First click: arms
  input_fsm_dispatch(EVT_CLICK, 1600);
  TEST_ASSERT_EQUAL(0, input_fsm_view().resetConfirmIdx);
  TEST_ASSERT_EQUAL(0, mock.delete_char);
  // Second click within 3s: executes
  input_fsm_dispatch(EVT_CLICK, 1700);
  TEST_ASSERT_EQUAL(1, mock.delete_char);
}

void test_reset_arm_expires() {
  input_fsm_dispatch(EVT_LONG, 1000);
  input_fsm_dispatch(EVT_CLICK, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CW, 1400);
  input_fsm_dispatch(EVT_CLICK, 1500);   // into reset
  input_fsm_dispatch(EVT_CLICK, 1600);   // arm delete_char
  TEST_ASSERT_EQUAL(0, input_fsm_view().resetConfirmIdx);
  // Tick with now past expiry
  input_fsm_tick(5000);
  TEST_ASSERT_EQUAL(0xFF, input_fsm_view().resetConfirmIdx);
  // Second click now is a fresh arm, not execute
  input_fsm_dispatch(EVT_CLICK, 5100);
  TEST_ASSERT_EQUAL(0, input_fsm_view().resetConfirmIdx);
  TEST_ASSERT_EQUAL(0, mock.delete_char);
}

void test_reset_scroll_cancels_arm() {
  input_fsm_dispatch(EVT_LONG, 1000);
  input_fsm_dispatch(EVT_CLICK, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CW, 1400);
  input_fsm_dispatch(EVT_CLICK, 1500);   // into reset
  input_fsm_dispatch(EVT_CLICK, 1600);   // arm delete_char
  TEST_ASSERT_EQUAL(0, input_fsm_view().resetConfirmIdx);
  input_fsm_dispatch(EVT_ROT_CW, 1700);  // scroll away
  TEST_ASSERT_EQUAL(0xFF, input_fsm_view().resetConfirmIdx);
}

void test_passkey_overrides_and_restores() {
  input_fsm_dispatch(EVT_LONG, 1000);    // menu
  input_fsm_on_passkey_change(true);
  TEST_ASSERT_EQUAL(DISP_PASSKEY, input_fsm_view().mode);
  input_fsm_on_passkey_change(false);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
}

void test_passkey_ignores_input() {
  input_fsm_on_passkey_change(true);
  input_fsm_dispatch(EVT_CLICK, 1000);
  input_fsm_dispatch(EVT_LONG,  1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  TEST_ASSERT_EQUAL(DISP_PASSKEY, input_fsm_view().mode);
}

void test_prompt_force_home_from_menu() {
  input_fsm_dispatch(EVT_LONG, 1000);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
  input_fsm_force_home_on_prompt();
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.invalidate_buddy);
}

void test_prompt_force_home_respects_passkey() {
  input_fsm_on_passkey_change(true);
  input_fsm_force_home_on_prompt();
  // Passkey wins; force-home is ignored while passkey active.
  TEST_ASSERT_EQUAL(DISP_PASSKEY, input_fsm_view().mode);
}

void test_help_and_about_from_menu() {
  input_fsm_dispatch(EVT_LONG, 1000);     // menu
  // help = item 3
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  TEST_ASSERT_EQUAL(3, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 1400);
  TEST_ASSERT_EQUAL(DISP_HELP, input_fsm_view().mode);
  input_fsm_dispatch(EVT_CLICK, 1500);   // dismiss
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
  // about = item 4
  input_fsm_dispatch(EVT_ROT_CW, 1600);
  TEST_ASSERT_EQUAL(4, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 1700);
  TEST_ASSERT_EQUAL(DISP_ABOUT, input_fsm_view().mode);
  input_fsm_dispatch(EVT_LONG, 1800);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);
}

void test_demo_stays_in_menu() {
  input_fsm_dispatch(EVT_LONG, 1000);
  // demo = item 5
  for (int i = 0; i < 5; i++) input_fsm_dispatch(EVT_ROT_CW, 1100 + i*100);
  TEST_ASSERT_EQUAL(5, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 2000);
  TEST_ASSERT_EQUAL(DISP_MENU, input_fsm_view().mode);  // stays
  TEST_ASSERT_EQUAL(1, mock.toggle_demo);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_is_home);
  RUN_TEST(test_long_home_to_menu);
  RUN_TEST(test_long_menu_to_home);
  RUN_TEST(test_menu_scroll_wraps);
  RUN_TEST(test_menu_click_clock_enters_clock);
  RUN_TEST(test_menu_click_turn_off_calls_callback);
  RUN_TEST(test_menu_click_settings_opens_settings);
  RUN_TEST(test_clock_long_returns_home);
  RUN_TEST(test_clock_rotation_is_noop);
  RUN_TEST(test_settings_brightness_cycles);
  RUN_TEST(test_settings_back_returns_menu);
  RUN_TEST(test_settings_long_returns_home);
  RUN_TEST(test_reset_arm_then_execute);
  RUN_TEST(test_reset_arm_expires);
  RUN_TEST(test_reset_scroll_cancels_arm);
  RUN_TEST(test_passkey_overrides_and_restores);
  RUN_TEST(test_passkey_ignores_input);
  RUN_TEST(test_prompt_force_home_from_menu);
  RUN_TEST(test_prompt_force_home_respects_passkey);
  RUN_TEST(test_help_and_about_from_menu);
  RUN_TEST(test_demo_stays_in_menu);
  return UNITY_END();
}
