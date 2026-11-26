// Mode adulte : un garde-fou contre les manipulations d'enfant, pas une protection de sécurité.
const UNLOCK_MS = 5 * 60 * 1000;
let until = 0;

export function pinHash(pin) {
  let h = 0x811c9dc5;
  for (const c of "home:" + pin) h = Math.imul(h ^ c.charCodeAt(0), 0x01000193);
  return (h >>> 0).toString(16);
}
export const validPin = (pin) => /^\d{4,8}$/.test(pin);
export const lockNow = () => (until = 0);

const RESET_MS = 24 * 3600 * 1000;
// Un code par adulte ; « pin » est l'ancien code unique.
export const pinsOf = (config) => [...(config.pins || []), ...(config.pin ? [{ name: "Adulte", hash: config.pin }] : [])];
export const resetHoursLeft = (config, now = Date.now()) =>
  config.pinReset && pinsOf(config).length ? Math.max(1, Math.ceil((config.pinReset + RESET_MS - now) / 3600000)) : null;
// Applique un effacement demandé il y a plus de 24 h.
export function expireReset(config, now = Date.now()) {
  if (!config.pinReset || now < config.pinReset + RESET_MS) return false;
  delete config.pin; delete config.pins; delete config.pinReset;
  return true;
}

// true : autorisé ; "reset" : autorisé, les codes viennent d'être effacés ;
// "changed" : refusé, mais les réglages ont changé (demande d'effacement) ; false : refusé.
export function askAdult(config, ask = (m) => prompt(m), tell = (m) => alert(m), now = Date.now(), agree = (m) => confirm(m)) {
  if (expireReset(config, now)) return "reset";
  const pins = pinsOf(config);
  if (!pins.length || now < until) return true;
  const answer = ask("Réservé aux adultes. Code ? (tape « oubli » si tu l'as perdu)");
  if (answer === null) return false;
  if (answer.trim().toLowerCase() === "oubli") {
    const hours = resetHoursLeft(config, now);
    if (hours !== null) { tell(`Effacement déjà demandé : encore ${hours} h. Un autre adulte peut aussi te redonner un code dans les réglages.`); return false; }
    if (!agree("Un autre adulte peut te redonner un code dans les réglages avec le sien.\n\nSinon, effacer tous les codes dans 24 h ? (annulable depuis l'écran d'accueil)")) return false;
    config.pinReset = now;
    return "changed";
  }
  const hash = pinHash(answer.trim());
  if (!pins.some((p) => p.hash === hash)) { tell("Code incorrect."); return false; }
  until = now + UNLOCK_MS;
  return true;
}
