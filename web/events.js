// Agenda : événements de la maison. Logique pure, testée avec Node.
// Élément : { title, date, end?, time?, who?: [ids], maybe?, yearly? } (dates « AAAA-MM-JJ »).
// Répartis sur plusieurs listes partagées : la centrale limite la taille de chacune.
import { addDays } from "./bins.js";
import { alive, remove } from "./lists.js";

export const EVENT_LISTS = Array.from({ length: 8 }, (_, i) => `events${i}`);
export const KEEP_PAST_DAYS = 400;

export const PALETTE = ["#ffd3a8", "#b9e0ff", "#c9efc0", "#f6c4e0", "#ddd0ff", "#fff0a6", "#ffc4bc", "#b8ece4", "#e0d2c0", "#d5dbe6"];
export const FAMILY_COLOR = "#b8c7bb";

// Toute la famille : les enfants (qui ont des routines) puis les adultes, chacun avec sa couleur.
export function persons(config) {
  return [...config.members, ...(Array.isArray(config.adults) ? config.adults : [])]
    .map((p, i) => ({ id: p.id, name: p.name, color: /^#[0-9a-f]{6}$/i.test(p.color) ? p.color : PALETTE[i % PALETTE.length] }));
}
// Fond d'un événement : la couleur de la personne ; à plusieurs, des bandes.
export function background(event, people) {
  const colors = (event.who || []).map((id) => people.find((p) => p.id === id)?.color).filter(Boolean);
  if (!colors.length) return FAMILY_COLOR;
  if (colors.length === 1) return colors[0];
  const part = 100 / colors.length;
  return `linear-gradient(115deg, ${colors.map((c, i) => `${c} ${Math.round(i * part)}% ${Math.round((i + 1) * part)}%`).join(", ")})`;
}

// Couleur des traits de côté : la teinte de la personne en plus soutenue, pour qu'elle ressorte sur le fond (week-end, aujourd'hui).
export function darken(hex, factor = 0.68) {
  const n = parseInt(hex.slice(1), 16);
  return "#" + [16, 8, 0].map((shift) => Math.round(((n >> shift) & 255) * factor).toString(16).padStart(2, "0")).join("");
}
export function barBackground(event, people) {
  const colors = (event.who || []).map((id) => people.find((p) => p.id === id)?.color).filter(Boolean).map((c) => darken(c));
  if (!colors.length) return darken(FAMILY_COLOR, 0.62);
  if (colors.length === 1) return colors[0];
  const part = 100 / colors.length;
  return `linear-gradient(180deg, ${colors.map((c, i) => `${c} ${Math.round(i * part)}% ${Math.round((i + 1) * part)}%`).join(", ")})`;
}

const isDate = (d) => typeof d === "string" && /^\d{4}-\d\d-\d\d$/.test(d) && !Number.isNaN(Date.parse(d + "T00:00:00Z")) && addDays(d, 0) === d;
export const allEvents = (lists) => EVENT_LISTS.flatMap((list) => alive(lists[list] || {}).filter((e) => isDate(e.date)).map((e) => ({ ...e, list })));

// La liste la moins remplie accueille un nouvel événement.
export function roomiest(lists) {
  const size = (name) => JSON.stringify(lists[name] || {}).length;
  return EVENT_LISTS.reduce((best, name) => (size(name) < size(best) ? name : best));
}

// Saisie → champs à enregistrer. Les champs vides sont remis à zéro pour effacer l'ancienne valeur.
export function cleanEvent(draft) {
  const title = String(draft.title || "").trim().slice(0, 60);
  if (!title) throw new Error("Donne un nom à l'événement.");
  if (!isDate(draft.date)) throw new Error("Choisis une date.");
  if (draft.end && (!isDate(draft.end) || draft.end < draft.date)) throw new Error("La fin est avant le début.");
  return {
    title, date: draft.date,
    end: draft.end && draft.end > draft.date ? draft.end : undefined,
    time: /^\d\d:\d\d$/.test(draft.time || "") ? draft.time : undefined,
    who: draft.who?.length ? [...draft.who] : undefined,
    maybe: draft.maybe ? true : undefined,
    yearly: draft.yearly ? true : undefined,
  };
}

export const daysBetween = (a, b) => Math.round((Date.parse(b + "T00:00:00Z") - Date.parse(a + "T00:00:00Z")) / 86400000);
// Même jour une autre année ; le 29 février retombe le 28.
function inYear(date, year) {
  const candidate = `${year}${date.slice(4)}`;
  return isDate(candidate) ? candidate : `${year}-02-28`;
}

// Ce qui tombe entre deux dates (incluses), trié : [{ event, start, end }].
export function occurrences(events, from, to) {
  const out = [];
  for (const event of events) {
    const length = event.end && event.end > event.date ? daysBetween(event.date, event.end) : 0;
    const starts = [];
    if (!event.yearly) starts.push(event.date);
    else for (let y = Number(from.slice(0, 4)) - 1; y <= Number(to.slice(0, 4)); y++)
      if (y >= Number(event.date.slice(0, 4))) starts.push(inYear(event.date, y));
    for (const start of starts) {
      const end = addDays(start, length);
      if (start <= to && end >= from) out.push({ event, start, end });
    }
  }
  return out.sort((a, b) => a.start.localeCompare(b.start) || (a.event.time || "").localeCompare(b.event.time || "")
    || a.event.title.localeCompare(b.event.title, "fr"));
}
export const onDay = (list, date) => list.filter((o) => o.start <= date && o.end >= date)
  .sort((a, b) => (a.event.time || "").localeCompare(b.event.time || ""));

const pad = (n) => String(n).padStart(2, "0");
export const localDate = (now) => `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`;

// Les 48 prochaines heures, jour par jour. Un rendez-vous d'aujourd'hui disparaît une heure après
// son début ; après-demain, seuls comptent les horaires atteints dans les 48 h.
export function upcoming(events, now) {
  const today = localDate(now), at = `${pad(now.getHours())}:${pad(now.getMinutes())}`;
  const hourAgo = now.getHours() ? `${pad(now.getHours() - 1)}:${pad(now.getMinutes())}` : "00:00";
  const list = occurrences(events, today, addDays(today, 2));
  return [0, 1, 2].map((n) => {
    const date = addDays(today, n);
    const items = onDay(list, date).filter(({ event, start, end }) => {
      if (!event.time || start !== end) return true;
      return n === 0 ? event.time >= hourAgo : n === 1 || event.time <= at;
    });
    return { date, items };
  }).filter((day) => day.items.length);
}

// Périodes affichées en traits verticaux : chacune reçoit une colonne libre.
export function lanes(list) {
  const ends = [], out = new Map();
  for (const o of list.filter((o) => o.end > o.start)) {
    let lane = ends.findIndex((end) => end < o.start);
    if (lane < 0) lane = ends.length;
    ends[lane] = o.end;
    out.set(o, lane);
  }
  return out;
}

// Le passé trop ancien est effacé pour garder de la place.
export function pruneEvents(lists, today) {
  const limit = addDays(today, -KEEP_PAST_DAYS);
  for (const e of allEvents(lists)) if (!e.yearly && (e.end || e.date) < limit) remove(lists[e.list], e.id);
}
