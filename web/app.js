import { dateKey, suggestRoutine, evaluate, formatDuration, formatClock, gauge, goKey, isDone, mark, stepTime, shiftStep } from "./routine.js";
import { pickLine, SPARE_IDEAS } from "./mascots.js";
import { loadConfig, saveConfig, loadProgress, saveProgress, loadMeta, saveMeta } from "./store.js";
import { defaultConfig } from "./data.js";
import { initCentral } from "./central.js";
import { stable } from "./sync.js";
import { loadLists, saveLists } from "./store.js";
import { askAdult, pinHash, validPin, lockNow, pinsOf, expireReset, resetHoursLeft } from "./lock.js";
import { initShopping } from "./shopping.js";
import { initWeather } from "./weather.js";
import { initMap } from "./map.js";
import { initContacts } from "./contacts.js";
import { initBins } from "./bins.js";
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

// Parties « vivantes », rafraîchies chaque seconde : heure, jauge et minuteur d'étape.
const hour = (time) => time.replace(":", " h ");
function renderTiming(r, info) {
  const [kind, label] = STATUS[info.state];
  $("clock").dataset.kind = kind;
  $("status-label").textContent = label;
  $("deadline-label").textContent = `${info.target.label} à ${hour(info.target.time)}`;
  $("countdown").textContent = info.left > 0 ? formatDuration(Math.ceil(info.left)) : "—";
  $("estimate").textContent = info.remaining > 0 ? `Environ ${formatDuration(Math.ceil(info.remaining))} d'étapes à faire` : "Plus rien d'obligatoire";

  const g = gauge(info.slack);
  $("gauge").hidden = info.state === "empty";
  $("gauge").dataset.tone = g.tone;
  $("gauge-needle").style.left = `${g.position}%`;
  $("gauge-fill").style.left = `${Math.min(50, g.position)}%`;
  $("gauge-fill").style.width = `${Math.abs(g.position - 50)}%`;
  const trend = !info.losing ? "" : info.slack >= 0 ? " · ⏳ l'avance fond" : " · ⏳ le retard grandit";
  $("gauge-label").textContent = (info.state === "done" ? "Fini : " : "") + g.label + trend;

  const { step, started, elapsed, free, available, waiting, wait } = info.timer;
  $("timer").hidden = !step || (info.state === "done" && !free);
  if ($("timer").hidden) return;
  const left = free ? available : waiting ? wait : step.minutes - elapsed;
  const running = started && !free;
  $("timer").dataset.over = String(running && left < 0);
  $("timer-title").textContent = `${step.icon} ${step.label}`;
  $("timer-value").textContent = free || waiting || running ? formatClock(left) : formatClock(step.minutes);
  $("timer-bar").style.width = `${running && step.minutes ? Math.max(0, (100 * left) / step.minutes) : 100}%`;
  $("timer-go").hidden = free || started;
  $("timer-go").textContent = waiting ? "▶ On commence maintenant" : "▶ C'est parti";
  $("timer-hint").textContent = free
    ? (available >= 1 ? `Temps libre jusqu'à « ${info.target.label} » (${hour(info.target.time)}).` : "C'est l'heure : on passe à la suite !")
    : waiting ? `Prévu à ${hour(info.target.time)}.`
    : !started ? "Touche « C'est parti » quand tu commences."
    : left < 0 ? "Le temps prévu est passé : on termine tranquillement, puis on coche." : "Coche l'étape quand c'est fini.";
}
// Heure fixe du jour (le repas) : les parents peuvent la décaler.
function renderAnchor(r, done) {
  const s = r.steps.find((x) => x.at && !isDone(done, x.id));
  $("anchor").hidden = !s;
  if (!s) return;
  $("anchor-label").textContent = `${s.icon} ${s.label} : ${hour(stepTime(s, done))}`;
  for (const button of $("anchor").querySelectorAll("button")) button.onclick = () => {
    if (!adult()) return;
    shiftStep(s, doneMap(), Number(button.dataset.shift), new Date());
    saveProgress(progress); central.soon(); render();
  };
}
function renderPinNotice() {
  const hours = resetHoursLeft(config);
  $("pin-notice").hidden = hours === null;
  if (hours !== null) $("pin-notice-text").textContent = `Codes adultes effacés dans ${hours} h (code oublié).`;
}
let shownState = "";
function tickTiming() {
  if (document.hidden || view !== "home") return;
  const r = routine(), info = evaluate(r, doneMap(), new Date());
  if (dateKey(new Date()) !== today || `${info.state}/${info.next?.id}/${info.target.time}` !== shownState) render();
  else renderTiming(r, info);
}

// « C'est parti » ou « je fais celle-là » : démarre le minuteur de cette étape.
function focusStep(s) {
  const done = doneMap();
  done[goKey(s.id)] = mark(done[goKey(s.id)], true);
  if (!saveProgress(progress)) $("speech").textContent = "Impossible d'enregistrer sur cet appareil.";
  reaction = null;
  central.soon();
  navigator.vibrate?.(30);
  render();
}

function render() {
  const now = new Date();
  if (dateKey(now) !== today) { today = dateKey(now); progress = loadProgress(today); manualRoutine = null; reaction = null; }
  const r = routine();
  const done = doneMap();
  const info = evaluate(r, done, now);
  shownState = `${info.state}/${info.next?.id}/${info.target.time}`;

  $("members").replaceChildren(...config.members.map((m) => el("button", {
    className: "chip" + (m.id === member().id ? " active" : ""), textContent: m.name, type: "button",
    onclick: () => { memberId = m.id; reaction = null; render(); },
  })));
  $("members").hidden = config.members.length < 2;
  $("routine-select").replaceChildren(...config.routines.map((x) => el("option", { value: x.id, textContent: `${x.icon} ${x.name}`, selected: x.id === r.id })));

  renderTiming(r, info);
  renderAnchor(r, done);
  renderPinNotice();
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
        el("small", { textContent: s.free
          ? `temps libre${info.next?.id === s.id ? ` · environ ${formatDuration(info.timer.available)}` : ""}`
          : `${s.at ? `à ${hour(stepTime(s, done))} · ` : ""}${s.minutes} min${s.optional ? " · facultatif" : ""}` })),
      el("span", { className: "check", textContent: checked ? "✓" : "" }));
    button.setAttribute("aria-pressed", String(checked));
    button.onclick = () => toggle(s);
    const play = !checked && info.next?.id !== s.id
      ? el("button", { type: "button", className: "play", textContent: "▶", ariaLabel: `Je fais « ${s.label} » maintenant`, onclick: () => focusStep(s) })
      : null;
    return el("li", {}, button, play);
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

// Poignée de glisser-déposer : on déplace les autres blocs autour de celui qu'on tient.
function handle(r) {
  const grip = el("button", { type: "button", className: "grip", textContent: "⠿", ariaLabel: "Glisser pour déplacer" });
  grip.onpointerdown = (down) => {
    down.preventDefault();
    const block = grip.closest(".step-edit"), parent = block.parentNode;
    const blocks = () => [...parent.querySelectorAll(":scope > .step-edit")];
    const before = blocks();
    block.classList.add("dragging");
    const onMove = (e) => {
      for (const other of blocks()) {
        if (other === block) continue;
        const box = other.getBoundingClientRect(), middle = box.top + box.height / 2;
        const above = !!(block.compareDocumentPosition(other) & Node.DOCUMENT_POSITION_PRECEDING);
        if (above && e.clientY < middle) { parent.insertBefore(block, other); break; }
        if (!above && e.clientY > middle) parent.insertBefore(other, block);
      }
      if (e.clientY < 70) window.scrollBy(0, -14); else if (e.clientY > innerHeight - 70) window.scrollBy(0, 14);
    };
    const onUp = () => {
      removeEventListener("pointermove", onMove); removeEventListener("pointerup", onUp); removeEventListener("pointercancel", onUp);
      r.steps = blocks().map((b) => r.steps[before.indexOf(b)]);
      renderSettings();
    };
    addEventListener("pointermove", onMove); addEventListener("pointerup", onUp); addEventListener("pointercancel", onUp);
  };
  return grip;
}
function renderPins() {
  const pins = draft.pins || [];
  $("pin-state").textContent = pins.length ? "" : "Aucun code : tout le monde peut modifier les réglages.";
  $("pin-list").replaceChildren(...pins.map((p, i) => el("div", { className: "row" },
    input("text", p.name, (v) => (p.name = v), { maxLength: 20, ariaLabel: "Nom de l'adulte" }),
    el("span", { className: "muted", textContent: "code actif" }),
    small("✕", () => { pins.splice(i, 1); renderPins(); }, "Retirer ce code"))));
}
function addPin() {
  const code = $("pin").value.trim();
  if (!validPin(code)) { alert("Le code adulte doit contenir 4 à 8 chiffres."); return false; }
  const name = $("pin-name").value.trim() || `Adulte ${(draft.pins || []).length + 1}`;
  const pins = (draft.pins || []).filter((p) => p.name !== name); // même nom : nouveau code
  draft.pins = [...pins, { name, hash: pinHash(code) }];
  $("pin").value = ""; $("pin-name").value = ""; lockNow(); renderPins();
  return true;
}

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
    const move = (from, to) => { if (to < 0 || to >= r.steps.length) return; r.steps.splice(to, 0, ...r.steps.splice(from, 1)); renderSettings(); };
    const kinds = (s) => {
      const select = el("select", { ariaLabel: "Type d'étape" }, ...[["normal", "Durée"], ["fixed", "Heure fixe"], ["free", "Temps libre"]]
        .map(([value, textContent]) => el("option", { value, textContent, selected: value === (s.free ? "free" : s.at !== undefined ? "fixed" : "normal") })));
      select.onchange = () => {
        delete s.at; delete s.free;
        if (select.value === "fixed") { s.at = r.period === "evening" ? "19:00" : r.deadline; s.optional = false; }
        if (select.value === "free") { s.free = true; s.optional = false; s.minutes = 0; }
        else if (!s.minutes) s.minutes = 5;
        renderSettings();
      };
      return select;
    };
    const steps = r.steps.map((s, i) => el("div", { className: "step-edit" },
      el("div", { className: "row" },
        handle(r),
        input("text", s.icon, (v) => (s.icon = v), { className: "icon-input", maxLength: 8, ariaLabel: "Emoji" }),
        input("text", s.label, (v) => (s.label = v), { maxLength: 40, ariaLabel: "Nom de l'étape" })),
      el("div", { className: "row" }, kinds(s),
        s.at !== undefined ? input("time", s.at, (v) => { if (v) s.at = v; }, { ariaLabel: "Heure prévue" }) : null,
        s.free ? el("span", { className: "muted", textContent: "dure ce qui reste" })
          : input("number", s.minutes, (v) => (s.minutes = Math.min(180, Math.max(0, Math.round(Number(v) || 0)))), { min: 0, max: 180, className: "minutes", ariaLabel: "Durée en minutes" }),
        s.free ? null : el("span", { textContent: "min" })),
      el("div", { className: "row" },
        s.free || s.at !== undefined ? null : el("label", { className: "inline" }, Object.assign(el("input", { type: "checkbox", checked: s.optional }), { onchange: (e) => (s.optional = e.target.checked) }), "facultatif"),
        small("↑", () => move(i, i - 1), "Monter"),
        small("↓", () => move(i, i + 1), "Descendre"),
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

const VIEWS = ["home", "shopping", "weather", "map-view", "contacts", "bins", "settings"];
function show(name) {
  view = name;
  binsView?.close();
  for (const id of VIEWS) $(id).hidden = id !== name;
  for (const tab of document.querySelectorAll("[data-view]")) tab.setAttribute("aria-current", String(tab.dataset.view === name));
  $("tabs").hidden = name === "settings";
  window.scrollTo(0, 0);
  renderView();
}
function renderView() {
  if (view === "home") { render(); binsView.renderHome(); weather.render(); weather.refresh(); }
  else if (view === "bins") binsView.render();
  else if (view === "weather") { weather.render(); weather.refresh(); }
  else if (view === "shopping") renderShopping();
  else if (view === "map-view") renderMap();
  else if (view === "contacts") renderContacts();
}
function saveAll() { saveConfig(config); saveProgress(progress); saveMeta(meta); saveLists(lists); }

// Autorisation adulte ; gère aussi l'effacement d'un code oublié.
function adult() {
  const answer = askAdult(config);
  if (answer === "reset" || answer === "changed") { meta.dirty = true; saveAll(); central.soon(); renderPinNotice(); }
  return answer === true || answer === "reset";
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
const binsView = initBins(shared);
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
  draft = structuredClone(config);
  if (pinsOf(draft).length) draft.pins = pinsOf(draft);
  delete draft.pin;
  $("pin").value = ""; $("pin-name").value = ""; renderPins(); renderSettings(); show("settings");
};
$("pin-add").onclick = addPin;
$("pin-cancel").onclick = () => { delete config.pinReset; meta.dirty = true; saveAll(); central.soon(); renderPinNotice(); };
for (const tab of document.querySelectorAll("[data-view]")) tab.onclick = () => show(tab.dataset.view);
$("weather-chip").onclick = () => show("weather");
$("bins-chip").onclick = $("bins-dates").onclick = () => show("bins");
$("cancel-settings").onclick = () => { show("home"); central.sync(); };
$("add-member").onclick = () => { draft.members.push({ id: uid(), name: "" }); renderSettings(); };
$("holidays").onchange = (e) => (draft.holidays = e.target.checked);
$("reset-config").onclick = () => { if (confirm("Revenir aux routines par défaut ?")) { draft = { ...defaultConfig(), pins: draft.pins }; renderPins(); renderSettings(); } };
$("save-settings").onclick = () => {
  if ($("pin").value.trim() && !addPin()) return;
  if (!draft.pins?.length) { delete draft.pins; delete draft.pinReset; }
  draft.members.forEach((m, i) => (m.name = m.name.trim() || `Enfant ${i + 1}`));
  draft.routines.forEach((r) => r.steps.forEach((s) => (s.label = s.label.trim() || "Étape")));
  if (stable(draft) !== stable(config)) meta.dirty = true;
  saveMeta(meta);
  config = draft; manualRoutine = null; reaction = null;
  if (!saveConfig(config)) { alert("Impossible d'enregistrer sur cet appareil."); return; }
  show("home"); central.sync();
};
$("routine-select").onchange = (e) => { manualRoutine = e.target.value; reaction = null; render(); };
$("reset-day").onclick = () => { if (!adult()) return; const d = doneMap(); for (const k in d) if (!k.startsWith("_at:")) d[k] = mark(d[k], false); saveProgress(progress); reaction = null; render(); central.soon(); };

show("home");
central.sync();
function tick() { if (!document.hidden && view !== "settings") { if (expireReset(config)) { meta.dirty = true; saveAll(); } if (view === "home") { render(); binsView.renderHome(); weather.render(); weather.refresh(); } central.sync(); } }
setInterval(tick, 15000);
setInterval(tickTiming, 1000);
$("timer-go").onclick = () => { const s = evaluate(routine(), doneMap(), new Date()).next; if (s) focusStep(s); };
document.addEventListener("visibilitychange", tick);
if ("serviceWorker" in navigator) window.addEventListener("load", () => navigator.serviceWorker.register("./sw.js").catch(console.error));
