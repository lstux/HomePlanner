// Liaison avec la centrale C6 (diagnostic et message partagé).
const $ = (id) => document.getElementById(id);
let server = "";
try { server = localStorage.getItem("home.server") || ""; } catch {}

function api(path, options = {}) {
  const url = new URL(server + path);
  const local = ["localhost", "127.0.0.1"].includes(url.hostname);
  const extra = {};
  // Chrome : autorise l'appel HTTP vers le réseau local depuis une page HTTPS (après accord de l'utilisateur).
  if (location.protocol === "https:" && url.protocol === "http:" && !local) extra.targetAddressSpace = "local";
  const headers = options.body ? { "Content-Type": "application/json" } : {};
  return fetch(url, { ...options, ...extra, headers, signal: AbortSignal.timeout(20000) });
}

async function refresh() {
  $("save-value").disabled = true;
  if (!server) { $("server-state").textContent = "Non configurée"; return; }
  $("server-state").textContent = "Vérification…";
  try {
    const response = await api("/api/v1/health");
    if (!response.ok) throw new Error("HTTP " + response.status);
    const data = await response.json();
    const healthy = !!data.ok && !!data.journal_healthy;
    $("server-state").textContent = healthy ? `En ligne · firmware ${data.firmware}` : "En ligne, stockage indisponible";
    if (!healthy) return;
    const v = await api("/api/v1/value");
    if (!v.ok) throw new Error("Lecture impossible");
    const value = await v.json();
    $("shared-value").value = value.value || "";
    $("value-status").textContent = "Révision " + (value.revision ?? 0);
    $("save-value").disabled = false;
  } catch (error) {
    $("server-state").textContent = "Inaccessible (" + error.message + ")";
  }
}

export function initCentral() {
  $("server-url").value = server;
  $("save-server").addEventListener("click", () => {
    let value = $("server-url").value.trim().replace(/\/+$/, "");
    if (value && !/^https?:\/\//i.test(value)) value = "http://" + value;
    server = value; $("server-url").value = value;
    try { localStorage.setItem("home.server", value); } catch {}
    refresh();
  });
  $("save-value").addEventListener("click", async () => {
    $("save-value").disabled = true;
    try {
      const response = await api("/api/v1/value", { method: "PUT", body: JSON.stringify({ value: $("shared-value").value }) });
      if (!response.ok) throw new Error("Écriture refusée (" + response.status + ")");
      $("value-status").textContent = "Enregistré · révision " + (await response.json()).revision;
    } catch (error) { $("value-status").textContent = error.message; }
    finally { $("save-value").disabled = false; }
  });
  return refresh;
}
