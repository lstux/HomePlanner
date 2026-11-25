// Stockage local (hors ligne d'abord).
import { defaultConfig } from "./data.js";

const CONFIG = "home.config.v1";
const PROGRESS = "home.progress.v1";

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
  return p && p.date === today ? p : { date: today, done: {} };
}
export const saveProgress = (p) => write(PROGRESS, p);
