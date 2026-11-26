import test from "node:test";
import assert from "node:assert/strict";
import { makeBackup, readBackup, restore } from "../backup.js";
import { defaultConfig } from "../data.js";
import { emptyLists, upsert, remove, mergeItems, alive } from "../lists.js";

const sample = () => {
  const config = { ...defaultConfig(), pins: [{ name: "A", hash: "abc" }] };
  const lists = emptyLists();
  upsert(lists.contacts, "c1", { name: "Mamie" }, 1000);
  upsert(lists.shopping, "s1", { name: "Lait" }, 1000);
  upsert(lists.shopping, "s2", { name: "Pain" }, 1000);
  remove(lists.shopping, "s2", 2000);
  return { config, lists };
};

test("export sans codes adultes ni éléments supprimés", () => {
  const { config, lists } = sample();
  const b = makeBackup(config, lists);
  assert.equal(b.config.pins, undefined);
  assert.deepEqual(Object.keys(b.lists.shopping), ["s1"]);
  assert.deepEqual(readBackup(JSON.stringify(b)).config, b.config);
});

test("restauration : revient, l'emporte à la synchro, garde les ajouts et les codes", () => {
  const { config, lists } = sample();
  const backup = readBackup(JSON.stringify(makeBackup(config, lists)));
  remove(lists.contacts, "c1", 5000);
  const deleted = structuredClone(lists.contacts);
  upsert(lists.contacts, "c2", { name: "Papi" }, 6000);
  const next = restore({ ...config, holidays: true }, lists, backup, 4000);
  assert.equal(next.holidays, false);
  assert.deepEqual(next.pins, config.pins);
  assert.deepEqual(alive(lists.contacts).map((c) => c.name).sort(), ["Mamie", "Papi"]);
  assert.equal(mergeItems(deleted, lists.contacts).c1.name, "Mamie");
});

test("fichiers refusés", () => {
  assert.throws(() => readBackup("pas du json"), /pas une sauvegarde/);
  assert.throws(() => readBackup('{"a":1}'), /pas une sauvegarde/);
  assert.throws(() => readBackup('{"app":"homeplanner","format":2}'), /plus récente/);
  const b = makeBackup(defaultConfig(), emptyLists());
  b.config.routines[0].steps = [{}];
  assert.throws(() => readBackup(JSON.stringify(b)), /abîmée/);
});
