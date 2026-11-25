// Stockage local (hors ligne d'abord).
import { defaultConfig } from "./data.js";
import { emptyLists } from "./lists.js";

const CONFIG = "home.config.v1";
const PROGRESS = "home.progress.v1";
const META = "home.sync.v1";

function read(key) {
  try { return JSON.parse(localStorage.getItem(key)); } catch { return null; }
}
export function write(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); return true; } catch { return false; }
}
export function loadConfig() {
  const c = read(CONFIG);
  return c && c.version === 1 && c.routines?.length && c.members?.length ? c : defaultConfig();
}
export const saveConfig = (c) => write(CONFIG, c);

// La progression est remise à zéro chaque jour.
export function loadProgress(today) {
  const p = read(PROGRESS);
  if (!p || p.date !== today) return { date: today, done: {} };
  // Ancien format (date ISO par étape cochée).
  for (const marks of Object.values(p.done))
    for (const [id, v] of Object.entries(marks)) if (typeof v === "string") marks[id] = Date.parse(v) || 1;
  return p;
}
export const saveProgress = (p) => write(PROGRESS, p);
export const loadMeta = () => read(META) || { central: "", rev: 0, dirty: false };
export const saveMeta = (m) => write(META, m);
const LISTS_KEY = "home.lists.v1";
export const loadLists = () => ({ ...emptyLists(), ...(read(LISTS_KEY) || {}) });
export const saveLists = (l) => write(LISTS_KEY, l);
