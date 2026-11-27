#include "screen.h"
#include <stdio.h>
#include <string.h>
#include "draw.h"
#include "gfx.h"
#include "pins.h"
#include "images.h"

namespace {
const HpImage& mascot(bool rabbit, hpui::Mood mood) {
  static const HpImage* const pictures[2][4] = {
      {&img_fox_bravo, &img_fox_content, &img_fox_think, &img_fox_comfort},
      {&img_rabbit_bravo, &img_rabbit_content, &img_rabbit_think, &img_rabbit_comfort}};
  return *pictures[rabbit][mood];
}
const HpImage& skyIcon(hpui::Sky sky) {
  static const HpImage* const pictures[] = {&img_sky_sun, &img_sky_partly, &img_sky_cloud, &img_sky_fog,
                                            &img_sky_rain, &img_sky_snow, &img_sky_storm};
  return *pictures[sky];
}
void clockText(char* out, size_t size, int minute) { snprintf(out, size, "%02d:%02d", (minute / 60) % 24, minute % 60); }
// En gros si ça tient, sinon en petit.
void drawFit(int y, const char* text, uint16_t fg) { drawText(y, text, textWidth(text, 2) <= LCD_W - 4 ? 2 : 1, fg, C_BG); }
}  // namespace

void hpScreenMessage(const char* title, const char* detail) {
  drawFill(C_BG);
  drawText(18, "HOMEPLANNER", 2, C_CYAN, C_BG);
  drawText(150, title, 1, C_WHITE, C_BG);
  if (detail) drawText(170, detail, 1, C_YELLOW, C_BG);
}

void hpScreenInfo(const HpStatus& s) {
  drawFill(C_BG);
  drawText(8, "HOMEPLANNER", 2, C_CYAN, C_BG);
  drawText(44, s.ap ? "WI-FI A CONFIGURER" : "WI-FI CONNECTE", 1, s.ap ? C_YELLOW : C_GREEN, C_BG);
  drawText(62, s.wifi, 1, C_WHITE, C_BG);
  drawText(86, s.ip, strlen(s.ip) > 13 ? 1 : 2, C_CYAN, C_BG);
  if (!s.ap) {
    char address[40];
    snprintf(address, sizeof(address), "%s.local", s.host);
    drawText(110, address, 1, C_CYAN, C_BG);
  }
  drawText(140, s.sd ? "CARTE SD : OK" : "CARTE SD : ABSENTE", 1, s.sd ? C_GREEN : C_ORANGE, C_BG);
  drawText(158, s.journal ? "JOURNAL : OK" : "JOURNAL : EN PANNE", 1, s.journal ? C_GREEN : C_ORANGE, C_BG);
  if (s.ap) {
    drawText(200, "REJOINS CE RESEAU,", 1, C_GREY, C_BG);
    drawText(220, "PUIS 192.168.4.1", 1, C_YELLOW, C_BG);
  } else {
    drawText(196, "CODE D'APPAIRAGE", 1, C_GREY, C_BG);
    drawText(216, s.key, 3, C_YELLOW, C_BG);
  }
}

void hpScreenPairing(const char* key) {
  drawFill(C_BG);
  drawText(18, "APPAIRAGE", 2, C_CYAN, C_BG);
  drawImage(11, 48, img_fox_content);
  drawText(212, "TAPE CE CODE :", 1, C_WHITE, C_BG);
  drawText(236, key, 3, C_YELLOW, C_BG);
}

void hpScreenIdle(int minute, int day, int month) {
  drawFill(C_BG);
  // Le renard et le lapin se relaient chaque minute.
  drawImage(11, 36, minute >= 0 && minute % 2 ? img_rabbit_content : img_fox_content);
  if (minute < 0) {
    drawText(224, "HOME", 4, C_WHITE, C_BG);
    return;
  }
  char text[16];
  snprintf(text, sizeof(text), "%02d:%02d", (minute / 60) % 24, minute % 60);
  drawText(214, text, 5, C_WHITE, C_BG);
  snprintf(text, sizeof(text), "%02d/%02d", day, month);
  drawText(272, text, 2, C_GREY, C_BG);
}

void hpScreenRoutine(int minute, const hpui::RoutineView& view) {
  drawFill(C_BG);
  drawFit(8, view.name.c_str(), C_CYAN);
  drawImage(11, 30, mascot(minute % 2, view.mood));
  // Jauge : au milieu « pile poil », à droite l'avance, à gauche le retard (15 min au bord).
  const int percent = hp::gaugePercent(view.slack);
  const uint16_t tone = view.done || view.slack >= 0 ? C_GREEN : view.slack > -5 * 60 ? C_YELLOW : C_ORANGE;
  constexpr int left = 11, width = 150, middle = left + width / 2, y = 188;
  drawRect(left, y, width, 12, C_DARK);
  const int length = percent * (width / 2) / 100;
  drawRect(length < 0 ? middle + length : middle, y, length < 0 ? -length : length, 12, tone);
  drawRect(middle - 1, y - 4, 2, 20, C_WHITE);
  const std::string label = view.done ? "FINI" : view.passed ? "C'EST L'HEURE" : hpui::gaugeShort(view.slack);
  drawFit(212, label.c_str(), tone);
  char text[16];
  clockText(text, sizeof(text), minute);
  drawText(240, text, 4, C_WHITE, C_BG);
  drawFit(282, view.target.c_str(), C_GREY);
  clockText(text, sizeof(text), view.target_minute);
  drawText(300, text, 2, C_GREY, C_BG);
}

void hpScreenWeather(int minute, const hpui::Forecast& forecast) {
  drawFill(C_BG);
  drawText(20, "AUJOURD'HUI", 2, C_CYAN, C_BG);
  drawImage((LCD_W - skyIcon(forecast.sky).w) / 2, 62, skyIcon(forecast.sky));
  char text[24];
  snprintf(text, sizeof(text), "%d* / %d*", forecast.tmin, forecast.tmax);
  drawText(204, text, textWidth(text, 3) <= LCD_W - 4 ? 3 : 2, C_WHITE, C_BG);
  clockText(text, sizeof(text), minute);
  drawText(266, text, 3, C_GREY, C_BG);
}
