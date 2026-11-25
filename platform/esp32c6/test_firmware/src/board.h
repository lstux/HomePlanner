#pragma once
#include <Arduino.h>
bool boardInit(); void lcdBacklight(uint8_t); void lcdFill(uint16_t); void lcdText(int,const char*,uint8_t,uint16_t,uint16_t=0);
bool sdMount(uint32_t khz); bool sdMounted(); const char* sdName();
