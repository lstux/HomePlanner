import { dateKey, suggestRoutine, evaluate, formatDuration, isDone, mark } from "./routine.js";
import { pickLine, SPARE_IDEAS } from "./mascots.js";
import { loadConfig, saveConfig, loadProgress, saveProgress, loadMeta, saveMeta } from "./store.js";
import { defaultConfig } from "./data.js";
import { initCentral } from "./central.js";
import { stable } from "./sync.js";

const $ = (id) => document.getElementById(id);
const el = (tag, props = {}, ...children) => {
  const node = Object.assign(document.createElement(tag), props);
  node.append(...children);
  return node;
};
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

function show(view) {
  for (const id of ["home", "settings"]) $(id).hidden = id !== view;
  window.scrollTo(0, 0);
}
const central = initCentral({
  get config() { return config; },
  set config(value) { config = value; },
  get progress() { return progress; },
  meta,
  canApplyConfig: () => $("settings").hidden,
  save() { saveConfig(config); saveProgress(progress); saveMeta(meta); },
}, (result) => {
  if (result.replacedLocalConfig) alert("Les réglages avaient aussi changé sur un autre appareil : ceux de la maison ont été repris.");
  if ((result.configChanged || result.progressChanged) && !$("home").hidden) render();
});
$("open-settings").onclick = () => { draft = structuredClone(config); renderSettings(); show("settings"); };
$("cancel-settings").onclick = () => { show("home"); render(); central.sync(); };
$("add-member").onclick = () => { draft.members.push({ id: uid(), name: "" }); renderSettings(); };
$("holidays").onchange = (e) => (draft.holidays = e.target.checked);
$("reset-config").onclick = () => { if (confirm("Revenir aux routines par défaut ?")) { draft = defaultConfig(); renderSettings(); } };
$("save-settings").onclick = () => {
  draft.members.forEach((m, i) => (m.name = m.name.trim() || `Enfant ${i + 1}`));
  draft.routines.forEach((r) => r.steps.forEach((s) => (s.label = s.label.trim() || "Étape")));
  if (stable(draft) !== stable(config)) meta.dirty = true;
  saveMeta(meta);
  config = draft; manualRoutine = null; reaction = null;
  if (!saveConfig(config)) { alert("Impossible d'enregistrer sur cet appareil."); return; }
  show("home"); render(); central.sync();
};
$("routine-select").onchange = (e) => { manualRoutine = e.target.value; reaction = null; render(); };
$("reset-day").onclick = () => { const d = doneMap(); for (const k in d) d[k] = mark(d[k], false); saveProgress(progress); reaction = null; render(); central.soon(); };

render();
central.sync();
function tick() { if (!document.hidden && !$("home").hidden) { render(); central.sync(); } }
setInterval(tick, 15000);
document.addEventListener("visibilitychange", tick);
if ("serviceWorker" in navigator) window.addEventListener("load", () => navigator.serviceWorker.register("./sw.js").catch(console.error));
