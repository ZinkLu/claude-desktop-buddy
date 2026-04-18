#include "hw_leds.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <math.h>

static const int      LED_PIN   = 38;
static const uint16_t LED_COUNT = 8;

static Adafruit_NeoPixel _strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
static LedMode  _mode = LED_OFF;
static uint32_t _modeStart = 0;

static void fill(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < LED_COUNT; i++) _strip.setPixelColor(i, r, g, b);
  _strip.show();
}

void hw_leds_init() {
  _strip.begin();
  _strip.setBrightness(80);
  fill(0, 0, 0);
  _mode = LED_OFF;
}

void hw_leds_set_mode(LedMode m) {
  // Flashes are self-terminating and must not be interrupted.
  if (_mode == LED_APPROVE_FLASH || _mode == LED_DENY_FLASH) return;
  if (m == _mode) return;
  _mode = m;
  _modeStart = millis();
}

void hw_leds_tick() {
  uint32_t t = millis() - _modeStart;
  switch (_mode) {
    case LED_OFF:
      fill(0, 0, 0);
      break;

    case LED_ATTENTION_BREATH: {
      // 1.2 s breath period, red.
      float phase = (t % 1200) / 1200.0f;
      float v = 0.5f - 0.5f * cosf(phase * 2.0f * (float)PI);
      uint8_t r = (uint8_t)(v * 200.0f);
      fill(r, 0, 0);
      break;
    }

    case LED_APPROVE_FLASH: {
      // 3 flashes × 300 ms cycle = 900 ms total, green.
      uint32_t cycle = t % 300;
      bool on = cycle < 150;
      fill(0, on ? 200 : 0, 0);
      if (t >= 900) { _mode = LED_OFF; fill(0, 0, 0); }
      break;
    }

    case LED_DENY_FLASH: {
      uint32_t cycle = t % 300;
      bool on = cycle < 150;
      fill(on ? 200 : 0, 0, 0);
      if (t >= 900) { _mode = LED_OFF; fill(0, 0, 0); }
      break;
    }
  }
}
