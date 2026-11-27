#pragma once
#include <stdint.h>
#define RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED RGB(255,60,60)
#define C_GREEN RGB(60,230,90)
#define C_CYAN RGB(80,220,255)
#define C_YELLOW RGB(255,220,60)
#define C_ORANGE RGB(255,150,40)
#define C_GREY RGB(150,150,150)
#define C_BG RGB(6,8,24)
#define C_DARK RGB(45,52,80)
