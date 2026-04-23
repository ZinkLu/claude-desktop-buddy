#include "hw_display.h"
#include <Arduino.h>

// X-Knob SPI pins
// IMPORTANT: MISO is GPIO 13, shared with backlight!
static Arduino_DataBus *bus = new Arduino_HWSPI(14 /* DC */, 10 /* CS */, 12 /* SCLK */, 11 /* MOSI */, 13 /* MISO */);
static Arduino_GFX *gfx = new Arduino_GC9A01(bus, 9 /* RST */, 2 /* rotation */, true /* IPS */);
// Use Canvas as framebuffer: draw in memory, then flush to screen atomically
static Arduino_GFX *canvas = new Arduino_Canvas(240, 240, gfx);

static const int BLK_PIN = 13;  // Shared with MISO

void hw_display_init() {
  Serial.println("[display] init start");
  
  // Step 1: Initialize GFX first (X-Knob original order)
  gfx->begin();
  Serial.println("[display] gfx begin done");
  
  // Step 2: Now configure backlight PWM
  // Use 20kHz to avoid visible flickering
  ledcSetup(0, 20000, 8);
  ledcAttachPin(BLK_PIN, 0);
  ledcWrite(0, 255);  // Full brightness
  Serial.println("[display] backlight ON");
  
  // Initialize canvas (memory framebuffer)
  canvas->begin();
  canvas->fillScreen(BLACK);
  Serial.println("[display] canvas ready");
}

// Return canvas for drawing (all UI draws to canvas, not directly to screen)
Arduino_GFX* hw_display_canvas() {
  return canvas;
}

// Flush canvas to screen atomically (no flicker)
void hw_display_flush() {
  canvas->flush();
}

void hw_display_set_brightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  ledcWrite(0, percent * 255 / 100);
}
