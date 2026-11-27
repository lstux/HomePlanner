import test from "node:test";
import assert from "node:assert/strict";
import { activeMembers, nextRoutine, greeting } from "../moments.js";
import { defaultConfig } from "../data.js";

const config = defaultConfig(); // soir d'école : au lit à 20 h 30 ; matin : départ à 8 h 10
const at = (h, m, day = 27) => new Date(2026, 10, day, h, m); // 27/11/2026 : vendredi
const none = { done: {} };
const marks = (routine, values) => ({ done: { [`m1/${routine}`]: values } });

test("routine en cours d'une heure avant à 15 min après", () => {
  assert.deepEqual(activeMembers(config, none, at(7, 9)), []);
  assert.deepEqual(activeMembers(config, none, at(7, 10)), ["m1"]);
  assert.deepEqual(activeMembers(config, none, at(8, 25)), ["m1"]);
  assert.deepEqual(activeMembers(config, none, at(8, 26)), []);
  assert.deepEqual(activeMembers(config, none, at(23, 47, 26)), []); // jeudi soir, tard
});

test("première case cochée en avance ; terminée tôt, retour à l'accueil", () => {
  const now = at(17, 0, 26);
  assert.deepEqual(activeMembers(config, marks("school-evening", { devoirs: now.getTime() - 1000 }), now), ["m1"]);
  assert.deepEqual(activeMembers(config, marks("school-evening", { "_at:repas:19:15": now.getTime() }), now), []);
  const all = Object.fromEntries(config.routines.find((r) => r.id === "school-evening").steps.map((s) => [s.id, now.getTime() - 20 * 60000]));
  assert.deepEqual(activeMembers(config, marks("school-evening", all), now), []);
  assert.deepEqual(activeMembers(config, marks("school-evening", all), at(16, 50, 26)), ["m1"]);
});

test("prochaine routine", () => {
  assert.equal(nextRoutine(config, at(6, 0)).routine.id, "school-morning");
  assert.equal(nextRoutine(config, at(12, 0)).routine.id, "weekend-evening");
  const late = nextRoutine(config, at(23, 0, 26));
  assert.equal(late.routine.id, "school-morning");
  assert.equal(late.tomorrow, true);
});

test("la nuit commence après la routine du soir", () => {
  assert.equal(greeting(config, at(20, 40, 26)).night, false);
  assert.equal(greeting(config, at(20, 45, 26)).night, true);
  assert.equal(greeting(config, at(4, 0)).night, true);
  assert.equal(greeting(config, at(8, 0)).night, false);
});
