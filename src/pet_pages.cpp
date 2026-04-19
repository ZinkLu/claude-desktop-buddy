#include "pet_pages.h"
#include <Arduino.h>
#include "hw_display.h"
#include "buddy.h"

// Bridges to main.cpp for stats.
extern uint8_t     pet_mood_tier();      // 0..4
extern uint8_t     pet_fed_progress();   // 0..10
extern uint8_t     pet_energy_tier();    // 0..5
extern uint8_t     pet_level();
extern uint16_t    pet_approvals();
extern uint16_t    pet_denials();
extern uint32_t    pet_nap_seconds();
extern uint32_t    pet_tokens_total();
extern uint32_t    pet_tokens_today();

static const uint16_t BG       = TFT_BLACK;
static const uint16_t TEXT     = TFT_WHITE;
static const uint16_t TEXT_DIM = TFT_DARKGREY;
static const uint16_t HEART    = 0xF810;
static const uint16_t HOT      = 0xFA20;

static void tiny_heart(int x, int y, bool filled, uint16_t col) {
  TFT_eSprite& sp = hw_display_sprite();
  if (filled) {
    sp.fillCircle(x - 2, y, 2, col);
    sp.fillCircle(x + 2, y, 2, col);
    sp.fillTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 5, col);
  } else {
    sp.drawCircle(x - 2, y, 2, col);
    sp.drawCircle(x + 2, y, 2, col);
    sp.drawLine(x - 4, y + 1, x, y + 5, col);
    sp.drawLine(x + 4, y + 1, x, y + 5, col);
  }
}

void draw_pet_main(uint8_t personaState, bool showHint) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);

  // Force buddy to repaint its canvas every frame. Its internal 5fps tick
  // gate would otherwise early-return and leave the sprite all black
  // between ticks (same bug Phase 1 Task 7 fixed for home).
  buddyInvalidate();
  buddyTick(personaState);

  // Hint at top (fades out via showHint from main).
  if (showHint) {
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TEXT_DIM, BG);
    sp.setTextSize(1);
    sp.drawString("pet me", 120, 30);
    sp.setTextDatum(TL_DATUM);
  }

  // Compact stats footer: mood hearts + level pill.
  uint8_t mood = pet_mood_tier();
  uint16_t moodCol = (mood >= 3) ? HEART : (mood >= 2) ? HOT : TEXT_DIM;
  sp.setTextColor(TEXT_DIM, BG);
  sp.setTextSize(1);
  sp.setCursor(40, 200); sp.print("mood");
  for (int i = 0; i < 4; i++) tiny_heart(80 + i * 12, 204, i < mood, moodCol);

  char buf[12];
  snprintf(buf, sizeof(buf), "lv %u", (unsigned)pet_level());
  sp.setCursor(160, 200); sp.print(buf);

  sp.pushSprite(0, 0);
}

void draw_pet_stats() {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);
  sp.setTextDatum(MC_DATUM);

  char buf[32];

  // Mood hearts — 4 across, centered at x=120, 12 px spacing.
  int y = 48;
  uint8_t mood = pet_mood_tier();
  uint16_t moodCol = (mood >= 3) ? HEART : (mood >= 2) ? HOT : TEXT_DIM;
  for (int i = 0; i < 4; i++) {
    int x = 120 - 18 + i * 12;
    tiny_heart(x, y, i < mood, moodCol);
  }

  // Level — big pill-style number in heart color.
  y = 75;
  sp.setTextSize(3);
  sp.setTextColor(HEART, BG);
  snprintf(buf, sizeof(buf), "Lv %u", (unsigned)pet_level());
  sp.drawString(buf, 120, y);

  // Approved / denied — medium size, most-used counters.
  y = 115;
  sp.setTextSize(2);
  sp.setTextColor(TEXT, BG);
  snprintf(buf, sizeof(buf), "approved %u", (unsigned)pet_approvals());
  sp.drawString(buf, 120, y);
  y += 22;
  snprintf(buf, sizeof(buf), "denied %u", (unsigned)pet_denials());
  sp.drawString(buf, 120, y);

  // Napped + tokens — smaller, denser.
  y = 168;
  sp.setTextSize(1);
  sp.setTextColor(TEXT_DIM, BG);
  uint32_t nap = pet_nap_seconds();
  snprintf(buf, sizeof(buf), "napped %luh%02lum", nap / 3600, (nap / 60) % 60);
  sp.drawString(buf, 120, y);
  y += 12;

  auto tok_fmt = [&](uint32_t v, char* out, size_t n) {
    if      (v >= 1000000) snprintf(out, n, "%lu.%luM", v / 1000000, (v / 100000) % 10);
    else if (v >= 1000)    snprintf(out, n, "%lu.%luK", v / 1000, (v / 100) % 10);
    else                   snprintf(out, n, "%lu", (unsigned long)v);
  };
  char tok[12];
  tok_fmt(pet_tokens_total(), tok, sizeof(tok));
  snprintf(buf, sizeof(buf), "tokens %s  today %s", tok, "");
  // Build "tokens X  today Y" as one centered line
  char today[12];
  tok_fmt(pet_tokens_today(), today, sizeof(today));
  snprintf(buf, sizeof(buf), "tokens %s  today %s", tok, today);
  sp.drawString(buf, 120, y);

  sp.setTextDatum(TL_DATUM);
  sp.pushSprite(0, 0);
}
