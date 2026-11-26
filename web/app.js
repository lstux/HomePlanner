import { dateKey, suggestRoutine, evaluate, formatDuration, isDone, mark } from "./routine.js";
import { pickLine, SPARE_IDEAS } from "./mascots.js";
import { loadConfig, saveConfig, loadProgress, saveProgress, loadMeta, saveMeta } from "./store.js";
import { defaultConfig } from "./data.js";
import { initCentral } from "./central.js";
import { stable } from "./sync.js";
import { loadLists, saveLists } from "./store.js";
import { askAdult, pinHash, validPin, lockNow } from "./lock.js";
import { initShopping } from "./shopping.js";
import { initWeather } from "./weather.js";
import { initMap } from "./map.js";
import { initContacts } from "./contacts.js";
import { $, el } from "./dom.js";

const uid = () => Math.random().toString(36).slice(2, 10);
const DAYS = ["Di", "Lu", "Ma", "Me", "Je", "Ve", "Sa"];

let config = loadConfig();
let today = dateKey(new Date());
let progress = loadProgress(today);
let memberId = config.members[0].id;
let manualRoutine = null;
let reaction = null; // réaction temporaire à une case cochée / décochée
let lineSeed = new Date().getDate();
let lastState = "";
const meta = loadMeta();
const lists = loadLists();
let view = "home";

const routine = () => config.routines.find((r) => r.id === manualRoutine) || suggestRoutine(config, new Date());
const member = () => config.members.find((m) => m.id === memberId) || config.members[0];
function doneMap() {
  const key = `${member().id}/${routine().id}`;
  return (progress.done[key] ||= {});
}

const STATUS = {
  early: ["good", "En avance"], ontime: ["good", "Dans les temps"], tight: ["warn", "C'est juste"],
  late: ["late", "Un peu de retard"], passed: ["late", "Heure dépassée"], done: ["good", "Terminé !"], empty: ["warn", "Aucune étape"],
};

function render() {
  const now = new Date();
  if (dateKey(now) !== today) { today = dateKey(now); progress = loadProgress(today); manualRoutine = null; reaction = null; }
  const r = routine();
  const done = doneMap();
  const info = evaluate(r, done, now);

  $("members").replaceChildren(...config.members.map((m) => el("button", {
    className: "chip" + (m.id === member().id ? " active" : ""), textContent: m.name, type: "button",
    onclick: () => { memberId = m.id; reaction = null; render(); },
  })));
  $("members").hidden = config.members.length < 2;
  $("routine-select").replaceChildren(...config.routines.map((x) => el("option", { value: x.id, textContent: `${x.icon} ${x.name}`, selected: x.id === r.id })));

  const [kind, label] = STATUS[info.state];
  $("clock").dataset.kind = kind;
  $("status-label").textContent = label;
  $("deadline-label").textContent = `${r.deadlineLabel} à ${r.deadline.replace(":", " h ")}`;
  $("countdown").textContent = info.left > 0 ? formatDuration(Math.ceil(info.left)) : "—";
  $("estimate").textContent = info.remaining ? `Environ ${formatDuration(info.remaining)} d'étapes à faire` : "Plus rien d'obligatoire";
  $("progress-bar").style.width = info.total ? `${(100 * info.doneCount) / info.total}%` : "0";
  $("progress-text").textContent = `${info.doneCount} / ${info.total}`;

  let situation = info.state;
  if (info.doneCount === 0 && (situation === "early" || situation === "ontime")) situation = "welcome";
  if (reaction && info.state !== "done") situation = reaction.type;
  if (situation !== lastState) { lastState = situation; lineSeed++; }
  const line = pickLine(situation, { name: member().name, next: info.next?.label.toLowerCase(), step: reaction?.label }, lineSeed);
  if ($("mascot").getAttribute("src") !== line.image) $("mascot").src = line.image;
  $("mascot").alt = line.who === "fox" ? "Le renard" : "Le lapin";
  $("speech").textContent = line.text;
  $("duo").hidden = info.state !== "done";

  // Temps libre : seulement le matin, et sans écran.
  const spare = r.period !== "evening" && info.state === "early" && info.doneCount > 0 && info.slack >= 15 && info.left <= 120;
  $("spare").hidden = !spare;
  if (spare) $("spare").textContent = `Tu as environ ${formatDuration(info.slack)} de marge : tu pourrais ${SPARE_IDEAS[now.getDate() % SPARE_IDEAS.length]}`;

  $("steps").replaceChildren(...r.steps.map((s) => {
    const checked = isDone(done, s.id);
    const button = el("button", { type: "button", className: "step" + (checked ? " done" : "") + (info.next?.id === s.id ? " next" : "") },
      el("span", { className: "step-icon", textContent: s.icon }),
      el("span", { className: "step-text" },
        el("strong", { textContent: s.label }),
        el("small", { textContent: `${s.minutes} min${s.optional ? " · facultatif" : ""}` })),
      el("span", { className: "check", textContent: checked ? "✓" : "" }));
    button.setAttribute("aria-pressed", String(checked));
    button.onclick = () => toggle(s);
    return el("li", {}, button);
  }));
}

let reactionTimer;
function toggle(s) {
  const done = doneMap();
  const checked = !isDone(done, s.id);
  done[s.id] = mark(done[s.id], checked);
  if (!saveProgress(progress)) $("speech").textContent = "Impossible d'enregistrer sur cet appareil.";
  reaction = { type: checked ? "checked" : "unchecked", label: s.label };
  central.soon();
  lineSeed++;
  navigator.vibrate?.(30);
  $("mascot").classList.remove("pop"); void $("mascot").offsetWidth; $("mascot").classList.add("pop");
  clearTimeout(reactionTimer);
  reactionTimer = setTimeout(() => { reaction = null; render(); }, 4000);
  render();
  if (!$("duo").hidden) window.scrollTo({ top: 0, behavior: "smooth" });
}

// ---------- Réglages ----------
let draft;
function field(label, input) { return el("label", { className: "field" }, el("span", { textContent: label }), input); }
function input(type, value, onchange, extra = {}) {
  const node = el("input", { type, value, ...extra });
  node.oninput = () => onchange(node.value);
  return node;
}
const small = (text, onclick, title) => el("button", { type: "button", className: "mini", textContent: text, onclick, title, ariaLabel: title });

function renderSettings() {
  $("member-list").replaceChildren(...draft.members.map((m, i) => el("div", { className: "row" },
    input("text", m.name, (v) => (m.name = v), { maxLength: 20, ariaLabel: "Prénom" }),
    small("✕", () => { if (draft.members.length > 1) { draft.members.splice(i, 1); renderSettings(); } }, "Retirer"))));
  $("holidays").checked = draft.holidays;
  const open = new Set([...$("routine-list").querySelectorAll("details[open]")].map((d) => d.dataset.id));
  $("routine-list").replaceChildren(...draft.routines.map((r) => {
    const days = el("div", { className: "days" }, ...DAYS.map((name, day) => {
      const box = el("input", { type: "checkbox", checked: r.days.includes(day) });
      box.onchange = () => {
        // Un jour n'appartient qu'à une routine par période.
        if (box.checked) draft.routines.filter((x) => x.period === r.period).forEach((x) => (x.days = x.days.filter((d) => d !== day)));
        r.days = r.days.filter((d) => d !== day);
        if (box.checked) r.days.push(day);
        renderSettings();
      };
      return el("label", {}, box, el("span", { textContent: name }));
    }));
    const steps = r.steps.map((s, i) => el("div", { className: "step-edit" },
      el("div", { className: "row" },
        input("text", s.icon, (v) => (s.icon = v), { className: "icon-input", maxLength: 8, ariaLabel: "Emoji" }),
        input("text", s.label, (v) => (s.label = v), { maxLength: 40, ariaLabel: "Nom de l'étape" })),
      el("div", { className: "row" },
        input("number", s.minutes, (v) => (s.minutes = Math.min(180, Math.max(0, Math.round(Number(v) || 0)))), { min: 0, max: 180, className: "minutes", ariaLabel: "Durée en minutes" }),
        el("span", { textContent: "min" }),
        el("label", { className: "inline" }, Object.assign(el("input", { type: "checkbox", checked: s.optional }), { onchange: (e) => (s.optional = e.target.checked) }), "facultatif"),
        small("↑", () => { if (i) { [r.steps[i - 1], r.steps[i]] = [s, r.steps[i - 1]]; renderSettings(); } }, "Monter"),
        small("✕", () => { r.steps.splice(i, 1); renderSettings(); }, "Supprimer"))));
    const details = el("details", { open: open.has(r.id) },
      el("summary", { textContent: `${r.icon} ${r.name}` }),
      field(`${r.deadlineLabel} à`, input("time", r.deadline, (v) => { if (v) r.deadline = v; })),
      r.period === "any" ? el("p", { className: "muted", textContent: "Utilisée le matin quand le mode vacances est activé." }) : days,
      ...steps,
      el("button", { type: "button", className: "button secondary", textContent: "+ Ajouter une étape",
        onclick: () => { r.steps.push({ id: uid(), icon: "⭐", label: "Nouvelle étape", minutes: 5, optional: false }); renderSettings(); } }));
    details.dataset.id = r.id;
    return details;
  }));
}

const VIEWS = ["home", "shopping", "weather", "map-view", "contacts", "settings"];
function show(name) {
  view = name;
  for (const id of VIEWS) $(id).hidden = id !== name;
  for (const tab of document.querySelectorAll("[data-view]")) tab.setAttribute("aria-current", String(tab.dataset.view === name));
  $("tabs").hidden = name === "settings";
  window.scrollTo(0, 0);
  renderView();
}
function renderView() {
  if (view === "home") { render(); weather.render(); weather.refresh(); }
  else if (view === "weather") { weather.render(); weather.refresh(); }
  else if (view === "shopping") renderShopping();
  else if (view === "map-view") renderMap();
  else if (view === "contacts") renderContacts();
}
function saveAll() { saveConfig(config); saveProgress(progress); saveMeta(meta); saveLists(lists); }

// Autorisation adulte ; gère aussi l'effacement d'un code oublié.
function adult() {
  const ok = askAdult(config);
  if (ok === "reset") { meta.dirty = true; saveAll(); central.soon(); }
  return !!ok;
}
const shared = {
  get config() { return config; },
  lists,
  adult,
  changed() { if (!saveLists(lists)) alert("Impossible d'enregistrer sur cet appareil."); central.soon(); },
  configChanged() { meta.dirty = true; saveAll(); central.soon(); },
};
const weather = initWeather(shared);
const renderShopping = initShopping(shared);
const renderMap = initMap(shared);
const renderContacts = initContacts(shared);
const central = initCentral({
  get config() { return config; },
  set config(value) { config = value; },
  get progress() { return progress; },
  lists,
  meta,
  canApplyConfig: () => $("settings").hidden,
  save: saveAll,
}, (result) => {
  if (result.replacedLocalConfig) alert("Les réglages avaient aussi changé sur un autre appareil : ceux de la maison ont été repris.");
  if (result.configChanged || result.progressChanged || result.listsChanged) renderView();
});
$("open-settings").onclick = () => {
  if (!adult()) return;
  draft = structuredClone(config); $("pin").value = ""; $("pin-state").textContent = draft.pin ? "Code actif" : "Aucun code";
  $("pin-remove").hidden = !draft.pin; renderSettings(); show("settings");
};
$("pin-remove").onclick = () => { delete draft.pin; $("pin-state").textContent = "Code retiré à l'enregistrement"; $("pin-remove").hidden = true; };
for (const tab of document.querySelectorAll("[data-view]")) tab.onclick = () => show(tab.dataset.view);
$("weather-chip").onclick = () => show("weather");
$("cancel-settings").onclick = () => { show("home"); central.sync(); };
$("add-member").onclick = () => { draft.members.push({ id: uid(), name: "" }); renderSettings(); };
$("holidays").onchange = (e) => (draft.holidays = e.target.checked);
$("reset-config").onclick = () => { if (confirm("Revenir aux routines par défaut ?")) { draft = defaultConfig(); renderSettings(); } };
$("save-settings").onclick = () => {
  const pin = $("pin").value.trim();
  if (pin) {
    if (!validPin(pin)) { alert("Le code adulte doit contenir 4 à 8 chiffres."); return; }
    draft.pin = pinHash(pin); lockNow();
  }
  draft.members.forEach((m, i) => (m.name = m.name.trim() || `Enfant ${i + 1}`));
  draft.routines.forEach((r) => r.steps.forEach((s) => (s.label = s.label.trim() || "Étape")));
  if (stable(draft) !== stable(config)) meta.dirty = true;
  saveMeta(meta);
  config = draft; manualRoutine = null; reaction = null;
  if (!saveConfig(config)) { alert("Impossible d'enregistrer sur cet appareil."); return; }
  show("home"); central.sync();
};
$("routine-select").onchange = (e) => { manualRoutine = e.target.value; reaction = null; render(); };
$("reset-day").onclick = () => { if (!adult()) return; const d = doneMap(); for (const k in d) d[k] = mark(d[k], false); saveProgress(progress); reaction = null; render(); central.soon(); };

show("home");
central.sync();
function tick() { if (!document.hidden && view !== "settings") { if (view === "home") { render(); weather.render(); weather.refresh(); } central.sync(); } }
setInterval(tick, 15000);
document.addEventListener("visibilitychange", tick);
if ("serviceWorker" in navigator) window.addEventListener("load", () => navigator.serviceWorker.register("./sw.js").catch(console.error));
