// Écrans de la centrale. Portable : dessine avec draw.h.
#pragma once
#include "life.h"

struct HpStatus {
  bool sd = false, journal = false, ap = false;
  const char* ip = "";
  const char* wifi = "";
  const char* host = "";
  const char* key = "";
};

void hpScreenMessage(const char* title, const char* detail = "");
void hpScreenInfo(const HpStatus& status);
void hpScreenPairing(const char* key);
// minute : minutes depuis minuit, -1 tant que l'heure est inconnue.
void hpScreenIdle(int minute, int day, int month);
void hpScreenRoutine(int minute, const hpui::RoutineView& view);
void hpScreenWeather(int minute, const hpui::Forecast& forecast);
