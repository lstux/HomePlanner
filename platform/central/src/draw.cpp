#include "draw.h"
#include "board_config.h"

namespace {
uint16_t slice[LCD_W * HP_SLICE];

// Mélange de deux couleurs RGB565 ; a de 0 (bg) à 255 (fg).
uint16_t mix(uint16_t fg, uint16_t bg, int a) {
  if (a >= 255) return fg;
  if (a <= 0) return bg;
  const int r = ((fg >> 11) * a + (bg >> 11) * (255 - a) + 127) / 255;
  const int g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (255 - a) + 127) / 255;
  const int b = ((fg & 31) * a + (bg & 31) * (255 - a) + 127) / 255;
  return static_cast<uint16_t>(r << 11 | g << 5 | b);
}

// Caractère UTF-8 suivant ; avance le pointeur.
uint32_t nextCode(const char*& p) {
  const unsigned char c = static_cast<unsigned char>(*p++);
  if (c < 0x80) return c;
  int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
  uint32_t code = c & (0x3F >> more);
  while (more-- && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) code = code << 6 | (static_cast<unsigned char>(*p++) & 0x3F);
  return code;
}

const HpGlyph* findGlyph(const HpFont& font, uint32_t code) {
  int low = 0, high = font.count - 1;
  while (low <= high) {
    const int middle = (low + high) / 2;
    if (font.glyphs[middle].code == code) return &font.glyphs[middle];
    if (font.glyphs[middle].code < code) low = middle + 1;
    else high = middle - 1;
  }
  return nullptr;
}

// Couleur du fond au pixel (x, y) : disque à bord lissé sur un pixel (distances au quart de pixel).
uint16_t discAt(const HpDisc& d, int x, int y) {
  if (d.radius <= 0) return d.bg;
  const int dx = x - d.cx, dy = y - d.cy, square = dx * dx + dy * dy, r = d.radius;
  if (square <= (r - 1) * (r - 1)) return d.color;
  if (square >= (r + 1) * (r + 1)) return d.bg;
  // Près du bord, distance ≈ r + (d² - r²) / 2r.
  return mix(d.color, d.bg, 128 - (square - r * r) * 255 / (2 * r));
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

void drawRound(int x, int y, int w, int h, int radius, uint16_t color, uint16_t bg) {
  if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > LCD_W || y + h > LCD_H) return;
  if (radius * 2 > w) radius = w / 2;
  if (radius * 2 > h) radius = h / 2;
  for (int top = 0; top < h; top += HP_SLICE) {
    const int rows = h - top < HP_SLICE ? h - top : HP_SLICE;
    for (int row = 0; row < rows; ++row)
      for (int col = 0; col < w; ++col) {
        const int line = top + row;
        const int cx = col < radius ? radius : col >= w - radius ? w - 1 - radius : col;
        const int cy = line < radius ? radius : line >= h - radius ? h - 1 - radius : line;
        const HpDisc corner = {cx, cy, radius, color, bg};
        slice[row * w + col] = cx == col && cy == line ? color : discAt(corner, col, line);
      }
    lcdBlit(x, y + top, w, rows, slice);
  }
}

void drawDisc(const HpDisc& d) {
  int x = d.cx - d.radius - 1, y = d.cy - d.radius - 1, w = d.radius * 2 + 3, h = w;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;
  for (int top = 0; top < h; top += HP_SLICE) {
    const int rows = h - top < HP_SLICE ? h - top : HP_SLICE;
    for (int row = 0; row < rows; ++row)
      for (int col = 0; col < w; ++col) slice[row * w + col] = discAt(d, x + col, y + top + row);
    lcdBlit(x, y + top, w, rows, slice);
  }
}

int textWidth(const char* text, const HpFont& font) {
  int width = 0;
  for (const char* p = text; *p;) {
    const HpGlyph* g = findGlyph(font, nextCode(p));
    if (g) width += g->advance;
  }
  return width;
}

void drawTextIn(int boxX, int boxW, int y, const char* text, const HpFont& font, uint16_t fg, uint16_t bg) {
  if (!text || y < 0 || y + font.height > LCD_H || boxX < 0 || boxW < 1 || boxX + boxW > LCD_W) return;
  int left = (boxW - textWidth(text, font)) / 2;
  if (left < 0) left = 0;  // trop long : coupé à droite
  uint16_t shades[16];
  for (int i = 0; i < 16; ++i) shades[i] = mix(fg, bg, i * 17);
  for (int top = 0; top < font.height; top += HP_SLICE) {
    const int rows = font.height - top < HP_SLICE ? font.height - top : HP_SLICE;
    for (int i = 0; i < boxW * rows; ++i) slice[i] = bg;
    int x = left;
    for (const char* p = text; *p;) {
      const HpGlyph* g = findGlyph(font, nextCode(p));
      if (!g) continue;
      if (x + g->advance > boxW) break;
      for (int row = 0; row < rows; ++row) {
        const int line = top + row - g->y;
        if (line < 0 || line >= g->h) continue;
        for (int col = 0; col < g->w; ++col) {
          const int at = line * g->w + col, target = x + g->x + col;
          const uint8_t pair = font.bits[g->offset + at / 2];
          const int level = at % 2 ? pair & 15 : pair >> 4;
          if (level && target >= 0 && target < boxW) slice[row * boxW + target] = shades[level];
        }
      }
      x += g->advance;
    }
    lcdBlit(boxX, y + top, boxW, rows, slice);
  }
}

void drawText(int y, const char* text, const HpFont& font, uint16_t fg, uint16_t bg) { drawTextIn(0, LCD_W, y, text, font, fg, bg); }

void drawImage(int x, int y, const HpImage& image, const HpDisc& back) {
  if (x < 0 || y < 0 || x + image.w > LCD_W || y + image.h > LCD_H) return;
  size_t at = 0;
  int run = 0;
  uint8_t index = 0;
  for (int top = 0; top < image.h; top += HP_SLICE) {
    const int rows = image.h - top < HP_SLICE ? image.h - top : HP_SLICE;
    for (int i = 0; i < image.w * rows; ++i) {
      if (!run) {
        if (at + 1 >= image.size) return;
        run = image.data[at];
        index = image.data[at + 1];
        at += 2;
      }
      const int a = image.alpha[index];
      slice[i] = a >= 255 ? image.palette[index]
                          : mix(image.palette[index], discAt(back, x + i % image.w, y + top + i / image.w), a);
      --run;
    }
    lcdBlit(x, y + top, image.w, rows, slice);
  }
}
