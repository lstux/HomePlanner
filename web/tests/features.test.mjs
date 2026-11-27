import test from "node:test";
import assert from "node:assert/strict";
import { askAdult, pinHash, lockNow, validPin, resetHoursLeft, expireReset } from "../lock.js";
import { describe, advice } from "../weather.js";
import { distanceKm, formatKm, whereIs, nearbyPlace } from "../map.js";
import { parseAddresses } from "../geo.js";
import { BASES } from "../basemaps.js";
import { DEFAULT_BINS, holidays, holidayBefore, collections, reminder } from "../bins.js";
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

test("un code par adulte ; code oublié : effacement après 24 h", () => {
  lockNow();
  const config = { pins: [{ name: "A", hash: pinHash("2580") }, { name: "B", hash: pinHash("1357") }] };
  assert.equal(askAdult(config, () => "1357", () => {}, 1000), true);
  lockNow();
  const forgot = (agree, now) => askAdult(config, () => "oubli", () => {}, now, () => agree);
  assert.equal(forgot(false, 5000), false);
  assert.equal(config.pinReset, undefined);
  assert.equal(forgot(true, 5000), "changed");
  assert.equal(resetHoursLeft(config, 5000 + 3600000), 23);
  assert.equal(forgot(true, 9000), false); // déjà demandé
  assert.equal(expireReset(config, 5000 + 23 * 3600000), false);
  assert.equal(askAdult(config, () => assert.fail(), () => {}, 5000 + 24 * 3600000), "reset");
  assert.equal(config.pins, undefined);
  assert.equal(resetHoursLeft(config), null);
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
  assert.equal(whereIs({ homeId: "a", nowId: "b" }, places).id, "a");
  assert.equal(whereIs({ homeId: "gone" }, places), null);
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

test("poubelles : un jeudi sur deux pour le tri, chaque mardi pour le reste", () => {
  const list = collections(DEFAULT_BINS, {}, "2026-11-26", 20).map((c) => `${c.rule.id} ${c.date}`);
  assert.deepEqual(list, ["tri 2026-11-26", "reste 2026-12-01", "reste 2026-12-08", "tri 2026-12-10", "reste 2026-12-15"]);
});

test("poubelles : jours fériés, décalage et ajustements", () => {
  assert.equal(holidays(2027).get("2027-03-29"), "Lundi de Pâques");
  assert.equal(holidays(2027).get("2027-05-06"), "Ascension");
  assert.equal(holidayBefore("2027-03-30"), "Lundi de Pâques");
  assert.equal(holidayBefore("2026-11-12"), "11 novembre");
  assert.equal(holidayBefore("2026-12-24"), ""); // Noël tombe après
  const easterTuesday = (shift, o = {}) => collections({ ...DEFAULT_BINS, shift }, o, "2027-03-29", 3).find((c) => c.rule.id === "reste");
  assert.equal(easterTuesday(true).date, "2027-03-31");
  assert.equal(easterTuesday(false).date, "2027-03-30");
  const moved = { "reste.2027-03-30": { move: "2027-04-02", t: 1 } };
  assert.equal(easterTuesday(true, moved).date, "2027-04-02");
  assert.equal(easterTuesday(true, { "reste.2027-03-30": { skip: true, t: 1 } }).skipped, true);
});

test("poubelles : rappel de la veille au soir jusqu'à 16 h, tant que ce n'est pas coché", () => {
  const at = (d, h, o = {}) => reminder(DEFAULT_BINS, o, new Date(2026, 10, d, h));
  assert.equal(at(24, 16), null);
  assert.equal(at(25, 17), null);
  assert.equal(at(25, 18).when, "eve");
  assert.equal(at(25, 18).items[0].rule.id, "tri");
  assert.equal(at(26, 7).when, "day");
  assert.equal(at(26, 15).when, "day");
  assert.equal(at(26, 16), null);
  const out = { "tri.2026-11-26": { out: true, t: 1 } };
  assert.equal(at(25, 20, out), null);
  assert.equal(at(26, 7, out), null);
  assert.equal(at(30, 20).items[0].rule.id, "reste");
});
