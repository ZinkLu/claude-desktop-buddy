#include "hw_idle.h"
#include <Arduino.h>

static uint32_t lastActivityMs = 0;
static bool     dimmed         = false;
static bool     enabled        = true;
static const uint32_t IDLE_TIMEOUT_MS = 30000;

void hw_idle_init() {
  lastActivityMs = millis();
  dimmed = false;
}

void hw_idle_activity() {
  lastActivityMs = millis();
  dimmed = false;
}

bool hw_idle_is_dimmed() {
  return enabled && dimmed;
}

void hw_idle_set_enabled(bool en) {
  enabled = en;
  if (!en) dimmed = false;
}

bool hw_idle_tick(uint32_t now) {
  if (!enabled || dimmed) return false;
  if ((int32_t)(now - lastActivityMs) >= (int32_t)IDLE_TIMEOUT_MS) {
    dimmed = true;
    return true;
  }
  return false;
}
