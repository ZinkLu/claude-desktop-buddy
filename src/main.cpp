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

  // Bottom panel y=160..220
  sp.fillRect(0, 160, 240, 60, bg);
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

static void drawHudSimple() {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillRect(0, 165, 240, 40, TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(1);
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  const char* line = tama.nLines ? tama.lines[tama.nLines - 1] : tama.msg;
  if (line && *line) sp.drawString(line, 120, 185);
  sp.setTextDatum(TL_DATUM);
}

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
  if (e == EVT_ROT_CW || e == EVT_ROT_CCW) hw_motor_click_default();

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
  delay(20);
}
