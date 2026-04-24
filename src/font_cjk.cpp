#include "font_cjk.h"
#include "font_cjk_data.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Internal state
// ---------------------------------------------------------------------------
static Arduino_GFX* _cjk_target = nullptr;

// ---------------------------------------------------------------------------
//  UTF-8 decoder
// ---------------------------------------------------------------------------

// Decode one UTF-8 codepoint from *pp, advance *pp.
// Returns 0 on invalid sequence or end of string.
static uint32_t decode_utf8(const char** pp) {
  const uint8_t* p = (const uint8_t*)*pp;
  uint8_t c = *p;
  if (c == 0) return 0;

  if (c < 0x80) {
    *pp = (const char*)(p + 1);
    return c;
  }

  if ((c & 0xE0) == 0xC0) {
    // 2-byte sequence
    if (p[1] == 0) return 0;
    uint32_t cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
    *pp = (const char*)(p + 2);
    return cp;
  }

  if ((c & 0xF0) == 0xE0) {
    // 3-byte sequence (CJK)
    if (p[1] == 0 || p[2] == 0) return 0;
    uint32_t cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    *pp = (const char*)(p + 3);
    return cp;
  }

  if ((c & 0xF8) == 0xF0) {
    // 4-byte sequence
    if (p[1] == 0 || p[2] == 0 || p[3] == 0) return 0;
    uint32_t cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    *pp = (const char*)(p + 4);
    return cp;
  }

  // Invalid lead byte, skip
  *pp = (const char*)(p + 1);
  return 0;
}

// ---------------------------------------------------------------------------
//  Glyph lookup (binary search in PROGMEM)
// ---------------------------------------------------------------------------

static int lookup_glyph(uint32_t cp) {
  int lo = 0;
  int hi = CJK_GLYPH_COUNT - 1;

  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint32_t mid_cp = pgm_read_dword(&CJK_CODEPOINTS[mid]);
    if (mid_cp == cp) return mid;
    if (mid_cp < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

// ---------------------------------------------------------------------------
//  Bitmap blit (1bpp → RGB565)
// ---------------------------------------------------------------------------

static void draw_glyph_1x(int x, int y, int glyph_idx, uint16_t color, uint16_t bg) {
  const uint8_t* bitmap = CJK_BITMAPS[glyph_idx];
  const int w = CJK_FONT_SIZE;
  const int h = CJK_FONT_SIZE;
  const int bytes_per_row = (w + 7) >> 3;

  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      int byte_idx = row * bytes_per_row + (col >> 3);
      int bit_idx = 7 - (col & 7);
      uint8_t byte = pgm_read_byte(&bitmap[byte_idx]);
      bool pixel = (byte >> bit_idx) & 1;

      if (pixel || bg != BLACK) {
        _cjk_target->drawPixel(x + col, y + row, pixel ? color : bg);
      }
    }
  }
}

static void draw_glyph_2x(int x, int y, int glyph_idx, uint16_t color, uint16_t bg) {
  const uint8_t* bitmap = CJK_BITMAPS[glyph_idx];
  const int bytes_per_row = (CJK_FONT_SIZE + 7) >> 3;

  for (int row = 0; row < CJK_FONT_SIZE; row++) {
    for (int col = 0; col < CJK_FONT_SIZE; col++) {
      int byte_idx = row * bytes_per_row + (col >> 3);
      int bit_idx = 7 - (col & 7);
      uint8_t byte = pgm_read_byte(&bitmap[byte_idx]);
      bool pixel = (byte >> bit_idx) & 1;
      if (pixel || bg != BLACK) {
        uint16_t px_color = pixel ? color : bg;
        _cjk_target->fillRect(x + col * 2, y + row * 2, 2, 2, px_color);
      }
    }
  }
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

void cjk_font_init() {
  // Nothing needed for PROGMEM data
}

void cjk_set_target(Arduino_GFX* gfx) {
  _cjk_target = gfx;
}

void cjk_draw_string(int x, int y, const char* utf8_str,
                     uint16_t color, uint16_t bg, uint8_t font_size) {
  if (!_cjk_target || !utf8_str) return;

  int cursor_x = x;
  int cursor_y = y;
  int scale = (font_size == 2) ? 2 : 1;
  int glyph_w = CJK_FONT_SIZE * scale;

  const char* p = utf8_str;
  while (*p) {
    uint32_t cp = decode_utf8(&p);
    if (cp == 0) break;

    if (cp < 0x80) {
      // ASCII: use GFX built-in font
      _cjk_target->setTextColor(color, bg);
      _cjk_target->setTextSize(font_size);
      _cjk_target->setCursor(cursor_x, cursor_y);
      _cjk_target->print((char)cp);
      cursor_x += 6 * font_size;
    } else {
      // CJK: bitmap lookup
      int glyph_idx = lookup_glyph(cp);
      if (glyph_idx >= 0) {
        if (scale == 1) {
          draw_glyph_1x(cursor_x, cursor_y, glyph_idx, color, bg);
        } else {
          draw_glyph_2x(cursor_x, cursor_y, glyph_idx, color, bg);
        }
        cursor_x += glyph_w;
      } else {
        // Missing glyph: draw placeholder box
        _cjk_target->drawRect(cursor_x + 1, cursor_y + 1, glyph_w - 2, CJK_FONT_SIZE * scale - 2, color);
        cursor_x += glyph_w;
      }
    }
  }
}

int cjk_text_width(const char* utf8_str, uint8_t font_size) {
  if (!utf8_str) return 0;

  int width = 0;
  int glyph_w = CJK_FONT_SIZE * ((font_size == 2) ? 2 : 1);

  const char* p = utf8_str;
  while (*p) {
    uint32_t cp = decode_utf8(&p);
    if (cp == 0) break;

    if (cp < 0x80) {
      width += 6 * font_size;
    } else {
      width += glyph_w;
    }
  }
  return width;
}

int cjk_utf8_strlen(const char* utf8_str) {
  if (!utf8_str) return 0;

  int count = 0;
  const char* p = utf8_str;
  while (*p) {
    uint32_t cp = decode_utf8(&p);
    if (cp != 0) count++;
  }
  return count;
}