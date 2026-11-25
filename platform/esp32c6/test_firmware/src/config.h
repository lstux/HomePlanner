#pragma once
#include <Arduino.h>
#define HP_MAX_NETS 5
struct HpNetwork { String ssid; String password; };
struct HpConfig { HpNetwork networks[HP_MAX_NETS]; uint8_t count=0; String apSsid; String apPassword; String hostname; };
extern HpConfig hpConfig;
void hpConfigLoad(); void hpConfigSave(); void hpConfigClear(); void hpConfigAddNetwork(const String&,const String&);
