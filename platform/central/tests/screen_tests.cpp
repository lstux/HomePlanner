// Écran de la centrale sur ordinateur : états du bouton et rendu.
// Avec un dossier en argument, écrit aussi chaque écran en image PPM.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>

#include "draw.h"
#include "gfx.h"
#include "board_config.h"
#include "screen.h"
#include "screen_state.h"

static uint16_t frame[LCD_H][LCD_W];
void lcdBlit(int x, int y, int w, int h, const uint16_t* p) {
  assert(x >= 0 && y >= 0 && w > 0 && h > 0 && h <= HP_SLICE && x + w <= LCD_W && y + h <= LCD_H);
  for (int row = 0; row < h; ++row) memcpy(&frame[y + row][x], p + row * w, w * 2);
}

static std::string out;
static void save(const char* name) {
  if (out.empty()) return;
  FILE* f = fopen((out + "/" + name + ".ppm").c_str(), "wb");
  assert(f);
  fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H);
  for (auto& row : frame)
    for (uint16_t c : row) {
      const unsigned char rgb[3] = {static_cast<unsigned char>((c >> 11) << 3), static_cast<unsigned char>(((c >> 5) & 63) << 2), static_cast<unsigned char>((c & 31) << 3)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static void testState() {
  using namespace hpui;
  ScreenState s;
  assert(s.mode(false, 0) == IDLE);
  assert(s.mode(true, 0) == FAULT);
  s.release(10, 100);  // rebond
  assert(s.mode(false, 100) == IDLE);
  s.release(200, 1000);
  assert(s.mode(false, 1000) == INFO && s.mode(false, 1000 + INFO_MS - 1) == INFO);
  assert(s.mode(false, 1000 + INFO_MS) == IDLE);
  s.release(200, 50000);
  s.release(200, 51000);
  assert(s.mode(false, 51000) == IDLE);
  s.release(HOLD_MS, 60000);
  assert(s.mode(false, 60000) == OFF && s.mode(true, 60000) == OFF);
  s.askPairing(7, 61000);  // même écran éteint
  assert(s.mode(false, 61000) == PAIRING);
  s.authorized(8);  // un autre téléphone déjà relié se synchronise
  assert(s.mode(false, 62000) == PAIRING);
  s.authorized(7);
  assert(s.mode(false, 62000) == OFF);
  s.release(200, 63000);  // un appui rallume
  assert(s.mode(false, 63000) == IDLE);
  s.askPairing(7, 70000);
  assert(s.mode(false, 70000 + PAIRING_MS) == IDLE);
  s.release(CLEAR_MS, 200000);  // l'effacement du Wi-Fi n'éteint rien
  assert(s.mode(false, 200000) == IDLE);
  // écran tactile
  ScreenState t;
  t.tap(TAP_ELSEWHERE, 1000);
  assert(t.mode(false, 1000) == INFO);
  t.tap(TAP_CODE, 2000);
  assert(t.mode(false, 2000) == PAIRING && t.mode(false, 2000 + PAIRING_MS) == IDLE);
  t.tap(TAP_ELSEWHERE, 70000);
  t.tap(TAP_SCREEN_OFF, 71000);
  assert(t.mode(true, 71000) == OFF);
  t.tap(TAP_ELSEWHERE, 72000);  // un toucher rallume
  assert(t.mode(false, 72000) == IDLE);
  t.tap(TAP_CODE, 73000);
  t.tap(TAP_ELSEWHERE, 74000);  // et ferme le code
  assert(t.mode(false, 74000) == IDLE);
  // débordement de millis()
  s.release(200, 0xFFFFFF00u);
  assert(s.mode(false, 100) == INFO);
}

static void testLife() {
  using namespace hpui;
  const char* json =
      "{\"version\":1,\"holidays\":false,\"weather\":{\"name\":\"Ville\",\"lat\":48.11,\"lon\":-1.68},"
      "\"members\":[{\"id\":\"a\",\"name\":\"L\xC3\xA9o\"},{\"id\":\"b\",\"name\":\"Mia\"}],"
      "\"routines\":[{\"id\":\"m\",\"name\":\"Matin\",\"period\":\"morning\",\"days\":[5],\"deadline\":\"08:10\",\"deadlineLabel\":\"D\xC3\xA9part\","
      "\"steps\":[{\"id\":\"x\",\"label\":\"Lever\",\"minutes\":10},{\"id\":\"y\",\"label\":\"Dents\",\"minutes\":5}]}]}";
  hp::Config config;
  assert(hp::parseConfig(json, config));
  std::map<std::string, std::string> progress;
  Moment now;
  now.date = "2026-11-27";
  now.weekday = 5;
  now.ms = 1795762800000LL;
  now.minute = 6 * 60;  // trop tôt, rien de coché
  assert(routineViews(config, progress, now).empty());
  now.minute = 7 * 60 + 30;
  auto views = routineViews(config, progress, now);
  assert(views.size() == 2 && views[0].mood == BRAVO && views[0].target_minute == 8 * 60 + 10);
  now.minute = 7 * 60 + 52;  // 18 min pour 15 min d'étapes
  views = routineViews(config, progress, now);
  assert(views[0].mood == THINK && views[0].slack > 0);
  now.minute = 8 * 60;
  views = routineViews(config, progress, now);
  assert(views[0].mood == COMFORT && views[0].slack < 0);
  now.minute = 8 * 60 + 20;
  assert(routineViews(config, progress, now)[1].passed);
  now.minute = 8 * 60 + 26;
  assert(routineViews(config, progress, now).empty());
  // Léo a tout coché tôt : fêté un quart d'heure ; Mia n'a pas commencé.
  now.minute = 6 * 60;
  char done[160];
  snprintf(done, sizeof(done), "{\"date\":\"2026-11-27\",\"steps\":{\"x\":%lld,\"y\":%lld}}",
           static_cast<long long>(now.ms - 60000), static_cast<long long>(now.ms - 30000));
  progress["a/m"] = done;
  views = routineViews(config, progress, now);
  assert(views.size() == 1 && views[0].done && views[0].mood == BRAVO);
  now.ms += 20 * 60000;
  assert(routineViews(config, progress, now).empty());
  now.date = "2026-11-28";  // coches d'hier ignorées
  now.ms -= 20 * 60000;
  assert(routineViews(config, progress, now).empty());

  assert(gaugeShort(30) == "Pile poil" && gaugeShort(300) == "+5 min" && gaugeShort(-3900) == "-1 h 05");
  assert(skyOf(0) == SUN && skyOf(2) == PARTLY && skyOf(61) == RAIN && skyOf(75) == SNOW && skyOf(95) == STORM && skyOf(48) == FOG);
  double lat = 0, lon = 0;
  assert(weatherPlace(json, lat, lon) && lat > 48 && lon < 0);
  assert(!weatherPlace("{\"version\":1}", lat, lon));
  assert(forecastUrl(48.11, -1.68).find("latitude=48.11&longitude=-1.68&") != std::string::npos);
  Forecast f;
  assert(parseForecast("{\"daily\":{\"time\":[\"2026-11-27\"],\"weather_code\":[61],\"temperature_2m_max\":[9.4],\"temperature_2m_min\":[-2.6]}}", f));
  assert(f.known && f.sky == RAIN && f.tmax == 9 && f.tmin == -3);
  assert(!parseForecast("{}", f));

  size_t index = 9;
  assert(idlePage(0, 0, true, index) == PAGE_CLOCK && idlePage(CLOCK_MS, 0, true, index) == PAGE_WEATHER);
  assert(idlePage(CLOCK_MS, 0, false, index) == PAGE_CLOCK && idlePage(CLOCK_MS + WEATHER_MS, 0, true, index) == PAGE_CLOCK);
  assert(idlePage(MEMBER_MS, 2, true, index) == PAGE_ROUTINE && index == 1);

  RoutineView v;
  v.name = "L\xC3\xA9o";
  v.target = "D\xC3\xA9part";
  v.target_minute = 8 * 60 + 10;
  v.slack = 17 * 60;
  v.mood = BRAVO;
  hpScreenRoutine(7 * 60 + 30, v); save("routine-avance");
  v.slack = 2 * 60;
  v.mood = THINK;
  hpScreenRoutine(7 * 60 + 53, v); save("routine-juste");
  v.slack = -7 * 60;
  v.mood = COMFORT;
  v.target = "\xC3\x80 table";
  v.target_minute = 19 * 60;
  hpScreenRoutine(18 * 60 + 56, v); save("routine-retard");
  v.done = true;
  v.mood = BRAVO;
  hpScreenRoutine(8 * 60 + 1, v); save("routine-finie");
  for (int sky = SUN; sky <= STORM; ++sky) {
    f.sky = static_cast<Sky>(sky);
    hpScreenWeather(7 * 60 + 5, f);
    save(("meteo-" + std::to_string(sky)).c_str());
  }
}

// L'heure affichée, à partir de l'heure universelle donnée par le réseau.
static void expectLocal(time_t utc, int day, int hour, int minute) {
  struct tm t;
  assert(localtime_r(&utc, &t));
  assert(t.tm_mday == day && t.tm_hour == hour && t.tm_min == minute);
}
static void testTimezone() {
  setenv("TZ", hpui::TZ_FRANCE, 1);
  tzset();
  expectLocal(1795760100, 27, 7, 15);   // 27/11/2026 06:15 UTC : hiver, +1 h
  expectLocal(1782907200, 1, 14, 0);    // 01/07/2026 12:00 UTC : été, +2 h
  expectLocal(1774745999, 29, 1, 59);   // juste avant le passage à l'heure d'été
  expectLocal(1774746000, 29, 3, 0);
  expectLocal(1792889999, 25, 2, 59);   // juste avant le retour à l'heure d'hiver
  expectLocal(1792890000, 25, 2, 0);
}

int main(int argc, char** argv) {
  if (argc > 1) out = argv[1];
  testState();
  testLife();
  testTimezone();
  // Les accents ont leur dessin ; un caractère inconnu est ignoré.
  assert(textWidth("\xC3\x89t\xC3\xA9", hp_font_medium) > 0);
  assert(textWidth("a\xF0\x9F\x98\x80", hp_font_medium) == textWidth("a", hp_font_medium));
  assert(textWidth("00:00", hp_font_clock) == textWidth("19:47", hp_font_clock) && textWidth("00:00", hp_font_clock) <= 160);
  assert(!hpIsNight(-1) && hpIsNight(6 * 60 + 59) && !hpIsNight(7 * 60) && !hpIsNight(19 * 60 + 59) && hpIsNight(20 * 60));
  hpScreenIdle(19 * 60 + 5, 27, 11); save("veille-renard");
  hpScreenIdle(7 * 60 + 42, 28, 11); save("veille-lapin");
  assert(frame[0][0] == hpTheme().bg);
  hpScreenIdle(-1, 0, 0); save("veille-sans-heure");
  HpStatus s;
  s.sd = s.journal = true; s.ip = "192.168.1.42"; s.wifi = "Maison"; s.host = "homeplanner"; s.key = "ABCD2345";
  hpScreenInfo(s); save("infos");
  s.ap = true; s.sd = false; s.ip = "192.168.4.1"; s.wifi = "HomePlanner-1A2B";
  hpScreenInfo(s); save("configuration");
  hpScreenPairing("ABCD2345"); save("appairage");
  hpScreenMessage("Wi-Fi", "Connexion..."); save("demarrage");
  hpSetNight(true);
  hpScreenIdle(21 * 60 + 5, 27, 11); save("nuit-veille");
  hpScreenPairing("ABCD2345"); save("nuit-appairage");
  s.ap = false; s.sd = true; s.ip = "192.168.1.42"; s.wifi = "Maison";
  hpScreenInfo(s); save("nuit-infos");
  hpui::RoutineView v;
  v.name = "Mia"; v.target = "Au lit"; v.target_minute = 20 * 60 + 30; v.slack = 6 * 60; v.mood = hpui::BRAVO;
  hpScreenRoutine(20 * 60 + 12, v); save("nuit-routine");
  hpSetNight(false);
#if LCD_W > LCD_H
  assert(hpTapZone(hpui::INFO, false, 40, 210) == hpui::TAP_CODE);
  assert(hpTapZone(hpui::FAULT, false, 160, 235) == hpui::TAP_SCREEN_OFF);
  assert(hpTapZone(hpui::INFO, false, 300, 210) == hpui::TAP_ELSEWHERE);
  assert(hpTapZone(hpui::INFO, false, 40, 100) == hpui::TAP_ELSEWHERE);
  assert(hpTapZone(hpui::INFO, true, 40, 210) == hpui::TAP_ELSEWHERE);
  assert(hpTapZone(hpui::IDLE, false, 40, 210) == hpui::TAP_ELSEWHERE);
#else
  assert(hpTapZone(hpui::INFO, false, 40, 210) == hpui::TAP_ELSEWHERE);
#endif
  puts("screen tests OK (" HP_BOARD_ID ")");
  return 0;
}
