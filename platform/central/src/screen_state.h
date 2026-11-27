// Que montrer à l'écran. Logique portable, testée sur ordinateur.
#pragma once
#include <stdint.h>

namespace hpui {

constexpr uint32_t INFO_MS = 30000, PAIRING_MS = 60000;
// Sans activité depuis ce délai, l'écran s'éteint (hors routine et hors panne).
constexpr uint32_t SLEEP_MS = 15 * 60000;
constexpr uint32_t PRESS_MIN_MS = 30, HOLD_MS = 2000, CLEAR_MS = 5000;

enum Mode { OFF, IDLE, INFO, FAULT, PAIRING };
// Ce que l'on touche sur un écran tactile.
enum Tap { TAP_ELSEWHERE, TAP_CODE, TAP_SCREEN_OFF };

class ScreenState {
 public:
  // Relâchement du bouton BOOT après heldMs (l'effacement du Wi-Fi est géré avant, bouton enfoncé).
  void release(uint32_t heldMs, uint32_t now) {
    if (heldMs < PRESS_MIN_MS || heldMs >= CLEAR_MS) return;
    if (activity(now)) return;  // en veille : l'appui ne fait que rallumer
    if (heldMs >= HOLD_MS) { off_ = !off_; info_ = pairing_ = false; return; }
    if (off_) { off_ = false; return; }
    if (pairing_) { pairing_ = false; return; }
    info_ = !info_;
    since_ = now;
  }
  // Toucher l'écran : ferme le code, rallume, suit un bouton, sinon montre ou cache les infos.
  void tap(Tap what, uint32_t now) {
    if (activity(now)) return;
    if (pairing_) { pairing_ = false; return; }
    if (off_) { off_ = false; return; }
    if (what == TAP_CODE) { info_ = false; askPairing(0, now); return; }
    if (what == TAP_SCREEN_OFF) { off_ = true; info_ = false; return; }
    info_ = !info_;
    since_ = now;
  }
  // Quelque chose s'est passé (appui, donnée reçue, appairage) : sort de veille et relance le délai.
  // Rend vrai si l'écran était en veille. L'extinction demandée à la main n'est pas concernée.
  bool activity(uint32_t now) {
    const bool slept = asleep_;
    asleep_ = false;
    active_ = now;
    return slept;
  }
  // Un appareil demande le code ; il disparaît quand ce même appareil s'en est servi.
  void askPairing(uint32_t client, uint32_t now) { activity(now); pairing_ = true; client_ = client; pairingSince_ = now; }
  void authorized(uint32_t client) { if (pairing_ && client == client_) pairing_ = false; }
  // fault : point d'accès, carte SD ou journal en panne ; routine : une routine est en cours.
  // L'un comme l'autre garde l'écran allumé.
  Mode mode(bool fault, uint32_t now, bool routine = false) {
    if (info_ && now - since_ >= INFO_MS) info_ = false;
    if (pairing_ && now - pairingSince_ >= PAIRING_MS) pairing_ = false;
    if (pairing_) return PAIRING;
    if (off_) return OFF;
    // La veille reste acquise jusqu'à une activité (même après le débordement de millis()).
    if (fault || routine) asleep_ = false;
    else if (now - active_ >= SLEEP_MS) asleep_ = true;
    if (asleep_) return OFF;
    if (fault) return FAULT;
    return info_ ? INFO : IDLE;
  }

 private:
  bool off_ = false, info_ = false, pairing_ = false, asleep_ = false;
  uint32_t since_ = 0, pairingSince_ = 0, client_ = 0, active_ = 0;
};

}  // namespace hpui
