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

// Retourne true si l'action adulte est autorisée (demande le code au besoin).
export function askAdult(config, ask = (m) => prompt(m), tell = (m) => alert(m), now = Date.now()) {
  if (!config.pin || now < until) return true;
  const answer = ask("Réservé aux adultes. Code ? (tape « oubli » si tu l'as perdu)");
  if (answer === null) return false;
  if (answer.trim().toLowerCase() === "oubli") {
    // Calcul à la portée d'un adulte, pas d'un petit.
    const a = 13 + Math.floor(Math.random() * 80), b = 13 + Math.floor(Math.random() * 80);
    if (Number(ask(`Pour effacer le code, combien font ${a} × ${b} ?`)) !== a * b) { tell("Ce n'est pas ça."); return false; }
    delete config.pin;
    return "reset";
  }
  if (pinHash(answer.trim()) !== config.pin) { tell("Code incorrect."); return false; }
  until = now + UNLOCK_MS;
  return true;
}
