// Ce que raconte la veille : où en sont les routines, la météo du jour, et quoi montrer quand.
// Logique portable, testée sur ordinateur.
#pragma once
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "../../../twatch/homeplanner/hp_core.h"

namespace hpui {

constexpr int BEFORE_MIN = 60, AFTER_MIN = 15;
constexpr uint32_t MEMBER_MS = 10000, CLOCK_MS = 20000, WEATHER_MS = 5000;
// Heure française, règle POSIX : été du dernier dimanche de mars au dernier dimanche
// d'octobre (la « semaine 5 » veut dire la dernière).
constexpr const char* TZ_FRANCE = "CET-1CEST,M3.5.0,M10.5.0/3";

enum Mood { BRAVO, CONTENT, THINK, COMFORT };

struct RoutineView {
  std::string name;    // prénom
  std::string target;  // « Départ », « À table »...
  int target_minute = 0;
  int slack = 0;  // secondes d'avance
  Mood mood = CONTENT;
  bool done = false;
  bool passed = false;
};

struct Moment {
  std::string date;  // YYYY-MM-DD
  int weekday = 0;   // 0 = dimanche
  int minute = 0;
  int64_t ms = 0;  // depuis 1970
};

// progress : « membre/routine » -> contenu de l'entité de progression.
std::vector<RoutineView> routineViews(const hp::Config& config, const std::map<std::string, std::string>& progress,
                                      const Moment& now);

// Texte court de la jauge : « PILE POIL », « +5 MIN », « -1 H 05 ».
std::string gaugeShort(int slack_seconds);

enum Sky { SUN, PARTLY, CLOUD, FOG, RAIN, SNOW, STORM };
Sky skyOf(int weather_code);

struct Forecast {
  bool known = false;
  std::string date;
  Sky sky = CLOUD;
  int tmin = 0, tmax = 0;
};
// Ville choisie dans l'application (réglages partagés) ; false si aucune.
bool weatherPlace(const char* config_json, double& lat, double& lon);
std::string forecastUrl(double lat, double lon);
bool parseForecast(const char* json, Forecast& out);

enum Page { PAGE_CLOCK, PAGE_WEATHER, PAGE_ROUTINE };
// Routines en cours : les prénoms se relaient. Sinon l'heure, et la météo plus brièvement.
Page idlePage(uint32_t now_ms, size_t routines, bool weather, size_t& index);

}  // namespace hpui
