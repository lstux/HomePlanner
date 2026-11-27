#pragma once
#include <Arduino.h>
bool boardInit(); void lcdBacklight(uint8_t);
bool sdMount(uint32_t khz); bool sdMounted(); const char* sdName();
