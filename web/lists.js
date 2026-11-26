// Collections partagées (courses, contacts, lieux). Logique pure, testée avec Node.
// Chaque élément porte t (dernière modification) ; x = 1 marque une suppression.
export const LISTS = ["shopping", "contacts", "places", "bins"];
export const KEEP_DELETED_MS = 30 * 24 * 3600 * 1000;

export const newId = () => Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
export const emptyLists = () => Object.fromEntries(LISTS.map((n) => [n, {}]));
const nextT = (previous, now) => Math.max(now, (previous?.t || 0) + 1);

export function upsert(list, id, fields, now = Date.now()) {
  const { x, ...previous } = list[id] || {};
  list[id] = { ...previous, ...fields, t: nextT(list[id], now) };
  return id;
}
export function remove(list, id, now = Date.now()) {
  if (list[id]) list[id] = { t: nextT(list[id], now), x: 1 }; // les données sont effacées
}
export const alive = (list) => Object.entries(list).filter(([, v]) => !v.x).map(([id, v]) => ({ ...v, id }));

// L'élément modifié le plus récemment gagne ; à égalité, choix identique sur tous les appareils.
export function mergeItems(a = {}, b = {}) {
  const out = { ...a };
  for (const [id, item] of Object.entries(b)) {
    if (!item || !Number.isFinite(item.t)) continue;
    const mine = out[id];
    if (!mine || item.t > mine.t || (item.t === mine.t && JSON.stringify(item) > JSON.stringify(mine))) out[id] = item;
  }
  return out;
}
export const prune = (list, now = Date.now()) =>
  Object.fromEntries(Object.entries(list).filter(([, v]) => !v.x || now - v.t < KEEP_DELETED_MS));
