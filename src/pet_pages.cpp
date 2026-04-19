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

  // Character — reuse buddy renderer. Scale stays at peek=false (2x home size).
  // The buddy tick call must be driven by main.cpp; we only draw the sprite
  // state after the tick has run. But since draw_pet_main is called AFTER
  // buddyTick in the main loop, the sprite already has the character painted
  // for DISP_HOME's typical y=BUDDY_Y_BASE=55 area. That works for pet too.

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
  sp.setTextDatum(TL_DATUM);
  sp.setTextSize(1);

  int y = 40;
  // mood
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, y); sp.print("mood");
  uint8_t mood = pet_mood_tier();
  uint16_t moodCol = (mood >= 3) ? HEART : (mood >= 2) ? HOT : TEXT_DIM;
  for (int i = 0; i < 4; i++) tiny_heart(90 + i * 14, y + 4, i < mood, moodCol);

  y += 16;
  sp.setTextColor(TEXT, BG);
  sp.setCursor(40, y); sp.print("fed");
  uint8_t fed = pet_fed_progress();
  for (int i = 0; i < 10; i++) {
    int px = 80 + i * 10;
    if (i < fed) sp.fillCircle(px, y + 4, 2, TEXT);
    else         sp.drawCircle(px, y + 4, 2, TEXT_DIM);
  }

  y += 16;
  sp.setCursor(40, y); sp.print("energy");
  uint8_t en = pet_energy_tier();
  uint16_t enCol = (en >= 4) ? 0x07FF : (en >= 2) ? 0xFFE0 : HOT;
  for (int i = 0; i < 5; i++) {
    int px = 100 + i * 14;
    if (i < en) sp.fillRect(px, y, 9, 8, enCol);
    else        sp.drawRect(px, y, 9, 8, TEXT_DIM);
  }

  y += 22;
  char buf[24];
  sp.fillRoundRect(40, y, 52, 16, 3, HEART);
  sp.setTextColor(BG, HEART);
  sp.setCursor(48, y + 4);
  snprintf(buf, sizeof(buf), "Lv %u", (unsigned)pet_level());
  sp.print(buf);

  y += 24;
  sp.setTextColor(TEXT_DIM, BG);
  snprintf(buf, sizeof(buf), "approved  %u", (unsigned)pet_approvals());
  sp.setCursor(40, y); sp.print(buf); y += 12;
  snprintf(buf, sizeof(buf), "denied    %u", (unsigned)pet_denials());
  sp.setCursor(40, y); sp.print(buf); y += 12;
  uint32_t nap = pet_nap_seconds();
  snprintf(buf, sizeof(buf), "napped    %luh%02lum", nap / 3600, (nap / 60) % 60);
  sp.setCursor(40, y); sp.print(buf); y += 12;

  // Token formatting: big numbers -> K / M.
  auto tok_fmt = [&](uint32_t v, char* out, size_t n) {
    if      (v >= 1000000) snprintf(out, n, "%lu.%luM", v / 1000000, (v / 100000) % 10);
    else if (v >= 1000)    snprintf(out, n, "%lu.%luK", v / 1000, (v / 100) % 10);
    else                   snprintf(out, n, "%lu", (unsigned long)v);
  };
  char tok[12];
  tok_fmt(pet_tokens_total(), tok, sizeof(tok));
  snprintf(buf, sizeof(buf), "tokens    %s", tok);
  sp.setCursor(40, y); sp.print(buf); y += 12;
  tok_fmt(pet_tokens_today(), tok, sizeof(tok));
  snprintf(buf, sizeof(buf), "today     %s", tok);
  sp.setCursor(40, y); sp.print(buf);

  sp.pushSprite(0, 0);
}
