import test from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { suggestRoutine, evaluate, formatDuration, formatClock, gauge, goKey, mark, dateKey } from "../routine.js";
import { defaultConfig } from "../data.js";
import { pickLine } from "../mascots.js";

const config = defaultConfig();
const school = config.routines[0];
const at = (h, m, day = 23) => new Date(2026, 10, day, h, m); // 23/11/2026 = lundi

test("routine proposée selon le jour, l'heure et les vacances", () => {
  assert.equal(suggestRoutine(config, at(7, 0)).id, "school-morning");
  assert.equal(suggestRoutine(config, at(7, 0, 25)).id, "weekend-morning");
  assert.equal(suggestRoutine(config, at(18, 0)).id, "school-evening");
  assert.equal(suggestRoutine(config, at(18, 0, 27)).id, "weekend-evening");
  assert.equal(suggestRoutine({ ...config, holidays: true }, at(8, 0)).id, "holidays");
});

test("avance, juste, retard, heure passée, terminé", () => {
  const state = (h, m, done = {}) => evaluate(school, done, at(h, m)).state;
  assert.equal(state(7, 0), "early");        // 40 min à faire, 70 disponibles
  assert.equal(state(7, 20), "ontime");
  assert.equal(state(7, 28), "tight");
  assert.equal(state(7, 45), "late");
  assert.equal(state(8, 15), "passed");
  const all = Object.fromEntries(school.steps.filter((s) => !s.optional).map((s) => [s.id, true]));
  assert.equal(state(7, 45, all), "done");   // les étapes facultatives ne bloquent pas
});

test("formats", () => {
  assert.equal(formatDuration(5.9), "5 min");
  assert.equal(formatDuration(65), "1 h 05");
  assert.equal(dateKey(at(0, 5)), "2026-11-23");
});

test("chaque réplique a son image, et le hors ligne la met en cache", () => {
  const sw = readFileSync(new URL("../sw.js", import.meta.url), "utf8");
  for (const s of ["welcome", "early", "ontime", "tight", "late", "passed", "checked", "unchecked", "done", "empty"])
    for (let i = 0; i < 3; i++) {
      const line = pickLine(s, { name: "A", next: "x", step: "y" }, i);
      assert.ok(existsSync(new URL("../" + line.image, import.meta.url)), line.image);
      assert.ok(!line.text.includes("{"));
    }
  for (const file of sw.match(/"\.\/[^"$]+\.[a-z]+"/g)) assert.ok(existsSync(new URL("../" + file.slice(1, -1), import.meta.url)), file);
});

test("minuteur d'étape : la marge tient, fond au dépassement, remonte si on finit tôt", () => {
  const [a, b] = school.steps.filter((s) => !s.optional);
  const t = (m, s = 0) => new Date(2026, 10, 23, 7, m, s).getTime();
  const run = (done, m, s) => evaluate(school, done, new Date(t(m, s)));
  assert.equal(run({}, 0).timer.started, false);
  assert.equal(run({}, 0).slack - run({}, 5).slack, 5); // pas parti : la marge diminue
  const go = { [goKey(a.id)]: t(0) };
  assert.equal(run(go, 0).timer.started, true);
  assert.equal(run(go, a.minutes).slack, run(go, 0).slack);
  assert.equal(run(go, a.minutes).losing, false);
  assert.equal(run(go, a.minutes + 3).slack, run(go, 0).slack - 3);
  assert.equal(run(go, a.minutes + 3).losing, true);
  const early = { ...go, [a.id]: t(1) };
  assert.equal(run(early, 1).slack, run(go, 0).slack + a.minutes - 1);
  assert.equal(run(early, 1).timer.step.id, b.id); // la suivante démarre à la validation
  assert.equal(run(early, 3).timer.elapsed, 2);
});

test("choisir une autre étape, recommencer, terminer", () => {
  const required = school.steps.filter((s) => !s.optional);
  const last = required.at(-1);
  const t = new Date(2026, 10, 23, 7, 10).getTime();
  const done = { [goKey(last.id)]: t };
  assert.equal(evaluate(school, done, new Date(t)).next.id, last.id);
  done[last.id] = t + 60000;
  assert.equal(evaluate(school, done, new Date(t + 60000)).next.id, required[0].id);
  for (const k in done) done[k] = mark(done[k], false, t + 120000);
  assert.equal(evaluate(school, done, new Date(t + 120000)).timer.started, false);
  const all = Object.fromEntries(required.map((s) => [s.id, t]));
  const end = evaluate(school, all, at(9, 0));
  assert.equal(end.state, "done");
  assert.equal(end.slack, evaluate(school, all, new Date(t)).slack); // figé
});

test("jauge pile poil et chrono", () => {
  assert.deepEqual(gauge(0.4), { position: 50 + (50 * 0.4) / 15, tone: "warn", label: "Pile poil" });
  assert.deepEqual(gauge(8.7), { position: 79, tone: "good", label: "8 min d'avance" });
  assert.equal(gauge(-3.2).label, "3 min de retard");
  assert.equal(gauge(-3.2).tone, "late");
  assert.equal(gauge(99).position, 100);
  assert.equal(gauge(-99).position, 0);
  assert.equal(formatClock(15), "15:00");
  assert.equal(formatClock(0.5), "0:30");
  assert.equal(formatClock(-2.25), "+2:15");
});
