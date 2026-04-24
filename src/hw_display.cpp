#include "hw_display.h"
#include <Arduino.h>

// X-Knob SPI pins
// IMPORTANT: MISO is GPIO 13, shared with backlight!
static Arduino_DataBus *bus = new Arduino_HWSPI(14 /* DC */, 10 /* CS */, 12 /* SCLK */, 11 /* MOSI */, 13 /* MISO */);
static Arduino_GFX *gfx = new Arduino_GC9A01(bus, 9 /* RST */, 2 /* rotation */, true /* IPS */);
// Use Canvas as framebuffer: draw in memory, then flush to screen atomically
static Arduino_GFX *canvas = new Arduino_Canvas(240, 240, gfx);

static const int BLK_PIN = 13;  // Shared with MISO
static const int BLK_CHANNEL = 4;  // LEDC channel 4 (Timer 2) avoids conflict
                                    // with SimpleFOC's motor phases on channels 0-2

void hw_display_init() {
  Serial.println("[display] init start");
  
  gfx->begin();
  Serial.println("[display] gfx begin done");
  
  ledcSetup(BLK_CHANNEL, 20000, 8);
  ledcAttachPin(BLK_PIN, BLK_CHANNEL);
  ledcWrite(BLK_CHANNEL, 255);
  Serial.println("[display] backlight ON");
  
  canvas->begin();
  canvas->fillScreen(BLACK);
  Serial.println("[display] canvas ready");
}

Arduino_GFX* hw_display_canvas() {
  return canvas;
}

void hw_display_flush() {
  canvas->flush();
}

void hw_display_set_brightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  ledcWrite(BLK_CHANNEL, percent * 255 / 100);
}
