// Liaison avec la centrale : adresse, appairage (code ou bouton), appareils connectés et planification de la synchro.
import { syncOnce } from "./sync.js";
import { pair, seenText } from "./pairing.js";

const $ = (id) => document.getElementById(id);
const read = (k) => { try { return localStorage.getItem(k) || ""; } catch { return ""; } };
const keep = (k, v) => { try { localStorage.setItem(k, v); } catch {} };

let server = read("home.server");
// Jeton propre à cet appareil, remis par la centrale à l'appairage.
let key = read("home.key");
let deviceName = read("home.deviceName") || "Téléphone";

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
  if (response.status === 401) throw Object.assign(new Error("Appareil non appairé"), { status: 401 });
  if (response.status === 404) throw Object.assign(new Error("Firmware de la centrale à mettre à jour"), { status: 404 });
  if (!response.ok) throw Object.assign(new Error("Erreur de la centrale (" + response.status + ")"), { status: response.status });
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
        if (result.oldFirmware) status("Courses et contacts non partagés : firmware de la centrale à mettre à jour.", false);
        else if (result.tooBig.length) status("Une liste est trop longue pour la centrale : supprime des éléments.", false);
        else status(result.pending ? "Synchronisation en cours…" : "Synchronisé avec la maison", true);
        onChange(result);
        if (result.pending) again = true;
      } catch (error) {
        again = false;
        app.save();
        if (error.status === 401) { forget(); status("Cet appareil n'est plus appairé avec la centrale : réglages → Connecter. Données enregistrées sur cet appareil.", false); }
        else status(error.message + " · enregistré sur cet appareil", false);
      } finally {
        running = null;
        if (again) { again = false; soon(); }
      }
    })();
    return running;
  }
  function soon() { clearTimeout(timer); timer = setTimeout(sync, 800); }

  function forget() { key = ""; keep("home.key", ""); $("device-list").hidden = true; }

  let attempt = 0;
  // Code saisi (centrale avec écran) ou appui sur le bouton de la centrale.
  function startPairing() {
    const mine = ++attempt, code = $("pairing-key").value.trim().toUpperCase();
    status(code ? "Appairage…" : "Appairage en cours…", false);
    pair(request, {
      name: deviceName, code,
      onWaiting: () => status("Appuie sur le bouton BOOT de la centrale (ou saisis le code affiché sur son écran, puis touche « Connecter »).", false),
      cancelled: () => mine !== attempt,
    }).then((result) => {
      if (!result) return;
      key = result.token; keep("home.key", key); $("pairing-key").value = "";
      status("Appareil appairé. Ferme les réglages pour synchroniser.", true);
      devices();
    }).catch((error) => { if (mine === attempt) status(error.status === 404 ? "Firmware de la centrale à mettre à jour (version 0.10 ou plus)." : error.message, false); });
  }

  const text = (tag, content, className) => Object.assign(document.createElement(tag), { textContent: content, className: className || "" });
  // Liste des appareils connectés (les réglages sont réservés aux adultes).
  async function devices() {
    const box = $("device-list");
    if (!server || !key) { box.hidden = true; return; }
    try {
      const list = (await request("/api/v1/devices")).devices;
      $("devices").replaceChildren(...list.map((d) => {
        const row = text("div", "", "row");
        const label = text("span", "");
        label.append(text("strong", d.name), text("span", ` ${d.self ? "· cet appareil · " : "· "}${seenText(d.seen_s_ago)}`, "muted"));
        label.style.flex = "1";
        const button = text("button", "Déconnecter", "button secondary");
        button.type = "button";
        button.onclick = async () => {
          if (!confirm(d.self ? "Déconnecter cet appareil de la centrale ? Il faudra l'appairer de nouveau." : `Déconnecter « ${d.name} » ? Il faudra l'appairer de nouveau.`)) return;
          try {
            await request("/api/v1/devices/revoke", { id: d.id });
            if (d.self) { forget(); status("Cet appareil est déconnecté de la centrale.", false); } else devices();
          } catch (error) { status(error.message, false); }
        };
        row.append(label, button);
        return row;
      }));
      box.hidden = false;
    } catch (error) {
      box.hidden = true;
      if (error.status === 401) { forget(); status("Cet appareil n'est plus appairé avec la centrale : touche « Connecter ».", false); }
    }
  }

  $("server-url").value = server;
  $("device-name").value = deviceName;
  $("save-server").addEventListener("click", () => {
    let value = $("server-url").value.trim().replace(/\/+$/, "");
    if (value && !/^https?:\/\//i.test(value)) value = "http://" + value;
    try { if (value) new URL(value); } catch { status("Adresse invalide", false); return; }
    server = value; $("server-url").value = value;
    deviceName = $("device-name").value.trim().slice(0, 24) || "Téléphone"; $("device-name").value = deviceName;
    keep("home.server", server); keep("home.deviceName", deviceName);
    if (!server) { status("Non configurée", false); return; }
    // Un code saisi, ou pas encore appairé : on (re)lance l'appairage.
    if (!key || $("pairing-key").value.trim()) { startPairing(); return; }
    status("Connexion…", false);
    // Depuis les réglages, on ne remplace pas la saisie en cours : synchro à la fermeture.
    request("/api/v1/sync/snapshot").then(() => { status("Connexion réussie. Ferme les réglages pour synchroniser.", true); devices(); })
      .catch((error) => { if (error.status === 401) { forget(); startPairing(); } else status(error.message, false); });
  });
  window.addEventListener("online", soon);
  status(server && key ? "Connexion à la maison…" : "Non configurée", false);
  return { sync, soon, devices };
}
