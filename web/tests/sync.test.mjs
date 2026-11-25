import test from "node:test";
import assert from "node:assert/strict";
import { syncOnce, mergeMarks } from "../sync.js";
import { isDone, mark } from "../routine.js";
import { defaultConfig } from "../data.js";
import { emptyLists, upsert, remove, alive, prune, KEEP_DELETED_MS } from "../lists.js";

// Centrale simulée : mêmes règles de révision que le cœur C++.
function central() {
  const entities = new Map();
  const request = async (path, body) => {
    if (!body) return { central_id: "hp-test", entities: [...entities.values()].map((e) => ({ ...e })) };
    return { results: body.commands.map((c) => {
      const key = `${c.entity_type}/${c.entity_id}`, e = entities.get(key);
      const ok = c.action === "create" ? !e : e && e.revision === c.expected_revision;
      if (!ok) return { status: "conflict" };
      const revision = (e?.revision || 0) + 1;
      entities.set(key, { type: c.entity_type, id: c.entity_id, revision, payload: c.payload });
      return { status: "applied", entity_revision: revision };
    }) };
  };
  return { entities, request };
}
const device = (date = "2026-11-26") => ({ config: defaultConfig(), progress: { date, done: {} }, lists: emptyLists(),
  meta: { central: "", rev: 0, dirty: false }, canApplyConfig: () => true, save() {} });
const KEY = "m1/school-morning";

test("marques : la plus récente gagne, dans les deux sens", () => {
  assert.deepEqual(mergeMarks({ a: 10, b: -30 }, { a: -20, b: 25, c: 5 }), { a: -20, b: -30, c: 5 });
  assert.ok(mark(5000, false, 1000) < -5000, "une action locale prime même si l'horloge retarde");
  assert.equal(isDone({ a: -1 }, "a"), false);
});

test("le premier appareil publie les réglages, le second les reprend", async () => {
  const c = central(), a = device(), b = device();
  a.config.members[0].name = "Zoé";
  await syncOnce(a, c.request);
  const r = await syncOnce(b, c.request);
  assert.equal(b.config.members[0].name, "Zoé");
  assert.ok(r.configChanged && !r.replacedLocalConfig);
});

test("coches hors ligne sur deux appareils : fusion sans perte", async () => {
  const c = central(), a = device(), b = device();
  a.progress.done[KEY] = { lever: 100 };
  b.progress.done[KEY] = { dej: 200 };
  await syncOnce(a, c.request); await syncOnce(b, c.request); await syncOnce(a, c.request);
  assert.deepEqual(a.progress.done[KEY], { lever: 100, dej: 200 });
  a.progress.done[KEY].lever = -300; // correction d'un adulte
  await syncOnce(a, c.request); await syncOnce(b, c.request);
  assert.equal(b.progress.done[KEY].lever, -300);
});

test("réglages modifiés des deux côtés : la centrale gagne et on le signale", async () => {
  const c = central(), a = device(), b = device();
  await syncOnce(a, c.request); await syncOnce(b, c.request);
  a.config.routines[0].deadline = "08:00"; a.meta.dirty = true;
  b.config.routines[0].deadline = "08:20"; b.meta.dirty = true;
  await syncOnce(a, c.request);
  const r = await syncOnce(b, c.request);
  assert.ok(r.replacedLocalConfig);
  assert.equal(b.config.routines[0].deadline, "08:00");
});

test("la progression d'hier est remplacée, celle de demain est préservée", async () => {
  const c = central(), old = device("2026-11-25"), now = device("2026-11-26");
  old.progress.done[KEY] = { lever: 1 };
  await syncOnce(old, c.request); await syncOnce(now, c.request);
  assert.deepEqual(now.progress.done, {});
  now.progress.done[KEY] = { dej: 2 };
  await syncOnce(now, c.request); await syncOnce(old, c.request);
  assert.match(c.entities.get(`progress/${KEY}`).payload, /2026-11-26/);
});

test("conflit pendant l'envoi : résolu au cycle suivant", async () => {
  const c = central(), a = device(), b = device();
  await syncOnce(a, c.request);
  a.progress.done[KEY] = { lever: 1 }; b.progress.done[KEY] = { dej: 2 };
  let raced = false;
  const r = await syncOnce(a, async (path, body) => {
    if (body && !raced) { raced = true; await syncOnce(b, c.request); }
    return c.request(path, body);
  });
  assert.ok(r.pending);
  await syncOnce(a, c.request);
  assert.deepEqual(a.progress.done[KEY], { lever: 1, dej: 2 });
});

test("listes : ajouts des deux côtés fusionnés, suppression propagée", async () => {
  const c = central(), a = device(), b = device();
  upsert(a.lists.shopping, "lait", { name: "Lait" }, 1000);
  upsert(b.lists.shopping, "pain", { name: "Pain" }, 2000);
  await syncOnce(a, c.request); await syncOnce(b, c.request); await syncOnce(a, c.request);
  assert.equal(alive(a.lists.shopping).length, 2);
  remove(a.lists.shopping, "lait", Date.now());
  await syncOnce(a, c.request); await syncOnce(b, c.request);
  assert.deepEqual(alive(b.lists.shopping).map((i) => i.id), ["pain"]);
  assert.equal(b.lists.shopping.lait.name, undefined, "les données supprimées sont effacées");
});

test("listes : les vieilles suppressions sont purgées", () => {
  const list = { a: { t: 1, x: 1 }, b: { t: 1, name: "x" } };
  assert.deepEqual(Object.keys(prune(list, KEEP_DELETED_MS + 10)), ["b"]);
});

test("ancien firmware : routines synchronisées, listes signalées", async () => {
  const c = central(), a = device();
  upsert(a.lists.shopping, "lait", { name: "Lait" });
  a.progress.done[KEY] = { lever: 100 };
  const r = await syncOnce(a, async (path, body) => {
    if (body?.commands[0].entity_type === "list") throw Object.assign(new Error("refus"), { status: 400 });
    return c.request(path, body);
  });
  assert.ok(r.oldFirmware);
  assert.ok(c.entities.has("progress/" + KEY));
});
