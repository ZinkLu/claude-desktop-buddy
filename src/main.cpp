#include <Arduino.h>
#include <LittleFS.h>
#include "esp_mac.h"
#include "ble_bridge.h"

static char btName[16] = "Claude";

static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("xknob-buddy: boot");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  startBt();
  Serial.printf("advertising as %s\n", btName);
}

void loop() {
  static uint32_t lastPasskey = 0;
  uint32_t pk = blePasskey();
  if (pk && pk != lastPasskey) {
    Serial.printf("passkey: %06lu\n", (unsigned long)pk);
  }
  lastPasskey = pk;

  static bool wasConn = false;
  bool conn = bleConnected();
  if (conn != wasConn) {
    Serial.printf("ble: %s\n", conn ? "connected" : "disconnected");
    wasConn = conn;
  }

  while (bleAvailable()) {
    int c = bleRead();
    if (c >= 0) Serial.write((char)c);
  }

  delay(20);
}
