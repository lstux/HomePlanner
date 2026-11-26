// Sauvegarde dans un fichier. Logique pure, testée avec Node.
// Ni les codes adultes, ni le code d'appairage, ni les cases du jour ne sont exportés.
import { LISTS, alive, emptyLists } from "./lists.js";
import { validConfig } from "./sync.js";

const FORMAT = 1;
export const MAX_FILE = 1024 * 1024;
const withoutPins = ({ pin, pins, pinReset, ...config }) => config;
const isObject = (v) => !!v && typeof v === "object" && !Array.isArray(v);
const text = (v) => typeof v === "string";

export const backupName = (date) => `home-sauvegarde-${date}.json`;
export function makeBackup(config, lists, now = new Date()) {
  return {
    app: "homeplanner", format: FORMAT, date: now.toISOString(),
    config: withoutPins(config),
    lists: Object.fromEntries(LISTS.map((n) => [n, Object.fromEntries(alive(lists[n] || {}).map(({ id, ...item }) => [id, item]))])),
  };
}

const usable = (c) => validConfig(c)
  && c.members.every((m) => isObject(m) && text(m.id) && text(m.name))
  && c.routines.every((r) => text(r.id) && text(r.name) && /^\d\d:\d\d$/.test(r.deadline)
    && r.steps.every((s) => isObject(s) && text(s.id) && text(s.label) && Number.isFinite(s.minutes)));

// Renvoie { date, config, lists } ou lève une erreur lisible.
export function readBackup(content) {
  let data;
  try { data = JSON.parse(content); } catch { throw new Error("Ce fichier n'est pas une sauvegarde Home."); }
  if (data?.app !== "homeplanner") throw new Error("Ce fichier n'est pas une sauvegarde Home.");
  if (data.format !== FORMAT) throw new Error("Sauvegarde d'une version plus récente : mets l'application à jour.");
  if (!usable(data.config) || !isObject(data.lists)) throw new Error("Sauvegarde abîmée ou incomplète.");
  const lists = emptyLists();
  for (const name of LISTS)
    for (const [id, item] of Object.entries(data.lists[name] || {}))
      if (id !== "__proto__" && isObject(item) && !item.x) lists[name][id] = item;
  return { date: String(data.date || ""), config: withoutPins(data.config), lists };
}

// Les réglages sont remplacés (codes adultes conservés). Les éléments du fichier reviennent et
// l'emportent sur les autres appareils ; ceux ajoutés depuis sont gardés.
export function restore(config, lists, backup, now = Date.now()) {
  for (const name of LISTS)
    for (const [id, item] of Object.entries(backup.lists[name]))
      lists[name][id] = { ...item, t: Math.max(now, (lists[name][id]?.t || 0) + 1) };
  const { pin, pins, pinReset } = config;
  return { ...structuredClone(backup.config), ...(pin && { pin }), ...(pins && { pins }), ...(pinReset && { pinReset }) };
}
