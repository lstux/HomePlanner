// Écran de la centrale sur ordinateur : états du bouton et rendu.
// Avec un dossier en argument, écrit aussi chaque écran en image PPM.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "draw.h"
#include "gfx.h"
#include "pins.h"
#include "screen.h"
#include "screen_state.h"

static uint16_t frame[LCD_H][LCD_W];
void lcdBlit(int x, int y, int w, int h, const uint16_t* p) {
  assert(x >= 0 && y >= 0 && w > 0 && h > 0 && h <= HP_SLICE && x + w <= LCD_W && y + h <= LCD_H);
  for (int row = 0; row < h; ++row) memcpy(&frame[y + row][x], p + row * w, w * 2);
}

static std::string out;
static void save(const char* name) {
  if (out.empty()) return;
  FILE* f = fopen((out + "/" + name + ".ppm").c_str(), "wb");
  assert(f);
  fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H);
  for (auto& row : frame)
    for (uint16_t c : row) {
      const unsigned char rgb[3] = {static_cast<unsigned char>((c >> 11) << 3), static_cast<unsigned char>(((c >> 5) & 63) << 2), static_cast<unsigned char>((c & 31) << 3)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static void testState() {
  using namespace hpui;
  ScreenState s;
  assert(s.mode(false, 0) == IDLE);
  assert(s.mode(true, 0) == FAULT);
  s.release(10, 100);  // rebond
  assert(s.mode(false, 100) == IDLE);
  s.release(200, 1000);
  assert(s.mode(false, 1000) == INFO && s.mode(false, 1000 + INFO_MS - 1) == INFO);
  assert(s.mode(false, 1000 + INFO_MS) == IDLE);
  s.release(200, 50000);
  s.release(200, 51000);
  assert(s.mode(false, 51000) == IDLE);
  s.release(HOLD_MS, 60000);
  assert(s.mode(false, 60000) == OFF && s.mode(true, 60000) == OFF);
  s.askPairing(7, 61000);  // même écran éteint
  assert(s.mode(false, 61000) == PAIRING);
  s.authorized(8);  // un autre téléphone déjà relié se synchronise
  assert(s.mode(false, 62000) == PAIRING);
  s.authorized(7);
  assert(s.mode(false, 62000) == OFF);
  s.release(200, 63000);  // un appui rallume
  assert(s.mode(false, 63000) == IDLE);
  s.askPairing(7, 70000);
  assert(s.mode(false, 70000 + PAIRING_MS) == IDLE);
  s.release(CLEAR_MS, 200000);  // l'effacement du Wi-Fi n'éteint rien
  assert(s.mode(false, 200000) == IDLE);
  // débordement de millis()
  s.release(200, 0xFFFFFF00u);
  assert(s.mode(false, 100) == INFO);
}

int main(int argc, char** argv) {
  if (argc > 1) out = argv[1];
  testState();
  assert(textWidth("ABC", 1) == 17 && textWidth("\xC3\x89t\xC3\xA9", 1) == 17);
  hpScreenIdle(19 * 60 + 5, 27, 11); save("veille-renard");
  hpScreenIdle(7 * 60 + 42, 28, 11); save("veille-lapin");
  assert(frame[0][0] == C_BG && frame[40][12] == C_BG);
  hpScreenIdle(-1, 0, 0); save("veille-sans-heure");
  HpStatus s;
  s.sd = s.journal = true; s.ip = "192.168.1.42"; s.wifi = "Maison"; s.host = "homeplanner"; s.key = "ABCD2345";
  hpScreenInfo(s); save("infos");
  s.ap = true; s.sd = false; s.ip = "192.168.4.1"; s.wifi = "HomePlanner-1A2B";
  hpScreenInfo(s); save("configuration");
  hpScreenPairing("ABCD2345"); save("appairage");
  hpScreenMessage("WIFI", "Connexion..."); save("demarrage");
  puts("screen tests OK");
  return 0;
}
