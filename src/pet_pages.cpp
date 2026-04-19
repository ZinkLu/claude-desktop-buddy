#include "pet_pages.h"
#include <Arduino.h>
#include "hw_display.h"
#include "buddy.h"

// Bridges to main.cpp for stats.
extern uint8_t     pet_mood_tier();      // 0..4
extern uint8_t     pet_fed_progress();   // 0..10 (unused in current layout)
extern uint8_t     pet_energy_tier();    // 0..5  (unused)
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

static void tok_fmt(uint32_t v, char* out, size_t n) {
  if      (v >= 1000000) snprintf(out, n, "%lu.%luM", v / 1000000, (v / 100000) % 10);
  else if (v >= 1000)    snprintf(out, n, "%lu.%luK", v / 1000, (v / 100) % 10);
  else                   snprintf(out, n, "%lu", (unsigned long)v);
}

void draw_pet_main(uint8_t personaState, bool showHint) {
  TFT_eSprite& sp = hw_display_sprite();
  sp.fillSprite(BG);

  // Force buddy to repaint its canvas every frame — its 5fps tick gate
  // otherwise leaves the sprite black between animation frames.
  buddyInvalidate();
  buddyTick(personaState);

  // Top hint "pet me" fades after 3 s (showHint from caller).
  if (showHint) {
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TEXT_DIM, BG);
    sp.setTextSize(1);
    sp.drawString("pet me", 120, 30);
    sp.setTextDatum(TL_DATUM);
  }

  // Full stats footer (y=150..210, 60 px). All stats tracked in stats.h
  // surface here; no sub-page. 4 rows, all size-1 for density, except
  // "Lv N" which uses size-2 at heart color so the level badge pops.
  //
  //  Row 1 (y≈160):  ♥♥♥♥      Lv 3           mood hearts + level
  //  Row 2 (y≈178):  fed ●●●●●○○○○○  en █████  fed pips + energy bars
  //  Row 3 (y≈193):  appr 42  deny 3  nap 2h   counters
  //  Row 4 (y≈206):  127.4K total  8.2K today  tokens
  char buf[48], tok[12], today[12];

  // Row 1: hearts (center-left) + Lv size-2 (center-right).
  uint8_t mood = pet_mood_tier();
  uint16_t moodCol = (mood >= 3) ? HEART : (mood >= 2) ? HOT : TEXT_DIM;
  for (int i = 0; i < 4; i++) tiny_heart(64 + i * 12, 160, i < mood, moodCol);
  sp.setTextDatum(ML_DATUM);
  sp.setTextColor(HEART, BG);
  sp.setTextSize(2);
  snprintf(buf, sizeof(buf), "Lv %u", (unsigned)pet_level());
  sp.drawString(buf, 140, 160);

  // Row 2: fed pips + energy bars. Both visual meters on one line.
  //   layout: "fed" label (x=28) + 10 pips (x=50..118) + "en" (x=128) + 5 bars (x=150..198)
  sp.setTextSize(1);
  sp.setTextColor(TEXT_DIM, BG);
  sp.setCursor(28, 175); sp.print("fed");
  uint8_t fed = pet_fed_progress();
  for (int i = 0; i < 10; i++) {
    int px = 54 + i * 7;
    if (i < fed) sp.fillCircle(px, 178, 2, TEXT);
    else         sp.drawCircle(px, 178, 2, TEXT_DIM);
  }
  sp.setCursor(130, 175); sp.print("en");
  uint8_t en = pet_energy_tier();
  uint16_t enCol = (en >= 4) ? 0x07FF : (en >= 2) ? 0xFFE0 : HOT;
  for (int i = 0; i < 5; i++) {
    int px = 150 + i * 10;
    if (i < en) sp.fillRect(px, 174, 7, 8, enCol);
    else        sp.drawRect(px, 174, 7, 8, TEXT_DIM);
  }

  // Row 3: approved / denied / nap (compact, centered).
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TEXT, BG);
  uint32_t nap = pet_nap_seconds();
  snprintf(buf, sizeof(buf), "appr %u  deny %u  nap %luh%02lum",
           (unsigned)pet_approvals(), (unsigned)pet_denials(),
           nap / 3600, (nap / 60) % 60);
  sp.drawString(buf, 120, 193);

  // Row 4: tokens total + today.
  sp.setTextColor(TEXT_DIM, BG);
  tok_fmt(pet_tokens_total(), tok,   sizeof(tok));
  tok_fmt(pet_tokens_today(), today, sizeof(today));
  snprintf(buf, sizeof(buf), "%s total  %s today", tok, today);
  sp.drawString(buf, 120, 206);

  sp.setTextDatum(TL_DATUM);
  sp.pushSprite(0, 0);
}
