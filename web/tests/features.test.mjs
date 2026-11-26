import test from "node:test";
import assert from "node:assert/strict";
import { askAdult, pinHash, lockNow, validPin } from "../lock.js";
import { describe, advice } from "../weather.js";
import { distanceKm, formatKm, whereIs } from "../map.js";
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
