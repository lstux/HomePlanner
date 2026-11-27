// Couleurs de la centrale : un thème clair le jour, bleu nuit le soir.
#pragma once
#include <stdint.h>
#define RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))

struct HpTheme {
  uint16_t bg, text, muted, accent, good, warn, late, track, button, mascotDisc, skyDisc;
  uint8_t backlight;  // en %
};
constexpr HpTheme HP_DAY = {RGB(255,244,228), RGB(56,40,64), RGB(128,106,116), RGB(192,80,16), RGB(24,130,66), RGB(168,108,0),
                            RGB(200,52,36), RGB(236,216,196), RGB(255,212,164), RGB(255,216,176), RGB(164,208,248), 70};
constexpr HpTheme HP_NIGHT = {RGB(36,45,82), RGB(244,238,255), RGB(180,188,220), RGB(255,188,112), RGB(112,228,152), RGB(255,216,96),
                              RGB(255,148,116), RGB(70,80,128), RGB(84,98,156), RGB(128,144,200), RGB(96,114,176), 30};
// Nuit de 20 h à 7 h ; minute : depuis minuit, -1 tant que l'heure est inconnue (thème de jour).
constexpr int HP_NIGHT_FROM = 20 * 60, HP_NIGHT_TO = 7 * 60;
inline bool hpIsNight(int minute) { return minute >= 0 && (minute >= HP_NIGHT_FROM || minute < HP_NIGHT_TO); }
inline const HpTheme* hp_theme = &HP_DAY;
inline void hpSetNight(bool night) { hp_theme = night ? &HP_NIGHT : &HP_DAY; }
inline const HpTheme& hpTheme() { return *hp_theme; }
