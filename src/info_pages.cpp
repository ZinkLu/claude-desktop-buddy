#include "info_pages.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "esp_mac.h"
#include "hw_display.h"
#include "ble_bridge.h"

// Live-state bridges owned by main.cpp (same pattern as menu_panels).
extern uint8_t panel_brightness();
extern uint8_t panel_haptic();
extern const char* info_bt_name();
extern uint8_t info_sessions_total();
extern uint8_t info_sessions_running();
extern uint8_t info_sessions_waiting();
extern uint32_t info_last_msg_age_s();
extern const char* info_claude_state_name();
extern const char* info_bt_status();   // "linked" / "discover" / "off"
extern const char* info_scenario_name();

static const uint16_t BG       = TFT_BLACK;
static const uint16_t TEXT     = TFT_WHITE;
static const uint16_t TEXT_DIM = TFT_DARKGREY;

static void panel_title(const char* t) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);
  sp.setTextDatum(TL_DATUM);
  sp.setTextSize(2);
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 28);
  sp.print(t);
  sp.drawFastHLine(40, 50, 160, TEXT_DIM);
  sp.setTextSize(1);
}

static void page_footer(uint8_t page) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 208);
  char buf[12];
  snprintf(buf, sizeof(buf), "page %u/4", (unsigned)(page + 1));
  sp.print(buf);
}

static void draw_about() {
  panel_title("ABOUT");
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 60);  sp.print("claude-desktop-buddy");
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 72);  sp.print("X-Knob port");
  sp.setCursor(40, 88);  sp.print("Watches Claude Desktop");
  sp.setCursor(40, 100); sp.print("sessions. Sleeps idle,");
  sp.setCursor(40, 112); sp.print("wakes on work, shows");
  sp.setCursor(40, 124); sp.print("approvals.");
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 144); sp.print("Controls");
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 156); sp.print("turn  scroll");
  sp.setCursor(40, 168); sp.print("click select");
  sp.setCursor(40, 180); sp.print("long  menu / back");
  page_footer(0);
}

static void draw_claude() {
  panel_title("CLAUDE");
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 60);  sp.print("sessions");
  sp.setTextColor(TEXT_DIM, BG);
  char buf[24];
  snprintf(buf, sizeof(buf), "total   %u", (unsigned)info_sessions_total());
  sp.setCursor(52, 72);  sp.print(buf);
  snprintf(buf, sizeof(buf), "running %u", (unsigned)info_sessions_running());
  sp.setCursor(52, 84);  sp.print(buf);
  snprintf(buf, sizeof(buf), "waiting %u", (unsigned)info_sessions_waiting());
  sp.setCursor(52, 96);  sp.print(buf);

  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 116); sp.print("link");
  sp.setTextColor(TEXT_DIM, BG);
  snprintf(buf, sizeof(buf), "via      %s", info_scenario_name());
  sp.setCursor(52, 128); sp.print(buf);
  snprintf(buf, sizeof(buf), "ble      %s", bleConnected() ? (bleSecure() ? "encrypted" : "open") : "-");
  sp.setCursor(52, 140); sp.print(buf);
  snprintf(buf, sizeof(buf), "last msg %lus", (unsigned long)info_last_msg_age_s());
  sp.setCursor(52, 152); sp.print(buf);
  snprintf(buf, sizeof(buf), "state    %s", info_claude_state_name());
  sp.setCursor(52, 164); sp.print(buf);

  page_footer(1);
}

static void draw_system() {
  panel_title("SYSTEM");
  TFT_eSprite& sp = hw_display_sprite();

  char buf[32];
  uint32_t up = millis() / 1000;
  snprintf(buf, sizeof(buf), "uptime %luh %02lum", up / 3600, (up / 60) % 60);
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 60); sp.print(buf);

  snprintf(buf, sizeof(buf), "heap   %uKB", (unsigned)(ESP.getFreeHeap() / 1024));
  sp.setCursor(40, 72); sp.print(buf);

  snprintf(buf, sizeof(buf), "bright %u/4", (unsigned)panel_brightness());
  sp.setCursor(40, 84); sp.print(buf);

  snprintf(buf, sizeof(buf), "haptic %u/4", (unsigned)panel_haptic());
  sp.setCursor(40, 96); sp.print(buf);

  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, 116); sp.print("bluetooth");
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(52, 128); sp.print(info_bt_name());

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  sp.setCursor(52, 140); sp.print(buf);

  sp.setCursor(52, 152); sp.print(info_bt_status());

  page_footer(2);
}

static void draw_credits() {
  panel_title("CREDITS");
  TFT_eSprite& sp = hw_display_sprite();
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 60);  sp.print("made by");
  sp.setTextColor(TEXT, BG);
  sp.setCursor(52, 72);  sp.print("Felix Rieseberg");
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(40, 96);  sp.print("source");
  sp.setCursor(52, 108); sp.print("github.com/anthropics");
  sp.setCursor(52, 120); sp.print("  /claude-desktop-buddy");
  sp.setCursor(40, 140); sp.print("X-Knob port");
  sp.setCursor(52, 152); sp.print("github.com/ZinkLu");
  sp.setCursor(52, 164); sp.print("  /claude-desktop-buddy");
  sp.setCursor(40, 184); sp.print("hardware: X-Knob ESP32-S3");
  page_footer(3);
}

void draw_info_page(uint8_t page) {
  switch (page) {
    case 0: draw_about();   break;
    case 1: draw_claude();  break;
    case 2: draw_system();  break;
    case 3: draw_credits(); break;
    default: draw_about();  break;
  }
  hw_display_sprite().pushSprite(0, 0);
}
