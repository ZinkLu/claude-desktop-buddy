#include <Arduino.h>
#include <LittleFS.h>
#include "esp_mac.h"
#include "ble_bridge.h"
#include "hw_display.h"
#include "hw_power.h"
#include "hw_input.h"
#include "hw_motor.h"
#include "buddy.h"
#include "clock_face.h"
#include "character.h"
#include "data.h"          // header-only; include once
#include "stats.h"         // header-only; include once
#include "input_fsm.h"
#include "menu_panels.h"
#include "info_pages.h"
#include "pet_pages.h"
#include "pet_gesture.h"

enum PersonaState { P_SLEEP, P_IDLE, P_BUSY, P_ATTENTION, P_CELEBRATE, P_DIZZY, P_HEART };

static char btName[16] = "Claude";
static TamaState tama{};
static PersonaState activeState = P_IDLE;
static bool approvalChoice = true;  // true=approve, false=deny
static char lastPromptId[40] = "";
static bool responseSent = false;
static uint32_t promptArrivedMs = 0;
// Globals (not static): xfer.h expects these as `extern` when a remote
// `species` or `char_end` command arrives over BLE.
bool buddyMode = true;          // true=ASCII species, false=GIF character pack
bool gifAvailable = false;

// Bridges for other translation units — stats.h and data.h are header-only
// with file-scope static state, so other .cpp files can't include them
// without getting their own (never-updated) copies.
uint8_t current_haptic_level() { return settings().haptic; }
uint8_t panel_brightness()     { return settings().brightness; }
uint8_t panel_haptic()         { return settings().haptic; }
bool    panel_transcript_on()  { return settings().hud; }
bool    panel_data_demo()      { return dataDemo(); }

// Info page bridges
const char* info_bt_name()              { return btName; }
uint8_t     info_sessions_total()       { return tama.sessionsTotal; }
uint8_t     info_sessions_running()     { return tama.sessionsRunning; }
uint8_t     info_sessions_waiting()     { return tama.sessionsWaiting; }
uint32_t    info_last_msg_age_s()       { return (millis() - tama.lastUpdated) / 1000; }
const char* info_claude_state_name() {
  static const char* names[] = {"sleep","idle","busy","attention","celebrate","dizzy","heart"};
  return names[(uint8_t)activeState < 7 ? (uint8_t)activeState : 1];
}
const char* info_bt_status() {
  if (!settings().bt) return "off";
  if (dataBtActive())  return "linked";
  if (bleConnected())  return "discover";
  return "off";
}
const char* info_scenario_name()        { return dataScenarioName(); }

// Pet page bridges (stats.h accessors live in main's TU)
uint8_t  pet_mood_tier()    { return statsMoodTier(); }
uint8_t  pet_fed_progress() { return statsFedProgress(); }
uint8_t  pet_energy_tier()  { return statsEnergyTier(); }
uint8_t  pet_level()        { return stats().level; }
uint16_t pet_approvals()    { return stats().approvals; }
uint16_t pet_denials()      { return stats().denials; }
uint32_t pet_nap_seconds()  { return stats().napSeconds; }
uint32_t pet_tokens_total() { return stats().tokens; }
uint32_t pet_tokens_today() { return tama.tokensToday; }

// Main owns the "cycle this setting to its next value" logic; input_fsm
// just tells us "user clicked brightness / haptic / transcript". This
// keeps all NVS writes centralized here.

static void cycle_brightness(uint8_t) {
  Settings& s = settings();
  s.brightness = (uint8_t)((s.brightness + 1) % 5);
  hw_display_set_brightness((s.brightness + 1) * 20);   // 0..4 -> 20..100
  settingsSave();
}

static void cycle_haptic(uint8_t) {
  Settings& s = settings();
  s.haptic = (uint8_t)((s.haptic + 1) % 5);
  settingsSave();
  hw_motor_click_default();   // fire at new strength so user feels it
}

static void toggle_transcript(bool) {
  Settings& s = settings();
  s.hud = !s.hud;
  settingsSave();
}

static void action_turn_off()    { hw_power_off(); }   // never returns
static void action_toggle_demo() { dataSetDemo(!dataDemo()); }
static void invalidate_panel_cb(){ /* main loop repaints each frame; no-op */ }

static void action_delete_char() {
  // Wipe /characters/ and reboot. Matches upstream's "delete char" behavior.
  File d = LittleFS.open("/characters");
  if (d && d.isDirectory()) {
    File e;
    while ((e = d.openNextFile())) {
      char path[80];
      snprintf(path, sizeof(path), "/characters/%s", e.name());
      if (e.isDirectory()) {
        File f;
        while ((f = e.openNextFile())) {
          char fp[128];
          snprintf(fp, sizeof(fp), "%s/%s", path, f.name());
          f.close();
          LittleFS.remove(fp);
        }
        e.close();
        LittleFS.rmdir(path);
      } else {
        e.close();
        LittleFS.remove(path);
      }
    }
    d.close();
  }
  delay(300);
  ESP.restart();
}

static void action_factory_reset() {
  _prefs.begin("buddy", false);
  _prefs.clear();
  _prefs.end();
  LittleFS.format();
  bleClearBonds();
  delay(300);
  ESP.restart();
}

static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

static void sendCmd(const char* json) {
  Serial.println(json);
  bleWrite((const uint8_t*)json, strlen(json));
  bleWrite((const uint8_t*)"\n", 1);
}

static PersonaState derive(const TamaState& s) {
  if (!s.connected)           return P_IDLE;
  if (s.sessionsWaiting > 0)  return P_ATTENTION;
  if (s.recentlyCompleted)    return P_CELEBRATE;
  if (s.sessionsRunning >= 3) return P_BUSY;
  return P_IDLE;
}

static void drawApproval() {
  TFT_eSprite& sp = hw_display_sprite();
  uint16_t bg = TFT_BLACK;

  // Bottom panel. Clear y=148..220 — same region HUD uses, so transitioning
  // between HUD and approval never leaves stale pixels at the boundary.
  sp.fillRect(0, 148, 240, 72, bg);
  sp.drawFastHLine(24, 160, 192, TFT_DARKGREY);

  sp.setTextDatum(TL_DATUM);
  sp.setTextSize(1);
  uint32_t waited = (millis() - promptArrivedMs) / 1000;
  sp.setTextColor(waited >= 10 ? TFT_ORANGE : TFT_DARKGREY, bg);
  char line[32];
  snprintf(line, sizeof(line), "approve? %lus", (unsigned long)waited);
  sp.setCursor(32, 166);
  sp.print(line);

  sp.setTextColor(TFT_WHITE, bg);
  sp.setTextSize(2);
  sp.setCursor(32, 178);
  sp.print(tama.promptTool[0] ? tama.promptTool : "?");
  sp.setTextSize(1);

  sp.setCursor(40, 205);
  if (approvalChoice) { sp.setTextColor(TFT_GREEN, bg);     sp.print("> APPROVE"); }
  else                { sp.setTextColor(TFT_DARKGREY, bg);  sp.print("  approve"); }
  sp.setCursor(140, 205);
  if (!approvalChoice){ sp.setTextColor(TFT_RED, bg);       sp.print("> DENY"); }
  else                { sp.setTextColor(TFT_DARKGREY, bg);  sp.print("  deny"); }
}

// Greedy word-wrap into fixed-width rows. Continuation rows get a leading
// space. Returns number of rows written. Row buffer must be >= width + 1
// bytes (the +1 is for the null terminator).
static uint8_t wrapInto(const char* in, char out[][32], uint8_t maxRows, uint8_t width) {
  uint8_t row = 0, col = 0;
  const char* p = in;
  while (*p && row < maxRows) {
    while (*p == ' ') p++;
    const char* w = p;
    while (*p && *p != ' ') p++;
    uint8_t wlen = p - w;
    if (wlen == 0) break;
    uint8_t need = (col > 0 ? 1 : 0) + wlen;
    if (col + need > width) {
      out[row][col] = 0;
      if (++row >= maxRows) return row;
      out[row][0] = ' '; col = 1;
    }
    if (col > 1 || (col == 1 && out[row][0] != ' ')) out[row][col++] = ' ';
    else if (col == 1 && row > 0) {}
    while (wlen > width - col) {
      uint8_t take = width - col;
      memcpy(&out[row][col], w, take); col += take; w += take; wlen -= take;
      out[row][col] = 0;
      if (++row >= maxRows) return row;
      out[row][0] = ' '; col = 1;
    }
    memcpy(&out[row][col], w, wlen); col += wlen;
  }
  if (col > 0 && row < maxRows) { out[row][col] = 0; row++; }
  return row;
}

static void drawHudSimple() {
  TFT_eSprite& sp = hw_display_sprite();
  // HUD sits in the lower portion of the visible circle (y=150..208).
  // With BUDDY_Y_BASE=40 the character body ends at y=146, leaving room
  // for 4 lines of transcript at y=154/166/178/190 + a scroll indicator
  // at the bottom. Narrowest row (y=190) visible width ~2*sqrt(120^2-70^2)
  // ≈ 195 px ≈ 32 chars at size 1; use WIDTH=28 for margin.
  const int SHOW = 4;
  const int TOP  = 150;
  const int LH   = 12;
  const int WIDTH = 28;

  // Clear y=148..220 — matches drawApproval's region so switching between
  // HUD and approval never leaves stale pixels at the boundary.
  sp.fillRect(0, 148, 240, 72, TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.setTextDatum(MC_DATUM);

  uint8_t scroll = input_fsm_view().hudScroll;

  // Always paint the scroll indicator first (even with no transcript) so the
  // user sees rotation feedback regardless of data state. Size-2 orange top
  // of the HUD strip so it's clearly visible.
  auto drawIndicator = [&]() {
    if (scroll == 0) return;
    char b[6];
    snprintf(b, sizeof(b), "-%u", (unsigned)scroll);
    sp.setTextSize(2);
    sp.setTextColor(TFT_ORANGE, TFT_BLACK);
    sp.setTextDatum(TR_DATUM);
    sp.drawString(b, 220, TOP + 2);
    sp.setTextSize(1);
    sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
  };

  if (tama.nLines == 0) {
    const char* line = tama.msg;
    if (line && *line) {
      sp.drawString(line, 120, TOP + 20);
    }
    drawIndicator();
    sp.setTextDatum(TL_DATUM);
    return;
  }

  static char disp[32][32];
  uint8_t nDisp = 0;
  for (uint8_t i = 0; i < tama.nLines && nDisp < 32; i++) {
    uint8_t got = wrapInto(tama.lines[i], &disp[nDisp], 32 - nDisp, WIDTH);
    nDisp += got;
  }
  if (nDisp == 0) { drawIndicator(); sp.setTextDatum(TL_DATUM); return; }

  uint8_t maxBack = (nDisp > SHOW) ? (uint8_t)(nDisp - SHOW) : 0;
  if (scroll > maxBack) scroll = maxBack;

  int end = (int)nDisp - scroll;
  int start = end - SHOW; if (start < 0) start = 0;

  for (int i = 0; start + i < end; i++) {
    sp.drawString(disp[start + i], 120, TOP + 4 + i * LH);
  }

  drawIndicator();
  sp.setTextDatum(TL_DATUM);
}

// Pet mode runtime state
static uint32_t petEnterMs = 0;
static uint32_t petStrokeLastMs = 0;
static uint32_t petStrokeTotalMs = 0;
static uint32_t petFirstRotMs = 0;   // start of current rotation burst; 0 = idle
static bool     petFellAsleep = false;
static uint32_t petDizzyUntilMs = 0;
static uint32_t petSquishUntilMs = 0;
static const uint32_t PURR_DEBOUNCE_MS = 250;  // wait past tickle window before starting purr
static const uint32_t PET_HINT_MS        = 3000;
static const uint32_t PET_IDLE_MS        = 3000;
static const uint32_t PET_MAX_STROKE_MS  = 30000;
static const uint32_t PET_DIZZY_DURATION = 1000;
static const uint32_t PET_SQUISH_DURATION = 800;

static void cb_on_enter_pet() {
  petEnterMs = millis();
  petStrokeLastMs = 0;
  petStrokeTotalMs = 0;
  petFirstRotMs = 0;
  petFellAsleep = false;
  petDizzyUntilMs = 0;
  petSquishUntilMs = 0;
  pet_gesture_reset();
  hw_motor_pulse_series(3, 100, settings().haptic);
}

static void cb_on_exit_pet() {
  hw_motor_purr_stop();
  hw_motor_pulse_series(2, 80, settings().haptic);
}

static void cb_on_pet_rotation(bool cw) {
  uint32_t now = millis();
  PetGesture g = pet_gesture_step(cw ? EVT_ROT_CW : EVT_ROT_CCW, now);
  Serial.printf("[pet] rot=%s gesture=%d\n", cw ? "CW" : "CCW", (int)g);
  if (g == PGEST_STROKE) {
    // New rotation burst if we've been idle for half a second.
    if (petFirstRotMs == 0 || now - petStrokeLastMs > 500) {
      petFirstRotMs = now;
    }
    petStrokeLastMs = now;
    petStrokeTotalMs += 200;
    // Only start purr after PURR_DEBOUNCE_MS — gives tickle the chance
    // to preempt if the user is actually spinning fast.
    if (now - petFirstRotMs >= PURR_DEBOUNCE_MS && !hw_motor_purr_active()) {
      hw_motor_purr_start(2);
      Serial.println("[pet] purr start");
    }
  } else if (g == PGEST_TICKLE) {
    // Tickle pre-empts a forming purr. Reset the burst so follow-up slow
    // rotation won't keep petFirstRotMs stale.
    petFirstRotMs = 0;
    hw_motor_purr_stop();
    // 3 rapid strong pulses = "stop that" annoyed buzz. Felt through the
    // grip because it's a series of distinct shocks, not a single 40 ms
    // pulse (which gets absorbed by the user's fingers).
    hw_motor_pulse_series(3, 80, 4);
    petDizzyUntilMs = now + PET_DIZZY_DURATION;
    Serial.println("[pet] tickle buzz");
  }
}

static void cb_on_pet_long_press() {
  hw_motor_vibrate(PET_SQUISH_DURATION, settings().haptic);
  petSquishUntilMs = millis() + PET_SQUISH_DURATION;
}

static void cb_on_info_page_change(uint8_t /*p*/)   { /* main loop repaints each frame */ }
static void cb_on_hud_scroll_change(uint8_t o) { Serial.printf("[hud] scroll=%u\n", o); }

void setup() {
  hw_power_init();

  Serial.begin(115200);
  delay(500);
  Serial.println("xknob-buddy: boot");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");

  hw_display_init();
  hw_input_init();
  hw_motor_init();

  statsLoad();
  settingsLoad();
  petNameLoad();

  buddyInit();
  buddySetPeek(false);        // 2× scale on home

  characterInit(nullptr);     // scans /characters/ in LittleFS
  gifAvailable = characterLoaded();
  buddyMode = !gifAvailable;  // prefer GIF if installed

  static const FsmCallbacks fsm_cb = {
    action_turn_off,
    action_delete_char,
    action_factory_reset,
    action_toggle_demo,
    cycle_brightness,
    cycle_haptic,
    toggle_transcript,
    clock_face_invalidate,
    buddyInvalidate,
    invalidate_panel_cb,
    cb_on_enter_pet,
    cb_on_exit_pet,
    cb_on_pet_rotation,
    cb_on_pet_long_press,
    cb_on_info_page_change,
    cb_on_hud_scroll_change,
  };
  input_fsm_init(&fsm_cb);

  // Apply persisted brightness now that stats are loaded.
  hw_display_set_brightness((settings().brightness + 1) * 20);

  startBt();
  Serial.printf("ready: mode=%s\n", buddyMode ? "ascii" : "gif");
}

void loop() {
  static uint32_t lastHeartbeat = 0;
  static bool firstFrame = true;
  uint32_t now = millis();

  dataPoll(&tama);
  activeState = derive(tama);

  static uint32_t lastPasskey = 0;
  uint32_t pk = blePasskey();
  if (pk && !lastPasskey) {
    Serial.printf("passkey: %06lu\n", (unsigned long)pk);
    input_fsm_on_passkey_change(true);
  } else if (!pk && lastPasskey) {
    input_fsm_on_passkey_change(false);
  }
  lastPasskey = pk;

  bool inPrompt = tama.promptId[0] && !responseSent;

  // New prompt arrival: reset highlight, clear sent flag, stamp time
  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId) - 1);
    lastPromptId[sizeof(lastPromptId) - 1] = 0;
    responseSent = false;
    approvalChoice = true;
    if (tama.promptId[0]) {
      promptArrivedMs = now;
      input_fsm_force_home_on_prompt();
    }
  }

  InputEvent e = hw_input_poll();
  // Fire motor bump only on rotation events. Open-loop pulse with no finger
  // on the knob (i.e. during CLICK/LONG/DOUBLE) can freely spin the shaft
  // since there's no hand resistance to absorb the torque.
  // Rotation bump: only in non-pet modes. In pet mode the gesture classifier
  // owns motor feedback (purr / kick); adding a generic bump on every detent
  // drowns out the subtle purr cadence.
  if (e == EVT_ROT_CW || e == EVT_ROT_CCW) {
    DisplayMode m = input_fsm_view().mode;
    if (m != DISP_PET) hw_motor_click_default();
  }

  if (inPrompt) {
    switch (e) {
      case EVT_ROT_CW:
      case EVT_ROT_CCW:
        approvalChoice = !approvalChoice;
        break;
      case EVT_CLICK: {
        char cmd[96];
        snprintf(cmd, sizeof(cmd),
                 "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}",
                 tama.promptId, approvalChoice ? "once" : "deny");
        sendCmd(cmd);
        responseSent = true;
        if (approvalChoice) {
          uint32_t waited = (now - promptArrivedMs) / 1000;
          statsOnApproval(waited);
        } else {
          statsOnDenial();
        }
        break;
      }
      default: break;
    }
  } else {
    input_fsm_dispatch(e, now);
    switch (e) {
      case EVT_ROT_CW:  Serial.println("CW");     break;
      case EVT_ROT_CCW: Serial.println("CCW");    break;
      case EVT_CLICK:   Serial.println("CLICK");  break;
      case EVT_DOUBLE:  Serial.println("DOUBLE"); break;
      case EVT_LONG:    Serial.println("LONG");   break;
      default: break;
    }
  }
  input_fsm_tick(now);   // clears expired reset arm each loop

  if (now - lastHeartbeat >= 5000) {
    lastHeartbeat = now;
    Serial.printf("tick %lus state=%d ble=%d prompt=%s\n",
                  now/1000, (int)activeState, bleConnected() ? 1 : 0,
                  tama.promptId[0] ? tama.promptId : "-");
  }

  // Render
  TFT_eSprite& sp = hw_display_sprite();
  if (firstFrame) { sp.fillSprite(TFT_BLACK); firstFrame = false; }

  // Pet mode purr safety: stop if user hasn't stroked for 3 s;
  // after 30 s of total stroking, transition to fell-asleep state.
  if (input_fsm_view().mode == DISP_PET && hw_motor_purr_active()) {
    if (petStrokeLastMs > 0 && now - petStrokeLastMs > PET_IDLE_MS) {
      hw_motor_purr_stop();
    } else if (petStrokeTotalMs > PET_MAX_STROKE_MS) {
      hw_motor_purr_stop();
      petFellAsleep = true;
    }
  }

  DisplayMode m = input_fsm_view().mode;
  switch (m) {
    case DISP_CLOCK:
      clock_face_tick();
      break;
    case DISP_MENU:     draw_main_menu(); break;
    case DISP_SETTINGS: draw_settings();  break;
    case DISP_RESET:    draw_reset();     break;
    case DISP_HELP:     draw_help();      break;
    case DISP_ABOUT:    draw_about();     break;
    case DISP_PASSKEY:  draw_passkey();   break;
    case DISP_INFO:
      draw_info_page(input_fsm_view().infoPage);
      break;
    case DISP_PET: {
      uint8_t state = (uint8_t)P_IDLE;
      if (petFellAsleep) state = (uint8_t)P_SLEEP;
      else if (millis() < petDizzyUntilMs) state = (uint8_t)P_DIZZY;
      else if (millis() < petSquishUntilMs) state = (uint8_t)P_HEART;   // squish reuses heart frames
      else if (hw_motor_purr_active()) state = (uint8_t)P_HEART;         // being stroked
      bool showHint = (millis() - petEnterMs) < PET_HINT_MS;
      draw_pet_main(state, showHint);
      break;
    }
    case DISP_HOME:
    default: {
      if (buddyMode) {
        buddyTick((uint8_t)activeState);
      } else if (characterLoaded()) {
        characterSetState((uint8_t)activeState);
        characterTick();
      } else {
        sp.setTextDatum(MC_DATUM);
        sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
        sp.drawString("no character", 120, 120);
        sp.setTextDatum(TL_DATUM);
      }
      if (inPrompt)            drawApproval();    // approvals always show
      else if (settings().hud) drawHudSimple();   // transcript gates only passive HUD
      sp.pushSprite(0, 0);
      break;
    }
  }

  hw_motor_tick(now);   // drive continuous motor patterns
  delay(20);
}
