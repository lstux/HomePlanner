// Écrans en portrait (écran plus haut que large).
#include "board_config.h"
#if LCD_H >= LCD_W
#include "screen_common.h"
#include <string.h>

namespace {
constexpr int MIDDLE = LCD_W / 2;
void line(int y, const char* text, const HpFont& font, uint16_t fg) { drawText(y, text, font, fg, hpTheme().bg); }
void fit(int y, const char* text, uint16_t fg, const HpFont& largest = hp_font_large) { drawFitIn(0, LCD_W, y, text, fg, largest); }
}  // namespace

void hpScreenMessage(const char* title, const char* detail) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  line(12, "HomePlanner", hp_font_medium, t.accent);
  drawBadge(MIDDLE, 128, 80, t.mascotDisc, img_fox_content);
  fit(222, title, t.text, hp_font_medium);
  if (detail) fit(256, detail, t.muted, hp_font_small);
}

void hpScreenInfo(const HpStatus& s) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  line(6, "HomePlanner", hp_font_medium, t.accent);
  line(40, s.ap ? "Wi-Fi à configurer" : "Wi-Fi connecté", hp_font_small, s.ap ? t.warn : t.good);
  fit(62, s.wifi, t.text, hp_font_small);
  fit(86, s.ip, t.text);
  if (!s.ap) {
    char address[40];
    snprintf(address, sizeof(address), "%s.local", s.host);
    fit(124, address, t.muted, hp_font_small);
  }
  line(150, s.sd ? "Carte SD : OK" : "Carte SD : absente", hp_font_small, s.sd ? t.good : t.late);
  line(172, s.journal ? "Journal : OK" : "Journal : en panne", hp_font_small, s.journal ? t.good : t.late);
  if (s.ap) {
    line(214, "Rejoins ce réseau,", hp_font_small, t.muted);
    line(238, "puis ouvre", hp_font_small, t.muted);
    fit(264, "192.168.4.1", t.accent);
  } else {
    line(214, "Code d'appairage", hp_font_small, t.muted);
    fit(240, s.key, t.accent);
  }
}

void hpScreenPairing(const char* key) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  line(10, "Appairage", hp_font_large, t.accent);
  drawBadge(MIDDLE, 134, 80, t.mascotDisc, img_fox_content);
  line(226, "Tape ce code :", hp_font_small, t.text);
  fit(250, key, t.accent);
  line(292, "ou appuie sur BOOT", hp_font_small, t.muted);
}

void hpScreenIdle(int minute, int day, int month) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  // Le renard et le lapin se relaient chaque minute.
  drawBadge(MIDDLE, 106, 83, t.mascotDisc, minute >= 0 && minute % 2 ? img_rabbit_content : img_fox_content);
  if (minute < 0) {
    line(224, "Home", hp_font_large, t.text);
    return;
  }
  char text[16];
  clockText(text, sizeof(text), minute);
  line(214, text, hp_font_clock, t.text);
  snprintf(text, sizeof(text), "%02d/%02d", day, month);
  line(272, text, hp_font_medium, t.muted);
}

void hpScreenRoutine(int minute, const hpui::RoutineView& view) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  fit(2, view.name.c_str(), t.accent);
  drawBadge(MIDDLE, 116, 77, t.mascotDisc, mascot(minute % 2, view.mood));
  // Jauge : au milieu « pile poil », à droite l'avance, à gauche le retard (15 min au bord).
  const int percent = hp::gaugePercent(view.slack);
  const uint16_t tone = view.done || view.slack >= 0 ? t.good : view.slack > -5 * 60 ? t.warn : t.late;
  constexpr int left = 11, width = 150, middle = left + width / 2, y = 201;
  drawRound(left, y, width, 12, 6, t.track, t.bg);
  const int length = percent * (width / 2) / 100;
  drawRect(length < 0 ? middle + length : middle, y, length < 0 ? -length : length, 12, tone);
  drawRect(middle - 1, y - 4, 3, 20, t.text);
  const std::string label = view.done ? "Fini !" : view.passed ? "C'est l'heure" : hpui::gaugeShort(view.slack);
  fit(218, label.c_str(), tone, hp_font_medium);
  char text[16];
  clockText(text, sizeof(text), minute);
  line(250, text, hp_font_clock, t.text);
  clockText(text, sizeof(text), view.target_minute);
  const std::string target = view.target + " " + text;
  fit(298, target.c_str(), t.muted, hp_font_small);
}

void hpScreenWeather(int minute, const hpui::Forecast& forecast) {
  const HpTheme& t = hpTheme();
  drawFill(t.bg);
  line(14, "Aujourd'hui", hp_font_medium, t.accent);
  drawBadge(MIDDLE, 122, 72, t.skyDisc, skyIcon(forecast.sky));
  char text[24];
  snprintf(text, sizeof(text), "%d° / %d°", forecast.tmin, forecast.tmax);
  fit(208, text, t.text);
  clockText(text, sizeof(text), minute);
  line(258, text, hp_font_clock, t.muted);
}

hpui::Tap hpTapZone(hpui::Mode, bool, int, int) { return hpui::TAP_ELSEWHERE; }
#endif
