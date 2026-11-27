// Application API (snapshot + push) protected by the household pairing key.
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "home/sync_journal.h"

#define HP_KEY_HEADER "X-HomePlanner-Key"

// Loads the household pairing code and the paired devices from NVS, creating it if needed. Call once Wi-Fi is started so
// that esp_random() has an entropy source.
void hpKeyLoad();
void hpKeyRenew();
const String& hpKey();
// Bouton de la centrale : accepte la demande d'appairage en cours. Vrai si une fenêtre était ouverte.
bool hpPairingButton();
bool hpPairingOpen();
size_t hpDeviceCount();
void hpDevicesClear();

// journal may point to nullptr when storage is unavailable.
void hpApiRegister(WebServer& server, home::JournaledSyncCore*& journal);
// CORS headers shared by every API response.
void hpCors(WebServer& server);

// Fournis par main.cpp : affichage du code d'appairage.
void hpPairingRequested(uint32_t client);
void hpClientAuthorized(uint32_t client);
