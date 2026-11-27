// Appareils appairés et fenêtre d'appairage, sur ordinateur.
#include <assert.h>
#include <stdio.h>
#include "pairing.h"

using namespace hpair;

static int counter = 0;
static std::string makeRandom(size_t n) {
  std::string out;
  for (size_t i = 0; i < n; ++i) out += "ABCDEFGHJK"[(counter++ * 7 + i) % 10];
  out += std::to_string(counter);  // jamais deux fois pareil
  return out;
}

int main() {
  assert(Registry::cleanName("  Tel de Papa \n") == "Tel de Papa");
  assert(Registry::cleanName("\t\n") == "Appareil" && Registry::cleanName("") == "Appareil");
  assert(Registry::cleanName("a\tb") == "a b");
  const std::string long_name(40, 'x');
  assert(Registry::cleanName(long_name).size() == MAX_NAME);
  // coupe sur une limite de caractère : « é » = 2 octets, jamais coupé en deux
  assert(Registry::cleanName(std::string(23, 'a') + "\xC3\xA9\xC3\xA9") == std::string(23, 'a'));

  Registry r;
  Device d;
  // Par le bouton
  assert(r.claimButton("x", "Tel", 1000, makeRandom, d) == CLAIM_EXPIRED);  // pas de fenêtre
  r.open("req1", 1000);
  assert(r.windowOpen(1000) && r.windowOpen(1000 + WINDOW_MS - 1) && !r.windowOpen(1000 + WINDOW_MS));
  assert(r.claimButton("req1", "Tel", 2000, makeRandom, d) == CLAIM_WAITING);
  assert(r.claimButton("autre", "Tel", 2000, makeRandom, d) == CLAIM_EXPIRED);
  assert(r.approve(3000));
  assert(r.claimButton("req1", "Tel de Léa", 4000, makeRandom, d) == CLAIM_OK);
  assert(d.name == "Tel de Léa" && d.token.size() >= 16 && !d.id.empty());
  assert(!r.windowOpen(4000) && !r.approve(4001));  // la fenêtre se ferme, l'appui n'est plus consommé
  assert(r.claimButton("req1", "Tel", 4000, makeRandom, d) == CLAIM_EXPIRED);
  const std::string token = d.token, id = d.id;
  assert(r.authorize(token, 5000) && r.authorize(token, 5000)->seenKnown);
  assert(!r.authorize("", 5000) && !r.authorize("pas-un-jeton", 5000) && !r.authorize(token.substr(1), 5000));
  // fenêtre expirée avant l'appui
  r.open("req2", 10000);
  assert(!r.approve(10000 + WINDOW_MS));
  assert(r.claimButton("req2", "Tel", 10000 + WINDOW_MS, makeRandom, d) == CLAIM_EXPIRED);
  // une nouvelle demande remplace la précédente et annule l'appui déjà donné
  r.open("a", 20000);
  assert(r.approve(20001));
  r.open("b", 20002);
  assert(r.claimButton("a", "T", 20003, makeRandom, d) == CLAIM_EXPIRED && r.claimButton("b", "T", 20003, makeRandom, d) == CLAIM_WAITING);

  // Par le code de la maison
  Registry c;
  assert(c.claimCode("ABCD2345", "ABCD2345", "Tel", 0, makeRandom, d) == CLAIM_OK);
  for (int i = 0; i < MAX_BAD_CODES; ++i) assert(c.claimCode("FAUX", "ABCD2345", "T", 100 + i, makeRandom, d) == CLAIM_REFUSED);
  assert(c.claimCode("ABCD2345", "ABCD2345", "T", 200, makeRandom, d) == CLAIM_LOCKED);  // même le bon code, pendant le blocage
  assert(c.claimCode("ABCD2345", "ABCD2345", "T", 104 + LOCK_MS, makeRandom, d) == CLAIM_OK);
  assert(c.claimCode("", "", "T", 0, makeRandom, d) == CLAIM_REFUSED);  // pas de code défini : jamais accepté

  // Retrait
  assert(r.revoke(id) && !r.authorize(token, 6000) && !r.revoke(id));

  // Stockage
  Registry full;
  for (size_t i = 0; i < MAX_DEVICES; ++i) assert(full.claimCode("K", "K", "T" + std::to_string(i), i, makeRandom, d) == CLAIM_OK);
  assert(full.claimCode("K", "K", "T", 99, makeRandom, d) == CLAIM_FULL);
  Registry back;
  back.load(full.dump());
  assert(back.devices().size() == MAX_DEVICES && back.devices()[3].name == "T3" && back.devices()[3].token == full.devices()[3].token);
  assert(back.authorize(full.devices()[5].token, 0));
  back.load("sans tabulation\nid\tnom\tcourt\nid2\tnom\tjeton-assez-long\n");  // lignes abîmées ignorées
  assert(back.devices().size() == 1 && back.devices()[0].id == "id2");
  back.clear();
  assert(back.devices().empty() && back.dump().empty());
  puts("pairing tests OK");
  return 0;
}
