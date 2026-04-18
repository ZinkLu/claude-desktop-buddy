#include "hw_display.h"
#include <Arduino.h>
#include <SPI.h>

static TFT_eSPI _tft;
static TFT_eSprite _spr(&_tft);

static const int BLK_PIN = 13;
static const int BLK_CH  = 0;

void hw_display_init() {
  ledcSetup(BLK_CH, 5000, 8);
  ledcAttachPin(BLK_PIN, BLK_CH);
  hw_display_set_brightness(0);  // off while init

  // ESP32-S3 quirk: TFT_eSPI 2.5.x does not always init SPI before its first
  // writecommand() inside init(). Initialize SPI explicitly with the X-Knob
  // pin map (SCLK=12, MOSI=11, no MISO, CS handled by TFT_eSPI).
  SPI.begin(12, -1, 11, -1);

  _tft.init();
  _tft.setRotation(0);
  _tft.fillScreen(TFT_BLACK);

  // X-Knob panel uses inverted color (matches upstream X-Knob hal/lcd.cpp).
  // If calibration shows cyan-for-red etc., toggle this to false.
  _tft.invertDisplay(true);

  _spr.setColorDepth(16);
  _spr.createSprite(240, 240);
  if (_spr.getPointer() == nullptr) {
    Serial.println("hw_display: sprite alloc FAILED");
  } else {
    Serial.printf("hw_display: sprite at %p, psram free %u KB\n",
                  _spr.getPointer(), (unsigned)(ESP.getFreePsram() / 1024));
  }

  hw_display_set_brightness(50);
}

void hw_display_set_brightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(BLK_CH, (pct * 255) / 100);
}

TFT_eSPI&     hw_display_tft()    { return _tft; }
TFT_eSprite&  hw_display_sprite() { return _spr; }
