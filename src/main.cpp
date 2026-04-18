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

enum PersonaState { P_SLEEP, P_IDLE, P_BUSY, P_ATTENTION, P_CELEBRATE, P_DIZZY, P_HEART };

// Phase 2-C: home ↔ clock toggle via LONG press.
enum DisplayMode { DISP_HOME, DISP_CLOCK };
static DisplayMode displayMode = DISP_HOME;

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

  startBt();
  Serial.printf("ready: mode=%s\n", buddyMode ? "ascii" : "gif");
}

void loop() {
  static uint32_t lastHeartbeat = 0;
  static bool firstFrame = true;
  uint32_t now = millis();

  dataPoll(&tama);
  activeState = derive(tama);

  bool inPrompt = tama.promptId[0] && !responseSent;

  // New prompt arrival: reset highlight, clear sent flag, stamp time
  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId) - 1);
    lastPromptId[sizeof(lastPromptId) - 1] = 0;
    responseSent = false;
    approvalChoice = true;
    if (tama.promptId[0]) {
      promptArrivedMs = now;
      if (displayMode == DISP_CLOCK) {
        displayMode = DISP_HOME;
        buddyInvalidate();   // force home first-frame repaint
      }
    }
  }

  InputEvent e = hw_input_poll();
  if (e != EVT_NONE) hw_motor_click(120);

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
    // Non-prompt input (free exploration) — currently no-op; Task 8+ could
    // wire rotation to GIF/species cycling or menu.
    switch (e) {
      case EVT_LONG:
        Serial.println("LONG");
        if (displayMode == DISP_HOME) {
          displayMode = DISP_CLOCK;
          clock_face_invalidate();
        } else {
          displayMode = DISP_HOME;
          buddyInvalidate();
        }
        break;
      default: break;
    }
  }

  if (now - lastHeartbeat >= 5000) {
    lastHeartbeat = now;
    Serial.printf("tick %lus state=%d ble=%d prompt=%s\n",
                  now/1000, (int)activeState, bleConnected() ? 1 : 0,
                  tama.promptId[0] ? tama.promptId : "-");
  }

  // Render
  TFT_eSprite& sp = hw_display_sprite();
  if (firstFrame) { sp.fillSprite(TFT_BLACK); firstFrame = false; }

  if (displayMode == DISP_CLOCK) {
    clock_face_tick();
  } else {
    if (buddyMode) {
      buddyTick((uint8_t)activeState);
    } else if (characterLoaded()) {
      characterSetState((uint8_t)activeState);
      characterTick();
    } else {
      // defensive: no renderer available
      sp.setTextDatum(MC_DATUM);
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString("no character", 120, 120);
      sp.setTextDatum(TL_DATUM);
    }

    if (inPrompt) drawApproval();
    else          drawHudSimple();

    sp.pushSprite(0, 0);
  }
  delay(20);
}
