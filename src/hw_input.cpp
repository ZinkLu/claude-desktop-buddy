#include "hw_input.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace hw_input_internal {

static const uint32_t LONG_MS       = 600;
static const uint32_t CLICK_MAX_MS  = 500;
static const uint32_t DOUBLE_GAP_MS = 300;

InputEvent buttonStep(ButtonFSM& s, bool pressed, uint32_t now) {
  // Edge: press
  if (pressed && !s.prevPressed) {
    s.prevPressed = true;
    s.pressStartMs = now;
    s.longFired = false;

    // Did this press come during a DOUBLE window?
    if (s.awaitingDouble && now <= s.awaitExpireMs) {
      // Second press — release will emit DOUBLE. Stop awaiting CLICK.
      // We remember "this is the double path" by keeping awaitingDouble=true
      // but also flagging that we're mid-second-press — handled on release.
    }
    return EVT_NONE;
  }

  // Holding pressed: check LONG
  if (pressed && s.prevPressed) {
    if (!s.longFired && (now - s.pressStartMs) >= LONG_MS) {
      s.longFired = true;
      s.awaitingDouble = false;  // LONG consumes, don't interpret release as click
      return EVT_LONG;
    }
    return EVT_NONE;
  }

  // Edge: release
  if (!pressed && s.prevPressed) {
    s.prevPressed = false;
    uint32_t held = now - s.pressStartMs;

    if (s.longFired) {
      // LONG already handled, ignore release.
      return EVT_NONE;
    }

    if (held > CLICK_MAX_MS) {
      // Treat as canceled press (between CLICK_MAX and LONG thresholds):
      // neither CLICK nor LONG. Drop silently.
      s.awaitingDouble = false;
      return EVT_NONE;
    }

    // It's a quick release. Are we completing a double?
    if (s.awaitingDouble && now <= s.awaitExpireMs) {
      s.awaitingDouble = false;
      s.lastReleaseMs = now;
      return EVT_DOUBLE;
    }

    // First quick release — start DOUBLE window.
    s.awaitingDouble = true;
    s.awaitExpireMs = now + DOUBLE_GAP_MS;
    s.lastReleaseMs = now;
    return EVT_NONE;
  }

  // Idle: check if DOUBLE window expired into a CLICK
  if (!pressed && !s.prevPressed && s.awaitingDouble && now > s.awaitExpireMs) {
    s.awaitingDouble = false;
    return EVT_CLICK;
  }

  return EVT_NONE;
}

InputEvent encoderStep(EncoderFSM& s, float currentDeg) {
  const float DETENT_DEG = 15.0f;
  float delta = currentDeg - s.lastEmitDeg;
  if (delta >= DETENT_DEG) {
    s.lastEmitDeg += DETENT_DEG;
    return EVT_ROT_CW;
  }
  if (delta <= -DETENT_DEG) {
    s.lastEmitDeg -= DETENT_DEG;
    return EVT_ROT_CCW;
  }
  return EVT_NONE;
}

}  // namespace hw_input_internal

#ifdef ARDUINO

#include <SPI.h>

static const int BTN_PIN  = 5;
// MT6701 is SSI/SPI half-duplex (NOT I2C). X-Knob wires it to FSPI (SPI2)
// because HSPI is already owned by TFT_eSPI (LCD).
static const int MT6701_SCLK = 2;   // X-Knob config.h misnames this "MT6701_SCL"
static const int MT6701_MISO = 1;   // X-Knob config.h misnames this "MT6701_SDA"
static const int MT6701_SS   = 42;
static const uint32_t MT6701_SPI_HZ = 1000000;  // 1 MHz; MT6701 SSI max 15 MHz, low is safer

static SPIClass _fspi(FSPI);
static hw_input_internal::ButtonFSM  _btn;
static hw_input_internal::EncoderFSM _enc;
static float _accDeg = 0.0f;
static float _lastRawDeg = NAN;

static float mt6701_read_deg() {
  _fspi.beginTransaction(SPISettings(MT6701_SPI_HZ, MSBFIRST, SPI_MODE1));
  digitalWrite(MT6701_SS, LOW);
  uint16_t raw = _fspi.transfer16(0);
  digitalWrite(MT6701_SS, HIGH);
  _fspi.endTransaction();
  // MT6701 SSI returns 14-bit angle in top bits, plus status in bottom 2.
  uint16_t ag = raw >> 2;
  return ((float)ag * 360.0f) / 16384.0f;
}

void hw_input_init() {
  // Button first so even if SPI init misbehaves, button polling still works.
  pinMode(BTN_PIN, INPUT_PULLUP);

  // FSPI for MT6701 (HSPI is used by TFT_eSPI).
  pinMode(MT6701_SS, OUTPUT);
  digitalWrite(MT6701_SS, HIGH);
  _fspi.begin(MT6701_SCLK, MT6701_MISO, -1, MT6701_SS);

  float d = mt6701_read_deg();
  if (!isnan(d)) { _lastRawDeg = d; _accDeg = 0.0f; _enc.lastEmitDeg = 0.0f; }
}

InputEvent hw_input_poll() {
  uint32_t now = millis();

  float d = mt6701_read_deg();
  if (!isnan(d) && !isnan(_lastRawDeg)) {
    float diff = d - _lastRawDeg;
    if (diff >  180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    _accDeg += diff;
    _lastRawDeg = d;
    InputEvent re = hw_input_internal::encoderStep(_enc, _accDeg);
    if (re != EVT_NONE) return re;
  } else if (!isnan(d)) {
    _lastRawDeg = d;
  }

  bool pressed = (digitalRead(BTN_PIN) == LOW);
  InputEvent be = hw_input_internal::buttonStep(_btn, pressed, now);
  return be;
}
#endif
