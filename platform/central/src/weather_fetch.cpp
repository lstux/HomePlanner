#include "weather_fetch.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

namespace {
constexpr uint32_t kRefreshMs = 3600000, kRetryMs = 600000;
hpui::Forecast current, fetched;
String url;
volatile bool busy = false, done = false;
bool succeeded = false, tried = false;
double placeLat = 0, placeLon = 0;
uint32_t last = 0;

void task(void*) {
  succeeded = false;
  {
    // Certificat non vérifié : la réponse ne sert qu'à choisir une icône.
    NetworkClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    if (http.begin(client, url)) {
      const int code = http.GET();
      if (code == 200) succeeded = hpui::parseForecast(http.getString().c_str(), fetched);
      else Serial.printf("[WEATHER] http %d\n", code);
      http.end();
    }
  }
  done = true;
  vTaskDelete(nullptr);
}
}  // namespace

void hpWeatherPoll(bool online, bool hasPlace, double lat, double lon) {
  if (busy) {
    if (!done) return;
    busy = false;
    if (succeeded && lat == placeLat && lon == placeLon) current = fetched;
    Serial.printf("[WEATHER] %s\n", succeeded ? "updated" : "unavailable");
  }
  if (!hasPlace) {
    current = hpui::Forecast();
    tried = false;
    return;
  }
  const bool moved = lat != placeLat || lon != placeLon;
  if (moved) current = hpui::Forecast();
  if (!online || (!moved && tried && millis() - last < (succeeded ? kRefreshMs : kRetryMs))) return;
  placeLat = lat;
  placeLon = lon;
  url = hpui::forecastUrl(lat, lon).c_str();
  tried = true;
  last = millis();
  done = false;
  busy = true;
  if (xTaskCreate(task, "weather", 10240, nullptr, 1, nullptr) != pdPASS) busy = false;
}

const hpui::Forecast& hpWeather() { return current; }
