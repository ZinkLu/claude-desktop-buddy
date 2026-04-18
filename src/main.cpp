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
  hw_power_init();

  Serial.begin(115200);
  delay(500);
  Serial.println("xknob-buddy: boot (DIAG minimal)");

  hw_display_init();

  // Round 1 OK: hw_motor. Round 2 broke: +LittleFS +hw_input.
  // Round 3: hw_motor + LittleFS only (bisect).
  hw_motor_init();
  Serial.println("DIAG: hw_motor_init done");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  Serial.println("DIAG: LittleFS done");

  // hw_input_init();
  // buddyInit();
  // buddySetPeek(false);
  // startBt();
}

void loop() {
  // Task 7 display-pipeline diagnostic: drop all buddy rendering, just
  // alternate solid blue and solid red every 500ms. If this doesn't show,
  // pushSprite itself broke. If it shows, buddy render path is the culprit.
  static uint32_t lastTick = 0;
  static bool onBlue = true;

  TFT_eSprite& sp = hw_display_sprite();

  if (millis() - lastTick >= 500) {
    lastTick = millis();
    onBlue = !onBlue;
    sp.fillSprite(onBlue ? TFT_BLUE : TFT_RED);
    sp.pushSprite(0, 0);
    Serial.printf("diag %s at %lums\n", onBlue ? "BLUE" : "RED", millis());
  }

  delay(5);
}
