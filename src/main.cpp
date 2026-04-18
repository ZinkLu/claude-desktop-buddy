#include <Arduino.h>
#include <LittleFS.h>
#include "esp_mac.h"
#include "ble_bridge.h"
#include "hw_display.h"
#include "hw_power.h"
#include "hw_input.h"
#include "hw_motor.h"
#include "buddy.h"

static char btName[16] = "Claude";
static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

void setup() {
  hw_power_init();                 // First: hold main power rail

  Serial.begin(115200);
  delay(500);
  Serial.println("xknob-buddy: boot");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");

  hw_display_init();

  Serial.println("hw_input: init start");
  hw_input_init();
  Serial.println("hw_input: init done");

  hw_motor_init();
  buddyInit();
  buddySetPeek(false);   // 2× scale (upstream "home" default; we never entered peek mode)

  startBt();
}

void loop() {
  static uint8_t state = 1;  // 1 = idle
  static uint32_t lastTick = 0;
  static bool firstFrame = true;

  InputEvent e = hw_input_poll();
  if (e == EVT_ROT_CW)  { state = (state + 1) % 7; buddyInvalidate(); }
  if (e == EVT_ROT_CCW) { state = (state + 6) % 7; buddyInvalidate(); }
  if (e != EVT_NONE)    hw_motor_click(120);

  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    Serial.printf("tick %lus state=%u\n", millis()/1000, state);
  }
  switch (e) {
    case EVT_ROT_CW:  Serial.println("CW");     break;
    case EVT_ROT_CCW: Serial.println("CCW");    break;
    case EVT_CLICK:   Serial.println("CLICK");  break;
    case EVT_DOUBLE:  Serial.println("DOUBLE"); break;
    case EVT_LONG:    Serial.println("LONG");   break;
    default: break;
  }

  TFT_eSprite& sp = hw_display_sprite();

  // First frame only: clear full sprite. After that, buddyTick manages its
  // own canvas region via fillRect inside the renderer — don't fillSprite
  // every frame or we blank the character between the 5fps animation ticks.
  if (firstFrame) { sp.fillSprite(TFT_BLACK); firstFrame = false; }

  // Task 7 debug marker — 4px red square at top of circle. If this is
  // invisible, sprite-to-LCD pipeline is dead. If it's visible but no
  // character, buddy render path is broken. Remove after Phase 1 acceptance.
  sp.fillRect(116, 28, 8, 8, TFT_RED);

  buddyTick(state);

  // State label — clear just its strip each frame and redraw.
  static const char* names[] = {"sleep","idle","busy","attention","celebrate","dizzy","heart"};
  sp.fillRect(60, 198, 120, 14, TFT_BLACK);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(1);
  sp.drawString(names[state], 120, 205);
  sp.setTextDatum(TL_DATUM);

  sp.pushSprite(0, 0);
  delay(20);
}
