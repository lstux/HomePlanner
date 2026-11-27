// Appareils appairés et fenêtre d'appairage. Portable (aucune dépendance Arduino) : testé sur ordinateur.
//
// Chaque appareil reçoit son propre jeton ; en retirer un coupe son accès sans toucher aux autres.
// Deux façons d'obtenir un jeton : saisir le code d'appairage de la maison (appareils avec écran),
// ou demander une fenêtre de 60 s et appuyer sur le bouton de la centrale.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <string>
#include <vector>

namespace hpair {

constexpr size_t MAX_DEVICES = 8, MAX_NAME = 24;
constexpr uint32_t WINDOW_MS = 60000, LOCK_MS = 5 * 60000;
constexpr int MAX_BAD_CODES = 5;

struct Device {
  std::string id, name, token;
  uint32_t seen = 0;  // millis() de la dernière requête, valable si seenKnown
  bool seenKnown = false;
};

enum Claim { CLAIM_OK, CLAIM_WAITING, CLAIM_EXPIRED, CLAIM_REFUSED, CLAIM_LOCKED, CLAIM_FULL };

// Génère une chaîne aléatoire de la longueur demandée.
using Random = std::function<std::string(size_t)>;

class Registry {
 public:
  // Nom lisible : sans caractères de contrôle, 24 octets au plus (coupé sur une limite de caractère).
  static std::string cleanName(const std::string& raw) {
    std::string name;
    for (char c : raw) name += static_cast<unsigned char>(c) < 32 || c == 127 ? ' ' : c;
    size_t start = name.find_first_not_of(' ');
    if (start == std::string::npos) return "Appareil";
    name = name.substr(start, name.find_last_not_of(' ') - start + 1);
    if (name.size() > MAX_NAME) {
      size_t cut = MAX_NAME;
      while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
      name.resize(cut);
    }
    return name;
  }

  // Stockage : une ligne « id<TAB>nom<TAB>jeton » par appareil.
  std::string dump() const {
    std::string out;
    for (const Device& d : devices_) out += d.id + "\t" + d.name + "\t" + d.token + "\n";
    return out;
  }
  void load(const std::string& text) {
    devices_.clear();
    size_t at = 0;
    while (at < text.size() && devices_.size() < MAX_DEVICES) {
      size_t end = text.find('\n', at);
      if (end == std::string::npos) end = text.size();
      const std::string line = text.substr(at, end - at);
      at = end + 1;
      const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
      if (b == std::string::npos) continue;
      Device d;
      d.id = line.substr(0, a);
      d.name = cleanName(line.substr(a + 1, b - a - 1));
      d.token = line.substr(b + 1);
      if (!d.id.empty() && d.token.size() >= 8) devices_.push_back(d);
    }
  }

  const std::vector<Device>& devices() const { return devices_; }
  void clear() { devices_.clear(); }

  // Jeton valide ? Note l'heure de la requête.
  const Device* authorize(const std::string& token, uint32_t now) {
    if (token.size() < 8) return nullptr;
    for (Device& d : devices_)
      if (d.token == token) {
        d.seen = now;
        d.seenKnown = true;
        return &d;
      }
    return nullptr;
  }
  bool revoke(const std::string& id) {
    for (size_t i = 0; i < devices_.size(); ++i)
      if (devices_[i].id == id) {
        devices_.erase(devices_.begin() + i);
        return true;
      }
    return false;
  }

  // Fenêtre d'appairage par bouton : une seule à la fois, la dernière demande remplace la précédente.
  void open(const std::string& request, uint32_t now) {
    request_ = request;
    since_ = now;
    approved_ = false;
    open_ = true;
  }
  bool windowOpen(uint32_t now) const { return open_ && now - since_ < WINDOW_MS; }
  // Appui sur le bouton de la centrale : vrai si une fenêtre était ouverte (l'appui est consommé).
  bool approve(uint32_t now) {
    if (!windowOpen(now)) return false;
    approved_ = true;
    return true;
  }

  Claim claimButton(const std::string& request, const std::string& name, uint32_t now, const Random& random, Device& out) {
    if (!open_ || request.empty() || request != request_) return CLAIM_EXPIRED;
    if (!windowOpen(now)) { open_ = false; return CLAIM_EXPIRED; }
    if (!approved_) return CLAIM_WAITING;
    return enroll(name, now, random, out);
  }
  // Code d'appairage de la maison (affiché sur l'écran) : 5 essais ratés bloquent 5 minutes.
  Claim claimCode(const std::string& code, const std::string& expected, const std::string& name, uint32_t now, const Random& random, Device& out) {
    if (locked_ && now - lockedAt_ < LOCK_MS) return CLAIM_LOCKED;
    locked_ = false;
    if (expected.empty() || code != expected) {
      if (++bad_ >= MAX_BAD_CODES) { locked_ = true; lockedAt_ = now; bad_ = 0; }
      return CLAIM_REFUSED;
    }
    bad_ = 0;
    return enroll(name, now, random, out);
  }

 private:
  Claim enroll(const std::string& name, uint32_t now, const Random& random, Device& out) {
    if (devices_.size() >= MAX_DEVICES) return CLAIM_FULL;
    Device d;
    do d.id = random(6); while (idTaken(d.id));
    d.name = cleanName(name);
    d.token = random(16);
    d.seen = now;
    d.seenKnown = true;
    devices_.push_back(d);
    open_ = false;
    out = d;
    return CLAIM_OK;
  }
  bool idTaken(const std::string& id) const {
    for (const Device& d : devices_) if (d.id == id) return true;
    return false;
  }

  std::vector<Device> devices_;
  std::string request_;
  uint32_t since_ = 0, lockedAt_ = 0;
  bool open_ = false, approved_ = false, locked_ = false;
  int bad_ = 0;
};

}  // namespace hpair
