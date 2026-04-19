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

// --- Palette -------------------------------------------------------------
// Use fixed values; the buddy character palette is not wired through yet.
static const uint16_t BG       = TFT_BLACK;
static const uint16_t TEXT     = TFT_WHITE;
static const uint16_t TEXT_DIM = TFT_DARKGREY;
static const uint16_t HOT      = 0xFA20;   // red-orange
static const uint16_t GREEN    = TFT_GREEN;

// --- Shared panel helpers -----------------------------------------------
static void _panel_title(const char* t, uint16_t color) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);
  sp.setTextDatum(TL_DATUM);
  sp.setTextSize(2);
  sp.setTextColor(color, BG);
  sp.setCursor(40, 30);
  sp.print(t);
  sp.drawFastHLine(40, 52, 160, TEXT_DIM);
  sp.setTextSize(1);
}

static void _hints(const char* a, const char* b) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextSize(1);
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 190); sp.print(a);
  sp.setCursor(40, 204); sp.print(b);
}

static void _draw_item(int y, bool selected, const char* label, const char* value) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextSize(1);
  sp.setTextColor(selected ? TEXT : TEXT_DIM, BG);
  sp.setCursor(40, y);
  sp.print(selected ? "> " : "  ");
  sp.print(label);
  if (value && *value) {
    sp.setCursor(170, y);
    sp.print(value);
  }
}

// --- Main menu -----------------------------------------------------------
void draw_main_menu() {
  _panel_title("Menu", TEXT);
  const FsmView& v = input_fsm_view();
  static const char* LABELS[7] = {
    "settings", "clock", "turn off", "help", "about", "demo", "close"
  };
  int y = 70;
  for (uint8_t i = 0; i < 7; i++) {
    const char* val = nullptr;
    if (i == 5) val = panel_data_demo() ? "on" : "off";
    _draw_item(y, v.menuSel == i, LABELS[i], val);
    y += 16;
  }
  _hints("CW/CCW: scroll", "CLICK: select");
  hw_display_sprite().pushSprite(0, 0);
}

// --- Settings ------------------------------------------------------------
void draw_settings() {
  _panel_title("Settings", TEXT);
  const FsmView& v = input_fsm_view();
  static const char* LABELS[5] = {
    "brightness", "haptic", "transcript", "reset", "back"
  };
  char buf[8];
  int y = 70;
  for (uint8_t i = 0; i < 5; i++) {
    const char* val = nullptr;
    if (i == 0) { snprintf(buf, sizeof(buf), "%u", panel_brightness()); val = buf; }
    else if (i == 1) { snprintf(buf, sizeof(buf), "%u", panel_haptic());     val = buf; }
    else if (i == 2) val = panel_transcript_on() ? "on" : "off";
    _draw_item(y, v.settingsSel == i, LABELS[i], val);
    y += 16;
  }
  _hints("CW/CCW: scroll", "CLICK: change");
  hw_display_sprite().pushSprite(0, 0);
}

// --- Reset ---------------------------------------------------------------
void draw_reset() {
  _panel_title("RESET", HOT);
  const FsmView& v = input_fsm_view();
  static const char* LABELS[3] = { "delete pet", "factory reset", "back" };
  int y = 90;
  uint32_t now = millis();
  for (uint8_t i = 0; i < 3; i++) {
    bool armed = (v.resetConfirmIdx == i) && (now < v.resetConfirmUntil);
    const char* label = armed ? "really?" : LABELS[i];
    TFT_eSprite& sp = hw_display_sprite();
    sp.setTextSize(1);
    uint16_t color = armed ? HOT : (v.resetSel == i ? TEXT : TEXT_DIM);
    sp.setTextColor(color, BG);
    sp.setCursor(40, y);
    sp.print(v.resetSel == i ? "> " : "  ");
    sp.print(label);
    y += 16;
  }
  _hints("CW/CCW: scroll", "CLICK: confirm");
  hw_display_sprite().pushSprite(0, 0);
}

// --- Help ---------------------------------------------------------------
void draw_help() {
  _panel_title("Controls", TEXT);
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextSize(1);
  sp.setTextColor(TEXT_DIM, BG);
  int y = 70;
  sp.setCursor(40, y); sp.print("Turn knob   scroll");             y += 12;
  sp.setCursor(40, y); sp.print("Click       select/toggle");      y += 12;
  sp.setCursor(40, y); sp.print("Long press  menu / back home");   y += 12;
  sp.setCursor(40, y); sp.print("Double      (reserved)");         y += 16;
  sp.drawFastHLine(40, y, 160, TEXT_DIM);                           y += 6;
  sp.setCursor(40, y); sp.print("Home:  long press menu");         y += 12;
  sp.setCursor(40, y); sp.print("Clock: menu > clock");
  _hints("CLICK or LONG", "to return");
  sp.pushSprite(0, 0);
}

// --- About --------------------------------------------------------------
void draw_about() {
  _panel_title("About", TEXT);
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextSize(1);
  sp.setTextColor(TEXT_DIM, BG);
  int y = 70;
  sp.setCursor(40, y); sp.print("claude-desktop");          y += 12;
  sp.setCursor(40, y); sp.print("-buddy  X-Knob");          y += 16;
  sp.drawFastHLine(40, y, 160, TEXT_DIM);                    y += 6;
  sp.setCursor(40, y); sp.print("by Felix Rieseberg");      y += 12;
  sp.setCursor(40, y); sp.print("+ community");             y += 12;
  sp.setCursor(40, y); sp.print("X-Knob port: you");        y += 16;
  sp.setCursor(40, y); sp.print("github:ZinkLu/");          y += 12;
  sp.setCursor(40, y); sp.print("  claude-desktop-buddy");
  _hints("CLICK or LONG", "to return");
  sp.pushSprite(0, 0);
}

// --- Passkey ------------------------------------------------------------
void draw_passkey() {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TEXT_DIM, BG);
  sp.setTextSize(2);
  sp.drawString("BT PAIRING", 120, 50);

  uint32_t pk = blePasskey();
  char b[8];
  snprintf(b, sizeof(b), "%06lu", (unsigned long)pk);
  sp.setTextColor(TEXT, BG);
  sp.setTextSize(5);
  sp.drawString(b, 120, 120);

  sp.setTextSize(1);
  sp.setTextColor(TEXT_DIM, BG);
  sp.drawString("Enter on", 120, 170);
  sp.drawString("your computer", 120, 184);

  sp.setTextDatum(TL_DATUM);
  sp.pushSprite(0, 0);
}
