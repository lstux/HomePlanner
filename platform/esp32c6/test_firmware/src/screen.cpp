#include "screen.h"
#include <stdio.h>
#include <string.h>
#include "draw.h"
#include "gfx.h"
#include "images.h"

void hpScreenMessage(const char* title, const char* detail) {
  drawFill(C_BG);
  drawText(18, "HOMEPLANNER", 2, C_CYAN, C_BG);
  drawText(150, title, 1, C_WHITE, C_BG);
  if (detail) drawText(170, detail, 1, C_YELLOW, C_BG);
}

void hpScreenInfo(const HpStatus& s) {
  drawFill(C_BG);
  drawText(8, "HOMEPLANNER", 2, C_CYAN, C_BG);
  drawText(44, s.ap ? "WI-FI A CONFIGURER" : "WI-FI CONNECTE", 1, s.ap ? C_YELLOW : C_GREEN, C_BG);
  drawText(62, s.wifi, 1, C_WHITE, C_BG);
  drawText(86, s.ip, strlen(s.ip) > 13 ? 1 : 2, C_CYAN, C_BG);
  if (!s.ap) {
    char address[40];
    snprintf(address, sizeof(address), "%s.local", s.host);
    drawText(110, address, 1, C_CYAN, C_BG);
  }
  drawText(140, s.sd ? "CARTE SD : OK" : "CARTE SD : ABSENTE", 1, s.sd ? C_GREEN : C_ORANGE, C_BG);
  drawText(158, s.journal ? "JOURNAL : OK" : "JOURNAL : EN PANNE", 1, s.journal ? C_GREEN : C_ORANGE, C_BG);
  if (s.ap) {
    drawText(200, "REJOINS CE RESEAU,", 1, C_GREY, C_BG);
    drawText(220, "PUIS 192.168.4.1", 1, C_YELLOW, C_BG);
  } else {
    drawText(196, "CODE D'APPAIRAGE", 1, C_GREY, C_BG);
    drawText(216, s.key, 3, C_YELLOW, C_BG);
  }
}

void hpScreenPairing(const char* key) {
  drawFill(C_BG);
  drawText(18, "APPAIRAGE", 2, C_CYAN, C_BG);
  drawImage(11, 48, img_fox_content);
  drawText(212, "TAPE CE CODE :", 1, C_WHITE, C_BG);
  drawText(236, key, 3, C_YELLOW, C_BG);
}

void hpScreenIdle(int minute, int day, int month) {
  drawFill(C_BG);
  // Le renard et le lapin se relaient chaque minute.
  drawImage(11, 36, minute >= 0 && minute % 2 ? img_rabbit_content : img_fox_content);
  if (minute < 0) {
    drawText(224, "HOME", 4, C_WHITE, C_BG);
    return;
  }
  char text[16];
  snprintf(text, sizeof(text), "%02d:%02d", (minute / 60) % 24, minute % 60);
  drawText(214, text, 5, C_WHITE, C_BG);
  snprintf(text, sizeof(text), "%02d/%02d", day, month);
  drawText(272, text, 2, C_GREY, C_BG);
}
