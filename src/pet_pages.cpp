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

  // Full stats footer (y=150..210, 60 px).
  // Row 1 (y=154): mood hearts + Lv N in heart color.
  // Row 2 (y=180): approved / denied counts, size 1, centered.
  // Row 3 (y=196): napped + tokens (total + today), condensed.
  char buf[48], tok[12], today[12];

  // Row 1: hearts left, Lv right — visually "emotional state + progress".
  uint8_t mood = pet_mood_tier();
  uint16_t moodCol = (mood >= 3) ? HEART : (mood >= 2) ? HOT : TEXT_DIM;
  for (int i = 0; i < 4; i++) tiny_heart(66 + i * 12, 158, i < mood, moodCol);
  sp.setTextDatum(ML_DATUM);
  sp.setTextColor(HEART, BG);
  sp.setTextSize(2);
  snprintf(buf, sizeof(buf), "Lv %u", (unsigned)pet_level());
  sp.drawString(buf, 140, 158);

  // Row 2: approved / denied.
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(1);
  sp.setTextColor(TEXT, BG);
  snprintf(buf, sizeof(buf), "approved %u   denied %u",
           (unsigned)pet_approvals(), (unsigned)pet_denials());
  sp.drawString(buf, 120, 180);

  // Row 3: napped + tokens total + today, dim.
  sp.setTextColor(TEXT_DIM, BG);
  uint32_t nap = pet_nap_seconds();
  tok_fmt(pet_tokens_total(), tok,   sizeof(tok));
  tok_fmt(pet_tokens_today(), today, sizeof(today));
  snprintf(buf, sizeof(buf), "nap %luh%02lum   %s today %s",
           nap / 3600, (nap / 60) % 60, tok, today);
  sp.drawString(buf, 120, 196);

  sp.setTextDatum(TL_DATUM);
  sp.pushSprite(0, 0);
}
