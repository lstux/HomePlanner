// Appairage avec la centrale : code de la maison, ou appui sur le bouton de la centrale. Sans DOM.
// request(chemin, corps) renvoie le JSON ou lève une erreur portant « status » (code HTTP).
export const WAIT_MS = 65000;   // un peu plus que la fenêtre de la centrale (60 s)
export const POLL_MS = 1500;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export const PAIRING_ERRORS = {
  403: "Code incorrect.",
  409: "La centrale a déjà trop d'appareils : déconnecte-en un depuis un autre téléphone (réglages).",
  410: "Délai dépassé : recommence.",
  429: "Trop d'essais : attends quelques minutes.",
};

// Renvoie { token } ou lève une erreur lisible.
// code : saisi par la personne (appareil avec écran) ; sinon on attend l'appui sur le bouton.
// onWaiting() : appelé quand la centrale attend l'appui. cancelled() : vrai si une autre tentative a pris le relais.
export async function pair(request, { name, code = "", onWaiting = () => {}, cancelled = () => false, wait = sleep, now = Date.now }) {
  const fail = (error) => { throw Object.assign(new Error(PAIRING_ERRORS[error.status] || error.message), { status: error.status }); };
  if (code) {
    try { return { token: (await request("/api/v1/pairing/claim", { code, name })).token }; } catch (error) { fail(error); }
  }
  let id;
  try { id = (await request("/api/v1/pairing/request", { name })).request_id; } catch (error) { fail(error); }
  onWaiting();
  const end = now() + WAIT_MS;
  while (now() < end) {
    await wait(POLL_MS);
    if (cancelled()) return null;
    let answer;
    try { answer = await request("/api/v1/pairing/claim", { request_id: id, name }); } catch (error) { fail(error); }
    if (answer.token) return { token: answer.token };
  }
  throw new Error(PAIRING_ERRORS[410]);
}

// Durée depuis la dernière requête d'un appareil, en français.
export function seenText(seconds) {
  if (seconds === null || seconds === undefined) return "pas vu depuis le redémarrage";
  if (seconds < 90) return "vu à l'instant";
  const minutes = Math.round(seconds / 60);
  if (minutes < 90) return `vu il y a ${minutes} min`;
  const hours = Math.round(minutes / 60);
  return hours < 48 ? `vu il y a ${hours} h` : `vu il y a ${Math.round(hours / 24)} j`;
}
