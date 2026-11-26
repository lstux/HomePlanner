// Poubelles : dates de ramassage, jours fériés, rappel la veille.
// Réglage partagé : config.bins ; ajustements d'une date et « c'est sorti » : liste « bins ».
import { $, el, button } from "./dom.js";
import { upsert, remove, newId } from "./lists.js";

// Pré-rempli : tri un jeudi sur deux (dont le 26/11/2026), le reste chaque mardi.
export const DEFAULT_BINS = { shift: true, rules: [
  { id: "tri", icon: "♻️", name: "Tri", from: "2026-11-26", every: 2 },
  { id: "reste", icon: "🗑️", name: "Ordures ménagères", from: "2026-11-24", every: 1 },
] };
export const binsConfig = (config) => config.bins || DEFAULT_BINS;

// Calculs sur des dates « AAAA-MM-JJ », en UTC pour ignorer les changements d'heure.
const DAY = 86400000;
const toDay = (iso) => Math.round(Date.parse(iso + "T00:00:00Z") / DAY);
const toIso = (day) => new Date(day * DAY).toISOString().slice(0, 10);
export const addDays = (iso, n) => toIso(toDay(iso) + n);
const weekday = (iso) => new Date(iso + "T00:00:00Z").getUTCDay();

function easter(year) { // calcul grégorien (Meeus)
  const a = year % 19, b = Math.floor(year / 100), c = year % 100;
  const h = (19 * a + b - Math.floor(b / 4) - Math.floor((b - Math.floor((b + 8) / 25) + 1) / 3) + 15) % 30;
  const l = (32 + 2 * (b % 4) + 2 * Math.floor(c / 4) - h - (c % 4)) % 7;
  const m = Math.floor((a + 11 * h + 22 * l) / 451);
  const n = h + l - 7 * m + 114;
  return `${year}-${String(Math.floor(n / 31)).padStart(2, "0")}-${String((n % 31) + 1).padStart(2, "0")}`;
}
// Jours fériés en France métropolitaine (hors particularités locales).
export function holidays(year) {
  const e = easter(year);
  return new Map([["01-01", "Jour de l'an"], ["05-01", "1er mai"], ["05-08", "8 mai"], ["07-14", "14 juillet"],
    ["08-15", "Assomption"], ["11-01", "Toussaint"], ["11-11", "11 novembre"], ["12-25", "Noël"]]
    .map(([d, n]) => [`${year}-${d}`, n])
    .concat([[addDays(e, 1), "Lundi de Pâques"], [addDays(e, 39), "Ascension"], [addDays(e, 50), "Lundi de Pentecôte"]]));
}
// Férié (du lundi au vendredi) entre le début de la semaine et le jour de ramassage.
export function holidayBefore(iso) {
  for (let i = (weekday(iso) + 6) % 7; i >= 0; i--) {
    const date = addDays(iso, -i), name = holidays(Number(date.slice(0, 4))).get(date);
    if (name && weekday(date) % 6 !== 0) return name;
  }
  return "";
}

// Ramassages à partir de « today », triés. overrides : lists.bins (clé « poubelle.date prévue »).
export function collections(bins, overrides, today, days = 70) {
  const out = [];
  for (const rule of bins.rules) {
    const step = 7 * Math.max(1, rule.every || 1);
    if (!Number.isFinite(toDay(rule.from))) continue;
    // On regarde un peu en arrière : une date passée peut avoir été repoussée.
    let day = toDay(rule.from) + Math.ceil((toDay(today) - 21 - toDay(rule.from)) / step) * step;
    for (; day <= toDay(today) + days; day += step) {
      const planned = toIso(day), key = `${rule.id}.${planned}`;
      const o = overrides[key] && !overrides[key].x ? overrides[key] : {};
      const holiday = holidayBefore(planned);
      const date = o.move || (holiday && bins.shift ? addDays(planned, 1) : planned);
      if (date >= today) out.push({ key, rule, planned, date, holiday, moved: !!o.move, skipped: !!o.skip, out: !!o.out });
    }
  }
  return out.sort((a, b) => a.date.localeCompare(b.date) || a.rule.name.localeCompare(b.rule.name, "fr"));
}

const pad = (n) => String(n).padStart(2, "0");
const localDate = (now) => `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`;

// Rappel : le matin du ramassage (jusqu'à 10 h, si rien n'est coché), sinon toute la veille.
export function reminder(bins, overrides, now) {
  const today = localDate(now);
  const all = collections(bins, overrides, today, 8).filter((c) => !c.skipped);
  const morning = now.getHours() < 10 ? all.filter((c) => c.date === today && !c.out) : [];
  if (morning.length) return { when: "morning", items: morning };
  const eve = all.filter((c) => c.date === addDays(today, 1));
  return eve.length ? { when: "eve", items: eve } : null;
}

export const frenchDate = (iso) => new Date(iso + "T12:00:00Z").toLocaleDateString("fr-FR", { weekday: "long", day: "numeric", month: "long", timeZone: "UTC" }).replace(/ 1 /, " 1er ");

export function initBins(app) {
  let editing = "", draft = null;
  const overrides = () => app.lists.bins;
  function save(key, fields) { upsert(overrides(), key, fields); app.changed(); render(); renderHome(); }

  function renderHome() {
    const now = new Date(), bins = binsConfig(app.config);
    const due = reminder(bins, overrides(), now);
    const next = collections(bins, overrides(), localDate(now)).find((c) => !c.skipped);
    $("bins-reminder").hidden = !due;
    $("bins-chip").hidden = !!due || !next;
    if (next) $("bins-chip").textContent = `${next.rule.icon} Prochaine poubelle : ${next.rule.name}, ${frenchDate(next.date)}`;
    if (!due) return;
    const names = due.items.map((c) => `${c.rule.icon} ${c.rule.name}`).join(" et ");
    const done = due.items.every((c) => c.out);
    $("bins-reminder").dataset.done = String(done);
    $("bins-title").textContent = done ? `✓ C'est sorti : ${names}`
      : due.when === "morning" ? `Ramassage ce matin : ${names} !` : `Ce soir, on sort : ${names}`;
    const unsure = due.items.find((c) => c.holiday && !c.moved);
    $("bins-note").textContent = unsure ? `Semaine avec jour férié (${unsure.holiday}) : date à vérifier.`
      : due.when === "eve" ? `Ramassage demain, ${frenchDate(due.items[0].date)}.` : "";
    $("bins-out").textContent = done ? "Annuler" : "C'est sorti ✓";
    $("bins-out").className = done ? "button secondary" : "button";
    $("bins-out").onclick = () => { for (const c of due.items) upsert(overrides(), c.key, { out: !done }); app.changed(); renderHome(); };
  }

  function editor(c) {
    const date = el("input", { type: "date", value: c.date, ariaLabel: "Nouvelle date" });
    return el("div", { className: "bin-edit" },
      el("div", { className: "row" }, date,
        button("Déplacer", "button", () => { if (!date.value) return; editing = ""; save(c.key, { move: date.value, skip: false }); })),
      el("div", { className: "row" },
        button(c.skipped ? "Rétablir le ramassage" : "Pas de ramassage", "button secondary", () => { editing = ""; save(c.key, { skip: !c.skipped }); }),
        button("Date calculée", "button secondary", () => { editing = ""; save(c.key, { move: "", skip: false }); })));
  }

  function renderRules() {
    $("bin-rules").replaceChildren(...draft.rules.map((r, i) => {
      const field = (type, key, props) => {
        const node = el("input", { type, value: r[key], ...props });
        node.oninput = () => (r[key] = node.value);
        return node;
      };
      const every = el("select", { ariaLabel: "Fréquence" }, ...[1, 2, 3, 4].map((n) =>
        el("option", { value: n, textContent: n === 1 ? "chaque semaine" : `une semaine sur ${n}`, selected: r.every === n })));
      every.onchange = () => (r.every = Number(every.value));
      return el("div", { className: "step-edit" },
        el("div", { className: "row" }, field("text", "icon", { className: "icon-input", maxLength: 8, ariaLabel: "Emoji" }),
          field("text", "name", { maxLength: 30, ariaLabel: "Nom de la poubelle" }),
          button("✕", "mini", () => { draft.rules.splice(i, 1); renderRules(); }, "Supprimer")),
        el("label", { className: "field" }, el("span", { textContent: "Un jour de ramassage (donne le jour et la semaine)" }), field("date", "from", {})),
        el("div", { className: "row" }, every));
    }));
    $("bin-shift").checked = draft.shift;
  }

  function render() {
    const today = localDate(new Date());
    const list = collections(binsConfig(app.config), overrides(), today).slice(0, 10);
    $("bin-list").replaceChildren(...list.map((c) => {
      const notes = [c.out && "sortie ✓", c.skipped && "pas de ramassage", c.moved && `déplacé (prévu ${frenchDate(c.planned)})`,
        !c.moved && c.holiday && (c.date === c.planned ? `férié (${c.holiday}) : à vérifier` : `décalé d'un jour (${c.holiday}) : à vérifier`)];
      return el("li", { className: "bin" + (c.skipped ? " skipped" : "") },
        el("div", { className: "contact" },
          el("span", { className: "step-icon", textContent: c.rule.icon }),
          el("div", { className: "step-text" }, el("strong", { textContent: frenchDate(c.date) }),
            el("small", { textContent: [c.rule.name, ...notes].filter(Boolean).join(" · ") })),
          button("✏️", "mini", () => { if (editing !== c.key && !app.adult()) return; editing = editing === c.key ? "" : c.key; render(); }, "Ajuster cette date")),
        editing === c.key ? editor(c) : null);
    }));
    $("bin-rules-panel").hidden = !draft;
    $("bin-rules-open").hidden = !!draft;
  }

  $("bin-rules-open").onclick = () => { if (!app.adult()) return; draft = structuredClone(binsConfig(app.config)); renderRules(); render(); };
  $("bin-add").onclick = () => { draft.rules.push({ id: newId(), icon: "🍾", name: "Verre", from: localDate(new Date()), every: 4 }); renderRules(); };
  $("bin-shift").onchange = (e) => (draft.shift = e.target.checked);
  $("bin-rules-cancel").onclick = () => { draft = null; render(); };
  $("bin-rules-save").onclick = () => {
    if (draft.rules.some((r) => !r.from)) { alert("Chaque poubelle a besoin d'une date de ramassage."); return; }
    draft.rules.forEach((r) => (r.name = r.name.trim() || "Poubelle"));
    app.config.bins = draft; draft = null; app.configChanged(); render(); renderHome();
  };
  // Les ajustements anciens sont effacés pour que la liste reste petite.
  const limit = addDays(localDate(new Date()), -30);
  for (const key of Object.keys(overrides())) if (key.split(".").at(-1) < limit) remove(overrides(), key);
  return { render, renderHome, close() { draft = null; editing = ""; } };
}
