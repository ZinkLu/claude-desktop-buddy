#include "menu_panels.h"
#include <Arduino.h>
#include "hw_display.h"
#include "ble_bridge.h"
#include "input_fsm.h"

// Bridges to main.cpp — stats.h and data.h are header-only with file-scope
// static state; including them here would give us our own never-updated
// copies. main.cpp owns the live state and exports these accessors.
extern uint8_t panel_brightness();
extern uint8_t panel_haptic();
extern bool    panel_transcript_on();
extern bool    panel_data_demo();
extern bool    panel_auto_dim();
extern const char* panel_species_name();

// --- Palette -------------------------------------------------------------
// Use fixed values; the buddy character palette is not wired through yet.
static const uint16_t BG       = BLACK;
static const uint16_t TEXT     = WHITE;
static const uint16_t TEXT_DIM = DARKGREY;
static const uint16_t HOT      = 0xFA20;   // red-orange
static const uint16_t GREEN_FG = GREEN;

// --- Shared panel helpers -----------------------------------------------
static void _panel_title(const char* t, uint16_t color) {
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->fillScreen(BG);
  canvas->setTextSize(2);
  canvas->setTextColor(color, BG);
  canvas->setCursor(40, 30);
  canvas->print(t);
  canvas->drawFastHLine(40, 52, 160, TEXT_DIM);
  canvas->setTextSize(1);
}

static void _hints(const char* a, const char* b) {
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->setTextSize(1);
  canvas->setTextColor(TEXT_DIM, BG);
  canvas->setCursor(40, 190); canvas->print(a);
  canvas->setCursor(40, 204); canvas->print(b);
}

static void _draw_item(int y, bool selected, const char* label, const char* value) {
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->setTextSize(1);
  canvas->setTextColor(selected ? TEXT : TEXT_DIM, BG);
  canvas->setCursor(40, y);
  canvas->print(selected ? "> " : "  ");
  canvas->print(label);
  if (value && *value) {
    canvas->setCursor(170, y);
    canvas->print(value);
  }
}

// --- Main menu -----------------------------------------------------------
void draw_main_menu() {
  const FsmView& v = input_fsm_view();
  static uint8_t lastSel = 0xFF;
  static bool lastDemo = false;
  bool demo = panel_data_demo();
  if (v.menuSel == lastSel && demo == lastDemo) return;
  lastSel = v.menuSel;
  lastDemo = demo;

  _panel_title("Menu", TEXT);
  static const char* LABELS[7] = {
    "settings", "clock", "turn off", "help", "about", "demo", "close"
  };
  int y = 70;
  for (uint8_t i = 0; i < 7; i++) {
    const char* val = nullptr;
    if (i == 5) val = demo ? "on" : "off";
    _draw_item(y, v.menuSel == i, LABELS[i], val);
    y += 16;
  }
  _hints("CW/CCW: scroll", "CLICK: select");
  hw_display_flush();
}

// --- Settings ------------------------------------------------------------
void draw_settings() {
  const FsmView& v = input_fsm_view();
  static uint8_t lastSel = 0xFF;
  static uint8_t lastBrightness = 0xFF;
  static uint8_t lastHaptic = 0xFF;
  static bool lastTranscript = false;
  static bool lastAutoDim = false;
  static const char* lastSpecies = nullptr;

  uint8_t brightness = panel_brightness();
  uint8_t haptic = panel_haptic();
  bool transcript = panel_transcript_on();
  bool autoDim = panel_auto_dim();
  const char* species = panel_species_name();

  if (v.settingsSel == lastSel && brightness == lastBrightness && haptic == lastHaptic
      && transcript == lastTranscript && autoDim == lastAutoDim && species == lastSpecies) return;
  lastSel = v.settingsSel;
  lastBrightness = brightness;
  lastHaptic = haptic;
  lastTranscript = transcript;
  lastAutoDim = autoDim;
  lastSpecies = species;

  _panel_title("Settings", TEXT);
  static const char* LABELS[7] = {
    "brightness", "haptic", "transcript", "auto dim", "ascii pet", "reset", "back"
  };
  char buf[16];
  int y = 70;
  for (uint8_t i = 0; i < 7; i++) {
    const char* val = nullptr;
    if (i == 0) { snprintf(buf, sizeof(buf), "%u", brightness); val = buf; }
    else if (i == 1) { snprintf(buf, sizeof(buf), "%u", haptic); val = buf; }
    else if (i == 2) val = transcript ? "on" : "off";
    else if (i == 3) val = autoDim ? "on" : "off";
    else if (i == 4) val = species;
    _draw_item(y, v.settingsSel == i, LABELS[i], val);
    y += 16;
  }
  _hints("CW/CCW: scroll", "CLICK: change");
  hw_display_flush();
}

// --- Reset ---------------------------------------------------------------
void draw_reset() {
  const FsmView& v = input_fsm_view();
  static uint8_t lastSel = 0xFF;
  static uint8_t lastConfirmIdx = 0xFF;
  static uint32_t lastConfirmUntil = 0;
  uint32_t now = millis();
  bool confirmActive = (now < v.resetConfirmUntil);
  if (v.resetSel == lastSel && v.resetConfirmIdx == lastConfirmIdx
      && v.resetConfirmUntil == lastConfirmUntil) return;
  lastSel = v.resetSel;
  lastConfirmIdx = v.resetConfirmIdx;
  lastConfirmUntil = v.resetConfirmUntil;

  _panel_title("RESET", HOT);
  static const char* LABELS[3] = { "delete pet", "factory reset", "back" };
  int y = 90;
  for (uint8_t i = 0; i < 3; i++) {
    bool armed = (v.resetConfirmIdx == i) && confirmActive;
    const char* label = armed ? "really?" : LABELS[i];
    Arduino_GFX* canvas = hw_display_canvas();
    canvas->setTextSize(1);
    uint16_t color = armed ? HOT : (v.resetSel == i ? TEXT : TEXT_DIM);
    canvas->setTextColor(color, BG);
    canvas->setCursor(40, y);
    canvas->print(v.resetSel == i ? "> " : "  ");
    canvas->print(label);
    y += 16;
  }
  _hints("CW/CCW: scroll", "CLICK: confirm");
  hw_display_flush();
}

// --- Help ---------------------------------------------------------------
void draw_help() {
  _panel_title("Controls", TEXT);
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->setTextSize(1);
  canvas->setTextColor(TEXT_DIM, BG);
  int y = 70;
  canvas->setCursor(40, y); canvas->print("Turn knob   scroll");             y += 12;
  canvas->setCursor(40, y); canvas->print("Click       select/toggle");      y += 12;
  canvas->setCursor(40, y); canvas->print("Long press  menu / back home");   y += 12;
  canvas->setCursor(40, y); canvas->print("Double      (reserved)");         y += 16;
  canvas->drawFastHLine(40, y, 160, TEXT_DIM);                           y += 6;
  canvas->setCursor(40, y); canvas->print("Home:  long press menu");         y += 12;
  canvas->setCursor(40, y); canvas->print("Clock: menu > clock");
  _hints("CLICK or LONG", "to return");
  hw_display_flush();
}

// --- About --------------------------------------------------------------
void draw_about() {
  _panel_title("About", TEXT);
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->setTextSize(1);
  canvas->setTextColor(TEXT_DIM, BG);
  int y = 70;
  canvas->setCursor(40, y); canvas->print("claude-desktop");          y += 12;
  canvas->setCursor(40, y); canvas->print("-buddy  X-Knob");          y += 16;
  canvas->drawFastHLine(40, y, 160, TEXT_DIM);                    y += 6;
  canvas->setCursor(40, y); canvas->print("by Felix Rieseberg");      y += 12;
  canvas->setCursor(40, y); canvas->print("+ community");             y += 12;
  canvas->setCursor(40, y); canvas->print("X-Knob port: you");        y += 16;
  canvas->setCursor(40, y); canvas->print("github:ZinkLu/");          y += 12;
  canvas->setCursor(40, y); canvas->print("  claude-desktop-buddy");
  _hints("CLICK or LONG", "to return");
  hw_display_flush();
}

// --- Passkey ------------------------------------------------------------
void draw_passkey() {
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->fillScreen(BG);
  canvas->setTextColor(TEXT_DIM, BG);
  canvas->setTextSize(2);
  canvas->setCursor(120 - 6*6, 50); canvas->print("BT PAIRING");

  uint32_t pk = blePasskey();
  char b[8];
  snprintf(b, sizeof(b), "%06lu", (unsigned long)pk);
  canvas->setTextColor(TEXT, BG);
  canvas->setTextSize(5);
  canvas->setCursor(120 - 6*5*3, 120); canvas->print(b);

  canvas->setTextSize(1);
  canvas->setTextColor(TEXT_DIM, BG);
  canvas->setCursor(120 - 4*6, 170); canvas->print("Enter on");
  canvas->setCursor(120 - 6*6, 184); canvas->print("your computer");
  hw_display_flush();
}
