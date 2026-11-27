// Dessin portable (testé sur ordinateur) : texte, rectangles et images, envoyés par bandes.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Fourni par la carte (ou par le test) : au plus HP_SLICE lignes à la fois, couleurs RGB565.
void lcdBlit(int x, int y, int w, int h, const uint16_t* pixels);

constexpr int HP_SLICE = 8;

struct HpImage {
  uint16_t w, h;
  const uint16_t* palette;
  const uint8_t* alpha;   // transparence de chaque couleur
  const uint8_t* data;  // couples (longueur, indice)
  size_t size;
};

// Police lissée : 4 bits de couverture par pixel.
struct HpGlyph {
  uint16_t code;
  uint8_t w, h;
  int8_t x, y;
  uint8_t advance;
  uint32_t offset;
};
struct HpFont {
  const HpGlyph* glyphs;  // triés par code
  uint16_t count;
  const uint8_t* bits;
  uint8_t height;
};
extern const HpFont hp_font_small, hp_font_medium, hp_font_large, hp_font_clock;  // clock : chiffres et « : »

// Fond d'une image : un disque (bord lissé) sur la couleur de l'écran. radius 0 : pas de disque.
struct HpDisc {
  int cx, cy, radius;
  uint16_t color, bg;
};

void drawRect(int x, int y, int w, int h, uint16_t color);
void drawFill(uint16_t color);
void drawRound(int x, int y, int w, int h, int radius, uint16_t color, uint16_t bg);
void drawDisc(const HpDisc& disc);
// Texte centré (UTF-8) ; les caractères absents de la police sont ignorés, le trop long est coupé.
void drawText(int y, const char* text, const HpFont& font, uint16_t fg, uint16_t bg);
int textWidth(const char* text, const HpFont& font);
// Le même, centré dans la colonne [x, x + w).
void drawTextIn(int x, int w, int y, const char* text, const HpFont& font, uint16_t fg, uint16_t bg);
void drawImage(int x, int y, const HpImage& image, const HpDisc& back);
