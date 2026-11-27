// Ce que chaque carte fournit (boards/<carte>/board.cpp). L'affichage passe par lcdBlit (draw.h).
#pragma once
#include <Arduino.h>
#include "board_config.h"

void boardPower();               // à appeler en premier : maintient l'alimentation si la carte le demande
bool boardInit();                // écran (et tactile) prêts, écran noir
void lcdBacklight(uint8_t percent);
bool sdMount();                  // monte la microSD sur SD_MOUNT, sans jamais la formater
bool sdMounted();
#if HP_HAS_TOUCH
// true tant que l'écran est touché ; x, y en pixels de l'écran, raw* : valeurs brutes du contrôleur.
bool touchRead(int& x, int& y, int& rawA, int& rawB);
#endif
