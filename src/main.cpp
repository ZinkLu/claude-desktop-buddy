#include <Arduino.h>
#include <LittleFS.h>
#include "esp_mac.h"
#include "ble_bridge.h"
#include "hw_display.h"
#include "hw_power.h"
#include "hw_input.h"

static char btName[16] = "Claude";
static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

static void drawCalibration() {
  TFT_eSprite& spr = hw_display_sprite();
  spr.fillSprite(TFT_BLACK);

  // Top-left RED, top-right GREEN, bottom-left BLUE, bottom-right WHITE.
  spr.fillRect(0,   0,   120, 120, TFT_RED);
  spr.fillRect(120, 0,   120, 120, TFT_GREEN);
  spr.fillRect(0,   120, 120, 120, TFT_BLUE);
  spr.fillRect(120, 120, 120, 120, TFT_WHITE);

  spr.setTextColor(TFT_BLACK);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(2);
  spr.drawString("R", 60,  60);
  spr.drawString("G", 180, 60);
  spr.drawString("B", 60,  180);
  spr.drawString("W", 180, 180);

  spr.pushSprite(0, 0);
}

void setup() {
  hw_power_init();                 // First: hold main power rail

  Serial.begin(115200);
  delay(500);
  Serial.println("xknob-buddy: boot");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");

  hw_display_init();
  drawCalibration();

  hw_input_init();

  startBt();
}

void loop() {
  InputEvent e = hw_input_poll();
  switch (e) {
    case EVT_ROT_CW:  Serial.println("CW");     break;
    case EVT_ROT_CCW: Serial.println("CCW");    break;
    case EVT_CLICK:   Serial.println("CLICK");  break;
    case EVT_DOUBLE:  Serial.println("DOUBLE"); break;
    case EVT_LONG:    Serial.println("LONG");   break;
    default: break;
  }
  delay(5);
}
