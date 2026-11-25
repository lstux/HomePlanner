import test from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { suggestRoutine, evaluate, formatDuration, dateKey } from "../routine.js";
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
