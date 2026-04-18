#include "hw_input.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
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

static const int BTN_PIN    = 5;
static const int MT6701_SDA = 1;
static const int MT6701_SCL = 2;
static const uint8_t MT6701_ADDR = 0x06;  // 7-bit address

static hw_input_internal::ButtonFSM  _btn;
static hw_input_internal::EncoderFSM _enc;

// MT6701 angle register is 0x03 (high byte) / 0x04 (low 6 bits).
// Value is 14-bit, 0..16383 over 0..360°.
static float mt6701_read_deg() {
  Wire.beginTransmission(MT6701_ADDR);
  Wire.write(0x03);
  if (Wire.endTransmission(false) != 0) return NAN;
  Wire.requestFrom((int)MT6701_ADDR, 2);
  if (Wire.available() < 2) return NAN;
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  uint16_t raw = ((uint16_t)hi << 6) | (lo >> 2);   // 14-bit
  return (raw * 360.0f) / 16384.0f;
}

// Accumulated degrees across wrap (MT6701 returns 0..360; we want unbounded).
static float _accDeg = 0.0f;
static float _lastRawDeg = NAN;

void hw_input_init() {
  pinMode(BTN_PIN, INPUT_PULLUP);
  Wire.begin(MT6701_SDA, MT6701_SCL, 400000);
  // Seed
  float d = mt6701_read_deg();
  if (!isnan(d)) { _lastRawDeg = d; _accDeg = 0.0f; _enc.lastEmitDeg = 0.0f; }
}

InputEvent hw_input_poll() {
  uint32_t now = millis();

  // Encoder: read angle, unwrap, pass to FSM. Emit only one event per poll
  // (caller can call again; the accumulated value sits until consumed).
  float d = mt6701_read_deg();
  if (!isnan(d) && !isnan(_lastRawDeg)) {
    float diff = d - _lastRawDeg;
    if (diff >  180.0f) diff -= 360.0f;   // wrap-around fix
    if (diff < -180.0f) diff += 360.0f;
    _accDeg += diff;
    _lastRawDeg = d;
    InputEvent re = hw_input_internal::encoderStep(_enc, _accDeg);
    if (re != EVT_NONE) return re;
  } else if (!isnan(d)) {
    _lastRawDeg = d;
  }

  // Button (active low on X-Knob)
  bool pressed = (digitalRead(BTN_PIN) == LOW);
  InputEvent be = hw_input_internal::buttonStep(_btn, pressed, now);
  return be;
}
#endif
