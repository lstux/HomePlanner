import test from "node:test";
import assert from "node:assert/strict";
import { askAdult, pinHash, lockNow, validPin } from "../lock.js";
import { describe, advice } from "../weather.js";
import { distanceKm, formatKm, whereIs, nearbyPlace } from "../map.js";
import { parseAddresses } from "../geo.js";
import { BASES } from "../basemaps.js";
import { telLink } from "../contacts.js";

test("mode adulte : code demandé, puis déverrouillé quelques minutes", () => {
  lockNow();
  const config = { pin: pinHash("2580") };
  assert.ok(validPin("2580") && !validPin("12") && !validPin("abcd"));
  assert.equal(askAdult(config, () => "1111", () => {}, 1000), false);
  assert.equal(askAdult(config, () => null, () => {}, 1000), false);
  assert.equal(askAdult(config, () => "2580", () => {}, 1000), true);
  assert.equal(askAdult(config, () => assert.fail("déjà déverrouillé"), () => {}, 2000), true);
  assert.equal(askAdult(config, () => null, () => {}, 1000 + 6 * 60000), false);
});

test("code oublié : effacé seulement avec le bon calcul", () => {
  lockNow();
  const config = { pin: pinHash("2580") };
  assert.equal(askAdult(config, (m) => (m.includes("×") ? "1" : "oubli"), () => {}), false);
  const solve = (m) => { const n = m.match(/(\d+) × (\d+)/); return n ? String(n[1] * n[2]) : "oubli"; };
  assert.equal(askAdult(config, solve, () => {}), "reset");
  assert.equal(config.pin, undefined);
});

test("météo : description et conseil", () => {
  assert.equal(describe(0)[1], "Grand soleil");
  assert.match(advice({ code: 61, min: 8, max: 12, rain: 90 }).text, /pluie/);
  assert.match(advice({ code: 0, min: 18, max: 30, rain: 0 }).text, /boire/);
  assert.match(advice({ code: 3, min: 1, max: 5, rain: 10 }).text, /froid/);
});

test("carte : distance, position d'un proche, lien d'appel", () => {
  const d = distanceKm({ lat: 48.8566, lon: 2.3522 }, { lat: 45.764, lon: 4.8357 });
  assert.ok(d > 380 && d < 400);
  assert.equal(formatKm(0.42), "420 m");
  assert.equal(formatKm(2.34), "2,3 km");
  const places = [{ id: "a" }, { id: "b" }];
  assert.equal(whereIs({ homeId: "a", nowId: "b" }, places).id, "b");
  assert.equal(whereIs({ homeId: "a", nowId: "gone" }, places).id, "a");
  assert.equal(telLink("06 12.34-56 78"), "tel:0612345678");
});

test("adresses : lecture de la réponse, lieu voisin réutilisé", () => {
  const f = (label, score, lat = 48.1, lon = -1.6) => ({ geometry: { coordinates: [lon, lat] }, properties: { label, score } });
  assert.deepEqual(parseAddresses({ features: [f("12 Rue des Lilas 35000 Rennes", 0.9), f("Autre", 0.2), { properties: {} }] }),
    [{ label: "12 Rue des Lilas 35000 Rennes", lat: 48.1, lon: -1.6 }]);
  assert.deepEqual(parseAddresses(null), []);
  const places = [{ id: "a", lat: 48.1, lon: -1.6 }];
  assert.equal(nearbyPlace(places, { lat: 48.1002, lon: -1.6 }).id, "a");
  assert.equal(nearbyPlace(places, { lat: 48.101, lon: -1.6 }), null);
});

test("fonds de carte : adresses complètes et sources citées", () => {
  for (const base of Object.values(BASES)) {
    for (const part of ["https://", "{z}", "{x}", "{y}"]) assert.ok(base.url.includes(part));
    assert.ok(base.options.attribution);
  }
});
