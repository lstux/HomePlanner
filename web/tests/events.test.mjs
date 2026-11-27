import test from "node:test";
import assert from "node:assert/strict";
import { EVENT_LISTS, allEvents, background, cleanEvent, lanes, occurrences, onDay, persons, pruneEvents, roomiest, upcoming, FAMILY_COLOR } from "../events.js";
import { LISTS, emptyLists, upsert, mergeItems } from "../lists.js";
import { makeBackup, readBackup, restore } from "../backup.js";
import { defaultConfig } from "../data.js";

const titles = (list) => list.map((o) => o.event.title);

test("agenda : saisie nettoyée et contrôlée", () => {
  const e = cleanEvent({ title: "  Dentiste ", date: "2026-12-03", end: "2026-12-03", time: "13:30", who: [], maybe: false });
  assert.deepEqual(JSON.parse(JSON.stringify(e)), { title: "Dentiste", date: "2026-12-03", time: "13:30" });
  assert.throws(() => cleanEvent({ title: " ", date: "2026-12-03" }), /nom/);
  assert.throws(() => cleanEvent({ title: "x", date: "" }), /date/);
  assert.throws(() => cleanEvent({ title: "x", date: "2026-02-30" }), /date/);
  assert.throws(() => cleanEvent({ title: "x", date: "2026-12-03", end: "2026-12-01" }), /fin/);
});

test("agenda : périodes, anniversaires et 29 février", () => {
  const events = [
    { id: "a", title: "Vacances", date: "2026-12-19", end: "2027-01-03" },
    { id: "b", title: "Anniversaire", date: "2020-12-31", yearly: true },
    { id: "c", title: "Bissextile", date: "2024-02-29", yearly: true },
    { id: "d", title: "Pas encore né", date: "2030-12-31", yearly: true },
  ];
  assert.deepEqual(titles(occurrences(events, "2027-01-01", "2027-01-01")), ["Vacances"]);
  assert.deepEqual(titles(occurrences(events, "2026-12-31", "2026-12-31")), ["Vacances", "Anniversaire"]);
  assert.equal(occurrences(events, "2027-02-01", "2027-02-28")[0].start, "2027-02-28");
  assert.equal(occurrences(events, "2028-02-01", "2028-02-29")[0].start, "2028-02-29");
});

test("agenda : les 48 prochaines heures", () => {
  const events = [
    { title: "Passé ce matin", date: "2026-11-27", time: "08:00" },
    { title: "Il y a peu", date: "2026-11-27", time: "13:30" },
    { title: "Journée", date: "2026-11-27" },
    { title: "Demain", date: "2026-11-28", time: "09:00" },
    { title: "Après-demain tôt", date: "2026-11-29", time: "11:00" },
    { title: "Après-demain tard", date: "2026-11-29", time: "20:00" },
    { title: "Plus tard", date: "2026-11-30" },
  ];
  const days = upcoming(events, new Date(2026, 10, 27, 14, 0));
  assert.deepEqual(days.map((d) => titles(d.items)), [["Journée", "Il y a peu"], ["Demain"], ["Après-demain tôt"]]);
});

test("agenda : deux périodes qui se croisent ont chacune leur trait", () => {
  const list = occurrences([
    { title: "A", date: "2026-12-01", end: "2026-12-10" },
    { title: "B", date: "2026-12-05", end: "2026-12-06" },
    { title: "C", date: "2026-12-12", end: "2026-12-13" },
  ], "2026-12-01", "2026-12-31");
  assert.deepEqual([...lanes(list).values()], [0, 1, 0]);
  assert.equal(onDay(list, "2026-12-05").length, 2);
});

test("agenda : couleurs des personnes", () => {
  const config = { ...defaultConfig(), adults: [{ id: "p", name: "Papa", color: "#b9e0ff" }, { id: "q", name: "Maman", color: "rouge" }] };
  const people = persons(config);
  assert.equal(people.length, 3);
  assert.match(people[2].color, /^#/);
  assert.equal(background({ who: ["p"] }, people), "#b9e0ff");
  assert.equal(background({}, people), FAMILY_COLOR);
  assert.equal(background({ who: ["parti"] }, people), FAMILY_COLOR);
  assert.match(background({ who: ["m1", "p"] }, people), /^linear-gradient/);
});

test("agenda : listes partagées, sauvegarde et ménage", () => {
  const lists = emptyLists();
  assert.ok(EVENT_LISTS.every((n) => LISTS.includes(n)));
  for (let i = 0; i < 400; i++) upsert(lists[roomiest(lists)], `e${i}`, { title: "Un événement au titre assez long pour peser", date: "2026-12-01", who: ["m1"] });
  assert.equal(allEvents(lists).length, 400);
  for (const name of EVENT_LISTS) assert.ok(JSON.stringify({ items: lists[name] }).length < 12288, name);
  const backup = readBackup(JSON.stringify(makeBackup(defaultConfig(), lists)));
  const fresh = emptyLists();
  restore(defaultConfig(), fresh, backup);
  assert.equal(allEvents(fresh).length, 400);
  upsert(lists.events0, "old", { title: "Vieux", date: "2024-01-01" });
  upsert(lists.events0, "anniv", { title: "Anniversaire", date: "2001-05-05", yearly: true });
  pruneEvents(lists, "2026-11-27");
  assert.equal(allEvents(lists).length, 401);
  assert.equal(mergeItems(lists.events0, {}).old.x, 1);
});
