#include "draw.h"
#include <string.h>
#include "board_config.h"

namespace {
const uint8_t font[36][5]={
{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
{0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x72,0x49,0x49,0x49,0x46},{0x21,0x41,0x49,0x4d,0x33},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x31},{0x41,0x21,0x11,0x09,0x07},{0x36,0x49,0x49,0x49,0x36},{0x46,0x49,0x49,0x29,0x1e}};
const char punct[] = ".-:/_'+*";
const uint8_t punctFont[][5] = {{0x00,0x60,0x60,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x36,0x36,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x40,0x40,0x40,0x40,0x40},{0x00,0x00,0x07,0x00,0x00},{0x08,0x08,0x3e,0x08,0x08},{0x00,0x06,0x09,0x09,0x06}};
uint16_t slice[LCD_W * HP_SLICE];

// Lettre sans accent d'un caractère UTF-8 latin (é -> E) ; avance le pointeur.
char nextChar(const char*& p) {
  const unsigned char c = static_cast<unsigned char>(*p++);
  if (c < 0x80) return c >= 'a' && c <= 'z' ? c - 32 : c;
  if (c == 0xC3 && *p) {
    const unsigned char d = static_cast<unsigned char>(*p++) | 0x20;  // minuscule
    if (d >= 0xA0 && d <= 0xA5) return 'A';
    if (d == 0xA7) return 'C';
    if (d >= 0xA8 && d <= 0xAB) return 'E';
    if (d >= 0xAC && d <= 0xAF) return 'I';
    if (d == 0xB1) return 'N';
    if (d >= 0xB2 && d <= 0xB6) return 'O';
    if (d >= 0xB9 && d <= 0xBC) return 'U';
    if (d == 0xBD || d == 0xBF) return 'Y';
    return ' ';
  }
  while ((static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
  return ' ';
}

const uint8_t* glyph(char c) {
  if (c >= 'A' && c <= 'Z') return font[c - 'A'];
  if (c >= '0' && c <= '9') return font[26 + c - '0'];
  const char* found = c ? strchr(punct, c) : nullptr;
  return found ? punctFont[found - punct] : nullptr;
}
}  // namespace

void drawRect(int x, int y, int w, int h, uint16_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;
  for (int i = 0; i < w * HP_SLICE; ++i) slice[i] = color;
  for (int row = 0; row < h; row += HP_SLICE) lcdBlit(x, y + row, w, h - row < HP_SLICE ? h - row : HP_SLICE, slice);
}

void drawFill(uint16_t color) { drawRect(0, 0, LCD_W, LCD_H, color); }

int textWidth(const char* text, int scale) {
  int count = 0;
  for (const char* p = text; *p;) { nextChar(p); ++count; }
  return count ? count * 6 * scale - scale : 0;
}

void drawTextIn(int boxX, int boxW, int y, const char* text, int scale, uint16_t fg, uint16_t bg) {
  if (!text || scale < 1 || y < 0 || y + 8 * scale > LCD_H || boxX < 0 || boxW < 1 || boxX + boxW > LCD_W) return;
  int left = (boxW - textWidth(text, scale)) / 2;
  if (left < 0) left = 0;  // trop long : coupé à droite
  for (int top = 0; top < 8 * scale; top += HP_SLICE) {
    const int rows = 8 * scale - top < HP_SLICE ? 8 * scale - top : HP_SLICE;
    for (int i = 0; i < boxW * rows; ++i) slice[i] = bg;
    int x = left;
    for (const char* p = text; *p && x + 5 * scale <= boxW; x += 6 * scale) {
      const uint8_t* bits = glyph(nextChar(p));
      if (!bits) continue;
      for (int row = 0; row < rows; ++row) {
        const int line = (top + row) / scale;
        for (int col = 0; col < 5; ++col)
          if (line < 7 && (bits[col] & (1 << line)))
            for (int dx = 0; dx < scale; ++dx) slice[row * boxW + x + col * scale + dx] = fg;
      }
    }
    lcdBlit(boxX, y + top, boxW, rows, slice);
  }
}

void drawText(int y, const char* text, int scale, uint16_t fg, uint16_t bg) { drawTextIn(0, LCD_W, y, text, scale, fg, bg); }

void drawImage(int x, int y, const HpImage& image) {
  if (x < 0 || y < 0 || x + image.w > LCD_W || y + image.h > LCD_H) return;
  size_t at = 0;
  int run = 0;
  uint16_t color = 0;
  for (int top = 0; top < image.h; top += HP_SLICE) {
    const int rows = image.h - top < HP_SLICE ? image.h - top : HP_SLICE;
    for (int i = 0; i < image.w * rows; ++i) {
      if (!run) {
        if (at + 1 >= image.size) return;
        run = image.data[at];
        color = image.palette[image.data[at + 1]];
        at += 2;
      }
      slice[i] = color;
      --run;
    }
    lcdBlit(x, y + top, image.w, rows, slice);
  }
}
