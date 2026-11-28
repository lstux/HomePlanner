import test from "node:test";
import assert from "node:assert/strict";
import { sortItems, applyOrder, nextOrder } from "../shopping.js";
import { upsert, alive } from "../lists.js";
import { barBackground, darken, FAMILY_COLOR } from "../events.js";

test("courses : l'ordre choisi est respecté, les cochés à la fin", () => {
  const list = {};
  upsert(list, "a", { name: "Pain", done: false, order: 2 });
  upsert(list, "b", { name: "Lait", done: true, order: 0 });
  upsert(list, "c", { name: "Œufs", done: false, order: 1 });
  upsert(list, "d", { name: "Ail", done: false });
  assert.deepEqual(sortItems(alive(list)).map((i) => i.id), ["c", "a", "d", "b"]);
  assert.equal(nextOrder(alive(list)), 3);
  applyOrder(list, ["a", "c", "d", "b"]);
  assert.deepEqual(sortItems(alive(list)).map((i) => i.id), ["a", "c", "d", "b"]);
});

test("agenda : les traits sont plus soutenus que les fonds", () => {
  assert.equal(darken("#ffffff", 0.5), "#808080");
  const people = [{ id: "x", color: "#ffd3a8" }, { id: "y", color: "#b9e0ff" }];
  assert.notEqual(barBackground({}, people), FAMILY_COLOR);
  assert.equal(barBackground({ who: ["x"] }, people), darken("#ffd3a8"));
  assert.match(barBackground({ who: ["x", "y"] }, people), /^linear-gradient/);
});
