#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("xknob-buddy: boot OK");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    Serial.printf("alive %lus\n", last / 1000);
  }
}
