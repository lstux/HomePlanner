#include "life.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ArduinoJson.h>

namespace hpui {
namespace {

hp::Marks marksOf(const std::string& payload, const std::string& date) {
  hp::Marks marks;
  DynamicJsonDocument doc(payload.size() * 3 + 1024);
  if (deserializeJson(doc, payload)) return marks;
  const char* day = doc["date"];
  if (!day || date != day) return marks;
  for (JsonPairConst pair : doc["steps"].as<JsonObjectConst>())
    if (pair.value().is<long long>()) marks[pair.key().c_str()] = pair.value().as<long long>();
  return marks;
}

}  // namespace

std::vector<RoutineView> routineViews(const hp::Config& config, const std::map<std::string, std::string>& progress,
                                      const Moment& now) {
  std::vector<RoutineView> views;
  if (!config.valid()) return views;
  const hp::Routine& routine = config.routines[hp::suggestRoutine(config, now.weekday, now.minute)];
  if (now.minute > routine.deadline + AFTER_MIN) return views;
  const bool window = now.minute >= routine.deadline - BEFORE_MIN;
  for (const hp::Member& member : config.members) {
    const auto found = progress.find(member.id + "/" + routine.id);
    const hp::Marks marks = found == progress.end() ? hp::Marks() : marksOf(found->second, now.date);
    // Dernière case cochée ou minuteur lancé (les décalages d'heure ne comptent pas).
    int64_t latest = 0;
    for (const auto& mark : marks)
      if (mark.first.compare(0, 4, "_at:") && mark.second > latest) latest = mark.second;
    if (!window && !latest) continue;
    const hp::Evaluation e = hp::evaluate(routine, marks, now.minute, now.ms);
    if (e.state == hp::STATE_EMPTY) continue;
    // Terminée bien avant l'heure : on la fête un quart d'heure, puis retour à la veille.
    if (e.state == hp::STATE_DONE && !window && now.ms - latest > AFTER_MIN * 60000LL) continue;
    RoutineView view;
    view.name = member.name;
    view.done = e.state == hp::STATE_DONE;
    view.passed = e.state == hp::STATE_PASSED;
    view.slack = e.slack;
    if (e.target >= 0 && !view.done) {
      view.target = routine.steps[e.target].label;
      view.target_minute = hp::stepTime(routine.steps[e.target], marks);
    } else {
      view.target = routine.deadline_label;
      view.target_minute = routine.deadline;
    }
    view.mood = view.done || e.state == hp::STATE_EARLY ? BRAVO
                : e.state == hp::STATE_ONTIME           ? CONTENT
                : e.state == hp::STATE_TIGHT            ? THINK
                                                        : COMFORT;
    views.push_back(view);
  }
  return views;
}

std::string gaugeShort(int slack_seconds) {
  const int minutes = abs(slack_seconds) / 60;
  if (!minutes) return "Pile poil";
  const char sign = slack_seconds > 0 ? '+' : '-';
  char text[24];
  if (minutes < 60) snprintf(text, sizeof(text), "%c%d min", sign, minutes);
  else snprintf(text, sizeof(text), "%c%d h %02d", sign, minutes / 60, minutes % 60);
  return text;
}

// Codes météo de l'OMM, comme dans l'application.
Sky skyOf(int code) {
  if (code <= 1) return SUN;
  if (code == 2) return PARTLY;
  if (code == 3) return CLOUD;
  if (code == 45 || code == 48) return FOG;
  if (code >= 95) return STORM;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return SNOW;
  if (code >= 51) return RAIN;
  return CLOUD;
}

bool weatherPlace(const char* config_json, double& lat, double& lon) {
  if (!config_json) return false;
  StaticJsonDocument<96> filter;
  filter["weather"] = true;
  DynamicJsonDocument doc(768);
  if (deserializeJson(doc, config_json, DeserializationOption::Filter(filter))) return false;
  JsonVariantConst place = doc["weather"];
  if (!place["lat"].is<double>() || !place["lon"].is<double>()) return false;
  lat = place["lat"].as<double>();
  lon = place["lon"].as<double>();
  return fabs(lat) <= 90 && fabs(lon) <= 180;
}

std::string forecastUrl(double lat, double lon) {
  char url[220];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1",
           lat, lon);
  return url;
}

bool parseForecast(const char* json, Forecast& out) {
  DynamicJsonDocument doc(3072);
  if (!json || deserializeJson(doc, json)) return false;
  JsonVariantConst daily = doc["daily"];
  const char* date = daily["time"][0];
  if (!date || !daily["weather_code"][0].is<int>() || !daily["temperature_2m_max"][0].is<double>() ||
      !daily["temperature_2m_min"][0].is<double>())
    return false;
  out.date = date;
  out.sky = skyOf(daily["weather_code"][0].as<int>());
  out.tmax = static_cast<int>(lround(daily["temperature_2m_max"][0].as<double>()));
  out.tmin = static_cast<int>(lround(daily["temperature_2m_min"][0].as<double>()));
  out.known = true;
  return true;
}

Page idlePage(uint32_t now_ms, size_t routines, bool weather, size_t& index) {
  index = 0;
  if (routines) {
    index = (now_ms / MEMBER_MS) % routines;
    return PAGE_ROUTINE;
  }
  return weather && now_ms % (CLOCK_MS + WEATHER_MS) >= CLOCK_MS ? PAGE_WEATHER : PAGE_CLOCK;
}

}  // namespace hpui
