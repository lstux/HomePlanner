// Météo du jour, demandée à Open-Meteo dans une tâche à part pour ne pas bloquer la centrale.
#pragma once
#include "life.h"

// À appeler régulièrement : relance toutes les heures (10 min après un échec).
void hpWeatherPoll(bool online, bool hasPlace, double lat, double lon);
const hpui::Forecast& hpWeather();
