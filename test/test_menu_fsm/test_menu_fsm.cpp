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
  // NEW
  int on_enter_pet;
  int on_exit_pet;
  int on_pet_rotation;
  bool last_pet_rotation_cw;
  int on_pet_long_press;
  int on_info_page_change;
  uint8_t last_info_page;
  int on_hud_scroll_change;
  uint8_t last_hud_scroll;
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
static void mk_on_enter_pet()                  { mock.on_enter_pet++; }
static void mk_on_exit_pet()                   { mock.on_exit_pet++; }
static void mk_on_pet_rotation(bool cw)        { mock.on_pet_rotation++; mock.last_pet_rotation_cw = cw; }
static void mk_on_pet_long_press()             { mock.on_pet_long_press++; }
static void mk_on_info_page_change(uint8_t p)  { mock.on_info_page_change++; mock.last_info_page = p; }
static void mk_on_hud_scroll_change(uint8_t o) { mock.on_hud_scroll_change++; mock.last_hud_scroll = o; }

static const FsmCallbacks CB = {
  mk_turn_off, mk_delete_char, mk_factory_reset, mk_toggle_demo,
  mk_brightness, mk_haptic, mk_transcript,
  mk_invalidate_clock, mk_invalidate_buddy, mk_invalidate_panel,
  mk_on_enter_pet, mk_on_exit_pet, mk_on_pet_rotation,
  mk_on_pet_long_press, mk_on_info_page_change, mk_on_hud_scroll_change,
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
  // Phase 2-B: menu help/about now route to DISP_INFO (pages 0/3).
  // CLICK/LONG from DISP_INFO returns to DISP_HOME.
  input_fsm_dispatch(EVT_LONG, 1000);     // menu
  // help = item 3
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  TEST_ASSERT_EQUAL(3, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 1400);
  TEST_ASSERT_EQUAL(DISP_INFO, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().infoPage);
  input_fsm_dispatch(EVT_CLICK, 1500);   // dismiss -> home
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
  // about = item 4 — navigate: home -> menu, scroll to 4
  input_fsm_dispatch(EVT_LONG, 1600);
  for (int i = 0; i < 4; i++) input_fsm_dispatch(EVT_ROT_CW, 1700 + i*100);
  TEST_ASSERT_EQUAL(4, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 2200);
  TEST_ASSERT_EQUAL(DISP_INFO, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(3, input_fsm_view().infoPage);
  input_fsm_dispatch(EVT_LONG, 2300);
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
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

// --- Phase 2-B: home CLICK cycle + pet + info + HUD scroll -------------

void test_home_click_enters_pet() {
  input_fsm_dispatch(EVT_CLICK, 1000);
  TEST_ASSERT_EQUAL(DISP_PET, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.on_enter_pet);
}

void test_pet_click_enters_info_page0() {
  input_fsm_dispatch(EVT_CLICK, 1000);  // home -> pet
  input_fsm_dispatch(EVT_CLICK, 1100);  // pet -> info
  TEST_ASSERT_EQUAL(DISP_INFO, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().infoPage);
  TEST_ASSERT_EQUAL(1, mock.on_exit_pet);
}

void test_info_click_returns_home() {
  input_fsm_dispatch(EVT_CLICK, 1000);  // home -> pet
  input_fsm_dispatch(EVT_CLICK, 1100);  // pet -> info
  input_fsm_dispatch(EVT_CLICK, 1200);  // info -> home
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_info_rotation_pages() {
  input_fsm_dispatch(EVT_CLICK, 1000);  // home -> pet
  input_fsm_dispatch(EVT_CLICK, 1100);  // pet -> info (page 0)
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  TEST_ASSERT_EQUAL(1, input_fsm_view().infoPage);
  TEST_ASSERT_EQUAL(1, mock.on_info_page_change);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  input_fsm_dispatch(EVT_ROT_CW, 1400);
  input_fsm_dispatch(EVT_ROT_CW, 1500);   // wraps 3 -> 0
  TEST_ASSERT_EQUAL(0, input_fsm_view().infoPage);
  input_fsm_dispatch(EVT_ROT_CCW, 1600);  // 0 -> 3 (reverse wrap)
  TEST_ASSERT_EQUAL(3, input_fsm_view().infoPage);
}

void test_info_long_returns_home() {
  input_fsm_dispatch(EVT_CLICK, 1000);  // -> pet
  input_fsm_dispatch(EVT_CLICK, 1100);  // -> info
  input_fsm_dispatch(EVT_LONG, 1200);
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
}

void test_pet_rotation_forwards_callback() {
  input_fsm_dispatch(EVT_CLICK, 1000);     // -> pet
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  TEST_ASSERT_EQUAL(1, mock.on_pet_rotation);
  TEST_ASSERT_TRUE(mock.last_pet_rotation_cw);
  input_fsm_dispatch(EVT_ROT_CCW, 1200);
  TEST_ASSERT_EQUAL(2, mock.on_pet_rotation);
  TEST_ASSERT_FALSE(mock.last_pet_rotation_cw);
}

void test_pet_double_enters_stats() {
  input_fsm_dispatch(EVT_CLICK, 1000);   // -> pet
  input_fsm_dispatch(EVT_DOUBLE, 1100);
  TEST_ASSERT_EQUAL(DISP_PET_STATS, input_fsm_view().mode);
}

void test_pet_stats_click_returns_to_pet() {
  input_fsm_dispatch(EVT_CLICK, 1000);   // -> pet
  input_fsm_dispatch(EVT_DOUBLE, 1100);  // -> stats
  input_fsm_dispatch(EVT_CLICK, 1200);
  TEST_ASSERT_EQUAL(DISP_PET, input_fsm_view().mode);
}

void test_pet_long_press_triggers_squish_callback() {
  input_fsm_dispatch(EVT_CLICK, 1000);  // -> pet
  // LONG from pet normally means exit, but spec §5 + §3 table: Pet-mode
  // LONG fires on_pet_long_press AND returns to home. So both effects.
  input_fsm_dispatch(EVT_LONG, 1100);
  TEST_ASSERT_EQUAL(1, mock.on_pet_long_press);
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.on_exit_pet);
}

void test_home_rotation_scrolls_hud() {
  input_fsm_dispatch(EVT_ROT_CW, 1000);
  TEST_ASSERT_EQUAL(1, input_fsm_view().hudScroll);
  TEST_ASSERT_EQUAL(1, mock.on_hud_scroll_change);
  input_fsm_dispatch(EVT_ROT_CCW, 1100);
  TEST_ASSERT_EQUAL(0, input_fsm_view().hudScroll);
  // CCW below 0 clamps at 0.
  input_fsm_dispatch(EVT_ROT_CCW, 1200);
  TEST_ASSERT_EQUAL(0, input_fsm_view().hudScroll);
}

void test_home_hud_scroll_clamps_at_30() {
  for (int i = 0; i < 35; i++) input_fsm_dispatch(EVT_ROT_CW, 1000 + i * 10);
  TEST_ASSERT_EQUAL(30, input_fsm_view().hudScroll);
}

void test_menu_help_opens_info_page_0() {
  input_fsm_dispatch(EVT_LONG, 1000);  // home -> menu
  // 'help' is item 3; scroll to it
  input_fsm_dispatch(EVT_ROT_CW, 1100);
  input_fsm_dispatch(EVT_ROT_CW, 1200);
  input_fsm_dispatch(EVT_ROT_CW, 1300);
  TEST_ASSERT_EQUAL(3, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 1400);
  TEST_ASSERT_EQUAL(DISP_INFO, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(0, input_fsm_view().infoPage);
}

void test_menu_about_opens_info_page_3() {
  input_fsm_dispatch(EVT_LONG, 1000);  // home -> menu
  // 'about' is item 4
  for (int i = 0; i < 4; i++) input_fsm_dispatch(EVT_ROT_CW, 1100 + i*100);
  TEST_ASSERT_EQUAL(4, input_fsm_view().menuSel);
  input_fsm_dispatch(EVT_CLICK, 1500);
  TEST_ASSERT_EQUAL(DISP_INFO, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(3, input_fsm_view().infoPage);
}

void test_prompt_force_home_stops_pet() {
  input_fsm_dispatch(EVT_CLICK, 1000);   // home -> pet
  input_fsm_force_home_on_prompt();
  TEST_ASSERT_EQUAL(DISP_HOME, input_fsm_view().mode);
  TEST_ASSERT_EQUAL(1, mock.on_exit_pet);
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
  RUN_TEST(test_home_click_enters_pet);
  RUN_TEST(test_pet_click_enters_info_page0);
  RUN_TEST(test_info_click_returns_home);
  RUN_TEST(test_info_rotation_pages);
  RUN_TEST(test_info_long_returns_home);
  RUN_TEST(test_pet_rotation_forwards_callback);
  RUN_TEST(test_pet_double_enters_stats);
  RUN_TEST(test_pet_stats_click_returns_to_pet);
  RUN_TEST(test_pet_long_press_triggers_squish_callback);
  RUN_TEST(test_home_rotation_scrolls_hud);
  RUN_TEST(test_home_hud_scroll_clamps_at_30);
  RUN_TEST(test_menu_help_opens_info_page_0);
  RUN_TEST(test_menu_about_opens_info_page_3);
  RUN_TEST(test_prompt_force_home_stops_pet);
  return UNITY_END();
}
