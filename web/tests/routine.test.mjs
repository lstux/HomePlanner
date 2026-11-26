import test from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { suggestRoutine, evaluate, formatDuration, formatClock, gauge, goKey, mark, dateKey, stepTime, shiftStep } from "../routine.js";
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

const dinner = { deadline: "20:30", deadlineLabel: "Au lit", steps: [
  { id: "douche", minutes: 15 }, { id: "libre", minutes: 0, free: true },
  { id: "repas", label: "À table", minutes: 30, at: "19:00" }, { id: "dents", minutes: 5 },
] };
const evening = (h, m) => new Date(2026, 10, 23, h, m);

test("soir : la jauge vise le repas, le quartier libre prend le reste", () => {
  const t = (h, m) => evening(h, m).getTime();
  let e = evaluate(dinner, {}, evening(18, 30));
  assert.equal(e.target.time, "19:00");
  assert.equal(e.slack, 15);
  assert.equal(e.next.id, "douche");
  const done = { douche: t(18, 40) };
  e = evaluate(dinner, done, evening(18, 40));
  assert.equal(e.next.id, "libre");
  assert.equal(e.timer.free, true);
  assert.equal(e.timer.available, 20);
  e = evaluate(dinner, done, evening(18, 50));
  assert.equal(e.timer.available, 10);
  assert.equal(e.losing, false);
  e = evaluate(dinner, done, evening(19, 0)); // l'heure du repas
  assert.equal(e.next.id, "repas");
  assert.equal(e.target.time, "20:30");
  assert.equal(e.timer.started, true);
  assert.equal(evaluate(dinner, done, evening(19, 10)).timer.elapsed, 10);
  assert.equal(evaluate(dinner, done, evening(19, 10)).slack, 55);
});

test("soir : repas en retard sur les étapes, ou décalé à cause des pâtes", () => {
  let e = evaluate(dinner, {}, evening(19, 5));
  assert.equal(e.state, "passed");
  assert.equal(e.next.id, "douche");
  const done = { douche: evening(18, 40).getTime(), libre: evening(18, 55).getTime() };
  e = evaluate(dinner, done, evening(18, 55));
  assert.equal(e.next.id, "repas");
  assert.equal(e.timer.waiting, true);
  assert.equal(e.timer.wait, 5);
  shiftStep(dinner.steps[2], done, 15, evening(18, 56));
  assert.equal(stepTime(dinner.steps[2], done), "19:15");
  assert.equal(evaluate(dinner, done, evening(19, 5)).timer.wait, 10);
  shiftStep(dinner.steps[2], done, -5, evening(19, 5));
  assert.equal(stepTime(dinner.steps[2], done), "19:10");
  shiftStep(dinner.steps[2], done, 5, evening(19, 20)); // heure passée : depuis maintenant
  assert.equal(stepTime(dinner.steps[2], done), "19:25");
  done[goKey("repas")] = evening(19, 21).getTime(); // finalement c'est servi
  e = evaluate(dinner, done, evening(19, 22));
  assert.equal(e.timer.started, true);
  assert.equal(e.target.time, "20:30");
});
