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
  const uint8_t* data;  // couples (longueur, indice)
  size_t size;
};

void drawRect(int x, int y, int w, int h, uint16_t color);
void drawFill(uint16_t color);
// Texte centré. Majuscules, chiffres et . - : / _ ' + * (étoile : degré) ; les accents sont retirés.
void drawText(int y, const char* text, int scale, uint16_t fg, uint16_t bg);
int textWidth(const char* text, int scale);
// Le même, centré dans la colonne [x, x + w).
void drawTextIn(int x, int w, int y, const char* text, int scale, uint16_t fg, uint16_t bg);
void drawImage(int x, int y, const HpImage& image);
