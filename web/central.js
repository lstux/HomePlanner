// Liaison avec la centrale C6 : adresse, code d'appairage et planification de la synchro.
import { syncOnce } from "./sync.js";

const $ = (id) => document.getElementById(id);
const read = (k) => { try { return localStorage.getItem(k) || ""; } catch { return ""; } };
const keep = (k, v) => { try { localStorage.setItem(k, v); } catch {} };

let server = read("home.server");
let key = read("home.key");

async function request(path, body) {
  const url = new URL(server + path);
  const local = ["localhost", "127.0.0.1"].includes(url.hostname);
  // Chrome : autorise l'appel HTTP vers le réseau local depuis une page HTTPS (après accord de l'utilisateur).
  const lan = location.protocol === "https:" && url.protocol === "http:" && !local ? { targetAddressSpace: "local" } : {};
  const headers = { "X-HomePlanner-Key": key, ...(body ? { "Content-Type": "application/json" } : {}) };
  let response;
  try {
    response = await fetch(url, { ...lan, headers, method: body ? "POST" : "GET", body: body && JSON.stringify(body), signal: AbortSignal.timeout(15000) });
  } catch { throw new Error("Centrale injoignable"); }
  if (response.status === 401) throw new Error("Code d'appairage refusé");
  if (response.status === 404) throw new Error("Firmware de la centrale à mettre à jour");
  if (!response.ok) throw new Error("Erreur de la centrale (" + response.status + ")");
  return response.json();
}

// app : voir syncOnce ; onChange(result) rafraîchit l'écran.
export function initCentral(app, onChange) {
  let running = null, again = false, timer;
  const status = (text, ok) => {
    for (const id of ["sync-state", "server-state"]) { $(id).textContent = text; $(id).dataset.ok = ok; }
    $("sync-state").hidden = !server || !key;
  };

  function sync() {
    if (!server || !key) { status("Non configurée", false); return Promise.resolve(); }
    if (running) { again = true; return running; }
    running = (async () => {
      try {
        const result = await syncOnce(app, request);
        status(result.pending ? "Synchronisation en cours…" : "Synchronisé avec la maison", true);
        onChange(result);
        if (result.pending) again = true;
      } catch (error) {
        again = false;
        app.save();
        status(error.message + " · enregistré sur cet appareil", false);
      } finally {
        running = null;
        if (again) { again = false; soon(); }
      }
    })();
    return running;
  }
  function soon() { clearTimeout(timer); timer = setTimeout(sync, 800); }

  $("server-url").value = server;
  $("pairing-key").value = key;
  $("save-server").addEventListener("click", () => {
    let value = $("server-url").value.trim().replace(/\/+$/, "");
    if (value && !/^https?:\/\//i.test(value)) value = "http://" + value;
    try { if (value) new URL(value); } catch { status("Adresse invalide", false); return; }
    server = value; $("server-url").value = value;
    key = $("pairing-key").value.trim().toUpperCase(); $("pairing-key").value = key;
    keep("home.server", server); keep("home.key", key);
    status("Connexion…", false);
    // Depuis les réglages, on ne remplace pas la saisie en cours : synchro à la fermeture.
    request("/api/v1/sync/snapshot").then(() => status("Connexion réussie. Ferme les réglages pour synchroniser.", true))
      .catch((error) => status(error.message, false));
  });
  window.addEventListener("online", soon);
  status(server && key ? "Connexion à la maison…" : "Non configurée", false);
  return { sync, soon };
}
