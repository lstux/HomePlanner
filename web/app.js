const $ = (id) => document.getElementById(id);
const state = { server: localStorage.getItem("home.server") || "", healthy: false };
$("server-url").value = state.server;
function setConnection(kind, label) {
  const el = $("connection"); el.className = "status " + (kind === "online" ? "status-online" : kind === "offline" ? "status-offline" : "status-unknown"); el.textContent = label;
}
function api(path, options = {}) {
  if (!state.server) throw new Error("Configure l'adresse de la C6.");
  return fetch(state.server.replace(/\/$/, "") + path, { ...options, headers: { "Content-Type": "application/json", ...(options.headers || {}) } });
}
async function refresh() {
  if (!state.server) { setConnection("unknown","Non connecté"); $("server-detail").textContent="Configure l'adresse locale de ta C6 pour commencer."; $("save-value").disabled=true; return; }
  $("server-state").textContent="Vérification…";
  try {
    const response = await api("/api/v1/health");
    if (!response.ok) throw new Error("HTTP " + response.status);
    const data = await response.json();
    state.healthy = !!data.ok && !!data.journal_healthy;
    setConnection(state.healthy ? "online" : "offline", state.healthy ? "C6 en ligne" : "Stockage indisponible");
    $("server-state").textContent=state.healthy ? "Opérationnel" : "Indisponible";
    $("server-sequence").textContent=data.last_sequence ?? "—";
    $("server-ip").textContent=data.ip || "—";
    $("server-detail").textContent="Firmware " + (data.firmware || "inconnu") + " · protocole " + (data.protocol_version ?? "—");
    $("save-value").disabled=!state.healthy;
    if (state.healthy) await loadValue();
  } catch (error) {
    state.healthy=false; setConnection("offline","C6 inaccessible");
    $("server-state").textContent="Hors ligne"; $("server-sequence").textContent="—"; $("server-ip").textContent="—";
    $("server-detail").textContent=error.message + " (attention : une PWA HTTPS ne peut pas appeler une API C6 en HTTP).";
    $("save-value").disabled=true; $("value-status").textContent="Serveur inaccessible";
  }
}
async function loadValue() {
  const response=await api("/api/v1/value"); if(!response.ok) throw new Error("Lecture impossible");
  const data=await response.json(); $("shared-value").value=data.value || ""; $("value-status").textContent="Synchronisé · révision " + (data.revision ?? 0);
}
$("save-server").addEventListener("click",()=>{state.server=$("server-url").value.trim().replace(/\/$/,"");localStorage.setItem("home.server",state.server);refresh();});
$("refresh").addEventListener("click",refresh);
$("save-value").addEventListener("click",async()=>{ $("save-value").disabled=true; $("value-status").textContent="Enregistrement…"; try {const response=await api("/api/v1/value",{method:"PUT",body:JSON.stringify({value:$("shared-value").value})});if(!response.ok)throw new Error("Écriture refusée ("+response.status+")");const data=await response.json();$("value-status").textContent="Enregistré · révision "+data.revision;}catch(error){$("value-status").textContent=error.message;}finally{$("save-value").disabled=!state.healthy;}});
if ("serviceWorker" in navigator) window.addEventListener("load",()=>navigator.serviceWorker.register("./sw.js").catch(console.error));
refresh();