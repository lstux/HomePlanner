// Écrans en paysage (320x240) : mascotte à gauche, texte à droite ; boutons si l'écran est tactile.
#include "board_config.h"
#if LCD_W > LCD_H
#include "screen_common.h"
#include <string.h>

namespace {
constexpr int RIGHT = LCD_W / 2, HALF = LCD_W / 2;
constexpr int BUTTON_Y = 188, BUTTON_H = 48, BUTTON_W = 100, BUTTON_STEP = 105, BUTTON_X = 5;
// En gros si ça tient dans la colonne, sinon en petit.
void drawFitIn(int x, int w, int y, const char* text, uint16_t fg) {
  drawTextIn(x, w, y, text, textWidth(text, 2) <= w - 4 ? 2 : 1, fg, C_BG);
}
void button(int index, const char* label) {
  const int x = BUTTON_X + index * BUTTON_STEP;
  drawRect(x, BUTTON_Y, BUTTON_W, BUTTON_H, C_DARK);
  drawTextIn(x, BUTTON_W, BUTTON_Y + (BUTTON_H - 14) / 2, label, 2, C_WHITE, C_DARK);
}
}  // namespace

void hpScreenMessage(const char* title, const char* detail) {
  drawFill(C_BG);
  drawText(40, "HOMEPLANNER", 3, C_CYAN, C_BG);
  drawText(120, title, 2, C_WHITE, C_BG);
  if (detail) drawText(150, detail, 1, C_YELLOW, C_BG);
}

void hpScreenInfo(const HpStatus& s) {
  drawFill(C_BG);
  drawText(4, "HOMEPLANNER", 2, C_CYAN, C_BG);
  drawText(28, s.ap ? "WI-FI A CONFIGURER" : "WI-FI CONNECTE", 1, s.ap ? C_YELLOW : C_GREEN, C_BG);
  drawText(42, s.wifi, 1, C_WHITE, C_BG);
  drawText(58, s.ip, 2, C_CYAN, C_BG);
  if (!s.ap) {
    char address[40];
    snprintf(address, sizeof(address), "%s.local", s.host);
    drawText(82, address, 1, C_CYAN, C_BG);
  }
  drawTextIn(0, HALF, 100, s.sd ? "CARTE SD : OK" : "CARTE SD : ABSENTE", 1, s.sd ? C_GREEN : C_ORANGE, C_BG);
  drawTextIn(RIGHT, HALF, 100, s.journal ? "JOURNAL : OK" : "JOURNAL : EN PANNE", 1, s.journal ? C_GREEN : C_ORANGE, C_BG);
  if (s.ap) {
    drawText(132, "REJOINS CE RESEAU,", 2, C_GREY, C_BG);
    drawText(156, "PUIS 192.168.4.1", 2, C_YELLOW, C_BG);
    return;
  }
  drawText(124, "CODE D'APPAIRAGE", 1, C_GREY, C_BG);
  drawText(142, s.key, 4, C_YELLOW, C_BG);
#if HP_HAS_TOUCH
  button(0, "CODE");
  button(1, "ETEINDRE");
  button(2, "RETOUR");
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
  drawFill(C_BG);
  drawText(10, "APPAIRAGE", 3, C_CYAN, C_BG);
  drawImage(5, 54, img_fox_content);
  drawTextIn(RIGHT, HALF, 90, "TAPE CE CODE :", 1, C_WHITE, C_BG);
  drawTextIn(RIGHT, HALF, 120, key, 3, C_YELLOW, C_BG);
}

void hpScreenIdle(int minute, int day, int month) {
  drawFill(C_BG);
  // Le renard et le lapin se relaient chaque minute.
  drawImage(5, 45, minute >= 0 && minute % 2 ? img_rabbit_content : img_fox_content);
  if (minute < 0) {
    drawTextIn(RIGHT, HALF, 100, "HOME", 5, C_WHITE, C_BG);
    return;
  }
  char text[16];
  clockText(text, sizeof(text), minute);
  drawTextIn(RIGHT, HALF, 80, text, 5, C_WHITE, C_BG);
  snprintf(text, sizeof(text), "%02d/%02d", day, month);
  drawTextIn(RIGHT, HALF, 140, text, 3, C_GREY, C_BG);
}

void hpScreenRoutine(int minute, const hpui::RoutineView& view) {
  drawFill(C_BG);
  drawFitIn(0, LCD_W, 6, view.name.c_str(), C_CYAN);
  drawImage(5, 28, mascot(minute % 2, view.mood));
  char text[16];
  clockText(text, sizeof(text), minute);
  drawTextIn(RIGHT, HALF, 54, text, 5, C_WHITE, C_BG);
  drawFitIn(RIGHT, HALF, 116, view.target.c_str(), C_GREY);
  clockText(text, sizeof(text), view.target_minute);
  drawTextIn(RIGHT, HALF, 140, text, 3, C_GREY, C_BG);
  // Jauge : au milieu « pile poil », à droite l'avance, à gauche le retard (15 min au bord).
  const int percent = hp::gaugePercent(view.slack);
  const uint16_t tone = view.done || view.slack >= 0 ? C_GREEN : view.slack > -5 * 60 ? C_YELLOW : C_ORANGE;
  const std::string label = view.done ? "FINI" : view.passed ? "C'EST L'HEURE" : hpui::gaugeShort(view.slack);
  drawText(184, label.c_str(), 3, tone, C_BG);
  constexpr int left = 20, width = 280, middle = left + width / 2, y = 216;
  drawRect(left, y, width, 14, C_DARK);
  const int length = percent * (width / 2) / 100;
  drawRect(length < 0 ? middle + length : middle, y, length < 0 ? -length : length, 14, tone);
  drawRect(middle - 1, y - 4, 3, 22, C_WHITE);
}

void hpScreenWeather(int minute, const hpui::Forecast& forecast) {
  drawFill(C_BG);
  drawText(10, "AUJOURD'HUI", 2, C_CYAN, C_BG);
  drawImage((HALF - skyIcon(forecast.sky).w) / 2, 60, skyIcon(forecast.sky));
  char text[24];
  snprintf(text, sizeof(text), "%d* / %d*", forecast.tmin, forecast.tmax);
  drawTextIn(RIGHT, HALF, 80, text, textWidth(text, 3) <= HALF - 4 ? 3 : 2, C_WHITE, C_BG);
  clockText(text, sizeof(text), minute);
  drawTextIn(RIGHT, HALF, 140, text, 4, C_GREY, C_BG);
}
#endif
