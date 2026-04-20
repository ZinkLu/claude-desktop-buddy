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

static const int BTN_PIN  = 5;
// MT6701 SSI half-duplex read, bitbanged on three GPIOs. We avoid the
// Arduino SPIClass abstraction on ESP32-S3 because calling SPIClass(FSPI).begin()
// on this SDK clobbers TFT_eSPI's HSPI state and silently kills the LCD.
// Clocking ~1 MHz in software is plenty — MT6701 supports up to 15 MHz SSI.
static const int MT6701_SCLK = 2;
static const int MT6701_MISO = 1;
static const int MT6701_SS   = 42;

static hw_input_internal::ButtonFSM  _btn;
static hw_input_internal::EncoderFSM _enc;
static float _accDeg = 0.0f;
static float _lastRawDeg = NAN;

// Bitbang one bit pulse. ~1 µs half-period ≈ 500 kHz clock.
static inline void clk_pulse() {
  digitalWrite(MT6701_SCLK, HIGH);
  delayMicroseconds(1);
  digitalWrite(MT6701_SCLK, LOW);
  delayMicroseconds(1);
}

// Read 16 SSI bits MSB-first. MT6701 latches data on SCLK falling edge; we
// sample MISO just before pulling CLK high (= after CLK went low last time).
static float mt6701_read_deg() {
  digitalWrite(MT6701_SCLK, LOW);
  digitalWrite(MT6701_SS, LOW);
  delayMicroseconds(1);
  uint16_t raw = 0;
  for (int i = 0; i < 16; i++) {
    digitalWrite(MT6701_SCLK, HIGH);
    delayMicroseconds(1);
    raw = (raw << 1) | (digitalRead(MT6701_MISO) ? 1 : 0);
    digitalWrite(MT6701_SCLK, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(MT6701_SS, HIGH);
  // MT6701 SSI returns 14-bit angle in top bits, bottom 2 bits are status.
  uint16_t ag = raw >> 2;
  return ((float)ag * 360.0f) / 16384.0f;
}

void hw_input_init() {
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(MT6701_SCLK, OUTPUT);
  pinMode(MT6701_MISO, INPUT);
  pinMode(MT6701_SS,   OUTPUT);
  digitalWrite(MT6701_SCLK, LOW);
  digitalWrite(MT6701_SS,   HIGH);

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

bool hw_input_button_pressed() {
  return (digitalRead(BTN_PIN) == LOW);
}
#endif
