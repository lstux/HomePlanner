import test from "node:test";
import assert from "node:assert/strict";
import { pair, seenText, WAIT_MS, POLL_MS } from "../pairing.js";

const httpError = (status) => Object.assign(new Error("http " + status), { status });

test("code de la maison : une seule requête", async () => {
  const calls = [];
  const result = await pair(async (path, body) => (calls.push([path, body]), { token: "JETON" }), { name: "Tel", code: "ABCD2345" });
  assert.deepEqual(result, { token: "JETON" });
  assert.deepEqual(calls, [["/api/v1/pairing/claim", { code: "ABCD2345", name: "Tel" }]]);
});

test("code refusé ou trop d'essais : messages lisibles", async () => {
  await assert.rejects(pair(async () => { throw httpError(403); }, { name: "T", code: "X" }), /Code incorrect/);
  await assert.rejects(pair(async () => { throw httpError(429); }, { name: "T", code: "X" }), /Trop d'essais/);
  await assert.rejects(pair(async () => { throw httpError(409); }, { name: "T", code: "X" }), /trop d'appareils/);
});

test("bouton : on attend l'appui puis on reçoit le jeton", async () => {
  let polls = 0, waiting = 0, clock = 0;
  const request = async (path, body) => {
    if (path.endsWith("/request")) return { request_id: "REQ" };
    assert.equal(body.request_id, "REQ");
    return ++polls < 3 ? { status: "waiting" } : { token: "JETON" };
  };
  const result = await pair(request, { name: "Tel", onWaiting: () => waiting++, wait: async (ms) => { assert.equal(ms, POLL_MS); clock += ms; }, now: () => clock });
  assert.deepEqual(result, { token: "JETON" });
  assert.equal(waiting, 1);
  assert.equal(polls, 3);
});

test("bouton : délai dépassé", async () => {
  let clock = 0;
  const request = async (path) => (path.endsWith("/request") ? { request_id: "R" } : { status: "waiting" });
  await assert.rejects(pair(request, { name: "T", wait: async (ms) => { clock += ms; }, now: () => clock }), /Délai dépassé/);
  assert.ok(clock >= WAIT_MS);
});

test("bouton : la centrale annonce l'expiration", async () => {
  const request = async (path) => { if (path.endsWith("/request")) return { request_id: "R" }; throw httpError(410); };
  await assert.rejects(pair(request, { name: "T", wait: async () => {} }), /Délai dépassé/);
});

test("une autre tentative prend le relais : on s'arrête sans erreur", async () => {
  let polls = 0;
  const request = async (path) => { if (!path.endsWith("/request")) polls++; return path.endsWith("/request") ? { request_id: "R" } : { status: "waiting" }; };
  assert.equal(await pair(request, { name: "T", cancelled: () => true, wait: async () => {} }), null);
  assert.equal(polls, 0);
});

test("dernière vue", () => {
  assert.equal(seenText(null), "pas vu depuis le redémarrage");
  assert.equal(seenText(10), "vu à l'instant");
  assert.equal(seenText(600), "vu il y a 10 min");
  assert.equal(seenText(3 * 3600), "vu il y a 3 h");
  assert.equal(seenText(3 * 86400), "vu il y a 3 j");
});
