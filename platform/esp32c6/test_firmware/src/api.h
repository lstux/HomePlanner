// Application API (snapshot + push) protected by the household pairing key.
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "home/sync_journal.h"

#define HP_KEY_HEADER "X-HomePlanner-Key"

// Loads the key from NVS, creating it if needed. Call once Wi-Fi is started so
// that esp_random() has an entropy source.
void hpKeyLoad();
void hpKeyRenew();
const String& hpKey();

// journal may point to nullptr when storage is unavailable.
void hpApiRegister(WebServer& server, home::JournaledSyncCore*& journal);
// CORS headers shared by every API response.
void hpCors(WebServer& server);
