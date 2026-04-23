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

static const uint16_t BG       = BLACK;
static const uint16_t TEXT     = WHITE;
static const uint16_t TEXT_DIM = DARKGREY;

static void panel_title(const char* t) {
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->fillScreen(BG);
  canvas->setTextSize(2);
  canvas->setTextColor(TEXT, BG);
  canvas->setCursor(40, 28);
  canvas->print(t);
  canvas->drawFastHLine(40, 50, 160, TEXT_DIM);
  canvas->setTextSize(1);
}

static void page_footer(uint8_t page) {
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->setTextColor(TEXT_DIM, BG);
  canvas->setCursor(40, 208);
  char buf[12];
  snprintf(buf, sizeof(buf), "page %u/4", (unsigned)(page + 1));
  canvas->print(buf);
}

static void draw_about() {
  panel_title("ABOUT");
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->setTextColor(TEXT, BG);
  canvas->setCursor(40, 60);  canvas->print("claude-desktop-buddy");
  canvas->setTextColor(TEXT_DIM, BG);
  canvas->setCursor(40, 72);  canvas->print("X-Knob port");
  canvas->setCursor(40, 88);  canvas->print("Watches Claude Desktop");
  canvas->setCursor(40, 100); canvas->print("sessions. Sleeps idle,");
  canvas->setCursor(40, 112); canvas->print("wakes on work, shows");
  canvas->setCursor(40, 124); canvas->print("approvals.");
  canvas->setTextColor(TEXT, BG);
  canvas->setCursor(40, 144); canvas->print("Controls");
  canvas->setTextColor(TEXT_DIM, BG);
  canvas->setCursor(40, 156); canvas->print("turn  scroll");
  canvas->setCursor(40, 168); canvas->print("click select");
  canvas->setCursor(40, 180); canvas->print("long  menu / back");
  page_footer(0);
}

static void draw_claude() {
  panel_title("CLAUDE");
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->setTextColor(TEXT, BG);
  canvas->setCursor(40, 60);  canvas->print("sessions");
  canvas->setTextColor(TEXT_DIM, BG);
  char buf[24];
  snprintf(buf, sizeof(buf), "total   %u", (unsigned)info_sessions_total());
  canvas->setCursor(52, 72);  canvas->print(buf);
  snprintf(buf, sizeof(buf), "running %u", (unsigned)info_sessions_running());
  canvas->setCursor(52, 84);  canvas->print(buf);
  snprintf(buf, sizeof(buf), "waiting %u", (unsigned)info_sessions_waiting());
  canvas->setCursor(52, 96);  canvas->print(buf);

  canvas->setTextColor(TEXT, BG);
  canvas->setCursor(40, 116); canvas->print("link");
  canvas->setTextColor(TEXT_DIM, BG);
  snprintf(buf, sizeof(buf), "via      %s", info_scenario_name());
  canvas->setCursor(52, 128); canvas->print(buf);
  snprintf(buf, sizeof(buf), "ble      %s", bleConnected() ? (bleSecure() ? "encrypted" : "open") : "-");
  canvas->setCursor(52, 140); canvas->print(buf);
  snprintf(buf, sizeof(buf), "last msg %lus", (unsigned long)info_last_msg_age_s());
  canvas->setCursor(52, 152); canvas->print(buf);
  snprintf(buf, sizeof(buf), "state    %s", info_claude_state_name());
  canvas->setCursor(52, 164); canvas->print(buf);

  page_footer(1);
}

static void draw_system() {
  panel_title("SYSTEM");
  Arduino_GFX* canvas = hw_display_canvas();

  char buf[32];
  uint32_t up = millis() / 1000;
  snprintf(buf, sizeof(buf), "uptime %luh %02lum", up / 3600, (up / 60) % 60);
  canvas->setTextColor(TEXT_DIM, BG);
  canvas->setCursor(40, 60); canvas->print(buf);

  snprintf(buf, sizeof(buf), "heap   %uKB", (unsigned)(ESP.getFreeHeap() / 1024));
  canvas->setCursor(40, 72); canvas->print(buf);

  snprintf(buf, sizeof(buf), "bright %u/4", (unsigned)panel_brightness());
  canvas->setCursor(40, 84); canvas->print(buf);

  snprintf(buf, sizeof(buf), "haptic %u/4", (unsigned)panel_haptic());
  canvas->setCursor(40, 96); canvas->print(buf);

  canvas->setTextColor(TEXT, BG);
  canvas->setCursor(40, 116); canvas->print("bluetooth");
  canvas->setTextColor(TEXT_DIM, BG);
  canvas->setCursor(52, 128); canvas->print(info_bt_name());

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  canvas->setCursor(52, 140); canvas->print(buf);

  canvas->setCursor(52, 152); canvas->print(info_bt_status());

  page_footer(2);
}

static void draw_credits() {
  panel_title("CREDITS");
  Arduino_GFX* canvas = hw_display_canvas();
  canvas->setTextColor(TEXT_DIM, BG);
  canvas->setCursor(40, 60);  canvas->print("made by");
  canvas->setTextColor(TEXT, BG);
  canvas->setCursor(52, 72);  canvas->print("Felix Rieseberg");
  canvas->setTextColor(TEXT_DIM, BG);
  canvas->setCursor(40, 96);  canvas->print("source");
  canvas->setCursor(52, 108); canvas->print("github.com/anthropics");
  canvas->setCursor(52, 120); canvas->print("  /claude-desktop-buddy");
  canvas->setCursor(40, 140); canvas->print("X-Knob port");
  canvas->setCursor(52, 152); canvas->print("github.com/ZinkLu");
  canvas->setCursor(52, 164); canvas->print("  /claude-desktop-buddy");
  canvas->setCursor(40, 184); canvas->print("hardware: X-Knob ESP32-S3");
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
  hw_display_flush();
}
