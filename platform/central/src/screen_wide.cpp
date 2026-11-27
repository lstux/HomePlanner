// Écrans en paysage (320x240) : mascotte à gauche, texte à droite ; boutons si l'écran est tactile.
#include "board_config.h"
#if LCD_W > LCD_H
#include "screen_common.h"
#include <string.h>

namespace {
constexpr int RIGHT = LCD_W / 2, HALF = LCD_W / 2;
constexpr int BUTTON_Y = 188, BUTTON_H = 46, BUTTON_W = 100, BUTTON_STEP = 105, BUTTON_X = 5;
void line(int y, const char* text, const HpFont& font, uint16_t fg) { drawText(y, text, font, fg, hpTheme().bg); }
void fit(int y, const char* text, uint16_t fg, const HpFont& largest = hp_font_large) { drawFitIn(0, LCD_W, y, text, fg, largest); }
void right(int y, const char* text, uint16_t fg, const HpFont& largest = hp_font_large) { drawFitIn(RIGHT, HALF, y, text, fg, largest); }
#if HP_HAS_TOUCH
void button(int index, const char* label) {
  const HpTheme& t = hpTheme();
  const int x = BUTTON_X + index * BUTTON_STEP;
  drawRound(x, BUTTON_Y, BUTTON_W, BUTTON_H, 14, t.button, t.bg);
  drawTextIn(x + 6, BUTTON_W - 12, BUTTON_Y + (BUTTON_H - hp_font_medium.height) / 2, label, hp_font_medium, t.text, t.button);
}
#endif
}  // namespace

void hpScreenMessage(const char* title, const char* detail) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  drawBadge(80, 120, 78, t.mascotDisc, img_fox_content);
  right(54, "HomePlanner", t.accent, hp_font_medium);
  right(104, title, t.text, hp_font_medium);
  if (detail) right(144, detail, t.muted, hp_font_small);
}

void hpScreenInfo(const HpStatus& s) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  line(2, "HomePlanner", hp_font_medium, t.accent);
  line(30, s.ap ? "Wi-Fi à configurer" : "Wi-Fi connecté", hp_font_small, s.ap ? t.warn : t.good);
  fit(49, s.wifi, t.text, hp_font_small);
  fit(68, s.ip, t.text, hp_font_medium);
  if (!s.ap) {
    char address[40];
    snprintf(address, sizeof(address), "%s.local", s.host);
    fit(93, address, t.muted, hp_font_small);
  }
  drawTextIn(0, HALF, 114, s.sd ? "Carte SD : OK" : "Carte SD : absente", hp_font_small, s.sd ? t.good : t.late, t.bg);
  drawTextIn(RIGHT, HALF, 114, s.journal ? "Journal : OK" : "Journal : en panne", hp_font_small, s.journal ? t.good : t.late, t.bg);
  if (s.ap) {
    line(146, "Rejoins ce réseau, puis ouvre", hp_font_small, t.muted);
    fit(172, "192.168.4.1", t.accent);
    return;
  }
  line(134, "Code d'appairage", hp_font_small, t.muted);
  fit(154, s.key, t.accent);
#if HP_HAS_TOUCH
  button(0, "Code");
  button(1, "Éteindre");
  button(2, "Retour");
#endif
}

hpui::Tap hpTapZone(hpui::Mode mode, bool ap, int x, int y) {
  // Les boutons n'existent que sur l'écran des infos, hors point d'accès ; marge autour pour les doigts.
  if (ap || (mode != hpui::INFO && mode != hpui::FAULT) || y < BUTTON_Y - 8) return hpui::TAP_ELSEWHERE;
  if (x < BUTTON_X + BUTTON_STEP) return hpui::TAP_CODE;
  if (x < BUTTON_X + 2 * BUTTON_STEP) return hpui::TAP_SCREEN_OFF;
  return hpui::TAP_ELSEWHERE;
}

void hpScreenPairing(const char* key) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  line(8, "Appairage", hp_font_large, t.accent);
  drawBadge(80, 134, 77, t.mascotDisc, img_fox_content);
  right(84, "Tape ce code :", t.text, hp_font_small);
  right(110, key, t.accent);
  right(160, "ou appuie", t.muted, hp_font_small);
  right(180, "sur BOOT", t.muted, hp_font_small);
}

void hpScreenIdle(int minute, int day, int month) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  // Le renard et le lapin se relaient chaque minute.
  drawBadge(80, 120, 78, t.mascotDisc, minute >= 0 && minute % 2 ? img_rabbit_content : img_fox_content);
  if (minute < 0) {
    right(102, "Home", t.text);
    return;
  }
  char text[16];
  clockText(text, sizeof(text), minute);
  drawTextIn(RIGHT, HALF, 76, text, hp_font_clock, t.text, t.bg);
  snprintf(text, sizeof(text), "%02d/%02d", day, month);
  right(136, text, t.muted, hp_font_medium);
}

void hpScreenRoutine(int minute, const hpui::RoutineView& view) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  fit(2, view.name.c_str(), t.accent, hp_font_medium);
  drawBadge(80, 104, 75, t.mascotDisc, mascot(minute % 2, view.mood));
  char text[16];
  clockText(text, sizeof(text), minute);
  drawTextIn(RIGHT, HALF, 52, text, hp_font_clock, t.text, t.bg);
  right(108, view.target.c_str(), t.muted, hp_font_small);
  clockText(text, sizeof(text), view.target_minute);
  right(132, text, t.muted);
  // Jauge : au milieu « pile poil », à droite l'avance, à gauche le retard (15 min au bord).
  const int percent = hp::gaugePercent(view.slack);
  const uint16_t tone = view.done || view.slack >= 0 ? t.good : view.slack > -5 * 60 ? t.warn : t.late;
  const std::string label = view.done ? "Fini !" : view.passed ? "C'est l'heure" : hpui::gaugeShort(view.slack);
  fit(180, label.c_str(), tone);
  constexpr int left = 20, width = 280, middle = left + width / 2, y = 219;
  drawRound(left, y, width, 14, 7, t.track, t.bg);
  const int length = percent * (width / 2) / 100;
  drawRect(length < 0 ? middle + length : middle, y, length < 0 ? -length : length, 14, tone);
  drawRect(middle - 1, y - 3, 3, 20, t.text);
}

void hpScreenWeather(int minute, const hpui::Forecast& forecast) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  line(8, "Aujourd'hui", hp_font_medium, t.accent);
  drawBadge(80, 134, 74, t.skyDisc, skyIcon(forecast.sky));
  char text[24];
  snprintf(text, sizeof(text), "%d° / %d°", forecast.tmin, forecast.tmax);
  right(84, text, t.text);
  clockText(text, sizeof(text), minute);
  drawTextIn(RIGHT, HALF, 136, text, hp_font_clock, t.muted, t.bg);
}
#endif
