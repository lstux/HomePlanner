// Écrans de l'agenda : mois « comme au mur », semaine, jour, et la saisie.
import { $, el, button } from "./dom.js";
import { upsert, remove, newId } from "./lists.js";
import { addDays, holidays } from "./bins.js";
import { allEvents, background, cleanEvent, lanes, localDate, occurrences, onDay, persons, pruneEvents, roomiest } from "./events.js";

const format = (iso, options) => new Date(iso + "T12:00:00Z").toLocaleDateString("fr-FR", { ...options, timeZone: "UTC" }).replace(/\b1 (?=\p{L})/u, "1er ");
export const dayName = (iso) => format(iso, { weekday: "long", day: "numeric", month: "long" });
export const hourText = (time) => time.replace(/^0/, "").replace(":", " h ").replace(" h 00", " h");
const weekday = (iso) => new Date(iso + "T00:00:00Z").getUTCDay();
const holidayName = (iso) => holidays(Number(iso.slice(0, 4))).get(iso) || "";
const monthStart = (iso) => iso.slice(0, 7) + "-01";
const addMonths = (iso, n) => {
  const d = new Date(Date.UTC(Number(iso.slice(0, 4)), Number(iso.slice(5, 7)) - 1 + n, 1));
  return d.toISOString().slice(0, 10);
};
const monthDays = (first) => {
  const out = [];
  for (let d = first; d.slice(0, 7) === first.slice(0, 7); d = addDays(d, 1)) out.push(d);
  return out;
};
const weekStart = (iso) => addDays(iso, -((weekday(iso) + 6) % 7));

export function initAgenda(app) {
  let mode = "month", cursor = localDate(new Date()), editing = null, scrollToToday = true;

  const label = (o) => `${o.event.time ? hourText(o.event.time) + " " : ""}${o.event.title}${o.event.maybe ? " ?" : ""}`;
  function chip(o, people) {
    const node = el("button", { type: "button", className: "event" + (o.event.maybe ? " maybe" : ""), textContent: label(o) });
    node.style.background = background(o.event, people);
    node.onclick = (e) => { e.stopPropagation(); edit(o.event); };
    return node;
  }
  // Ligne détaillée (semaine, jour, accueil).
  function line(o, people, before) {
    const names = (o.event.who || []).map((id) => people.find((p) => p.id === id)?.name).filter(Boolean);
    const notes = [o.end > o.start && `du ${format(o.start, { day: "numeric", month: "short" })} au ${format(o.end, { day: "numeric", month: "short" })}`,
      names.join(", "), o.event.maybe && "à confirmer", o.event.yearly && "chaque année"].filter(Boolean);
    const node = el("button", { type: "button", className: "event big-event" + (o.event.maybe ? " maybe" : "") },
      el("strong", { textContent: label(o) }), notes.length ? el("small", { textContent: notes.join(" · ") }) : null);
    node.style.background = background(o.event, people);
    node.onclick = (e) => { e.stopPropagation(); before?.(); edit(o.event); };
    return node;
  }

  // Téléphone tourné ou grand écran : deux ou trois mois côte à côte, comme au mur.
  const wide = [matchMedia("(orientation: landscape) and (min-width: 640px)"), matchMedia("(orientation: landscape) and (min-width: 880px)")];
  const monthCount = () => 1 + wide.filter((q) => q.matches).length;
  function monthsView(people, events) {
    const count = monthCount();
    if (count === 1) return monthView(people, events, monthStart(cursor));
    return el("div", { className: "months" }, ...Array.from({ length: count }, (_, i) => {
      const first = addMonths(cursor, i);
      return el("section", {}, el("h2", { textContent: format(first, { month: "long", year: "numeric" }) }), monthView(people, events, first));
    }));
  }

  function monthView(people, events, start) {
    const days = monthDays(start), first = days[0], last = days.at(-1), today = localDate(new Date());
    const list = occurrences(events, first, last), columns = lanes(list);
    const count = Math.max(0, ...[...columns.values()].map((n) => n + 1));
    return el("ol", { className: "month" }, ...days.map((date) => {
      const here = onDay(list, date), holiday = holidayName(date);
      // Une période est nommée à son premier jour (ou au 1er du mois), puis suivie par son trait.
      const named = here.filter((o) => o.start === o.end || date === o.start || date === first);
      const bars = Array.from({ length: count }, (_, lane) => {
        const o = here.find((x) => columns.get(x) === lane);
        const bar = el("i", { className: "bar" + (o ? "" : " empty") });
        if (o) { bar.style.background = background(o.event, people); bar.title = o.event.title; bar.onclick = (e) => { e.stopPropagation(); edit(o.event); }; }
        return bar;
      });
      const row = el("li", { className: "month-day" + (weekday(date) % 6 === 0 || holiday ? " off" : "") + (date === today ? " today" : "") + (weekday(date) === 1 ? " monday" : "") },
        el("button", { type: "button", className: "day-number", ariaLabel: `Voir le ${dayName(date)}`, onclick: (e) => { e.stopPropagation(); cursor = date; mode = "day"; render(); } },
          el("span", { textContent: "DLMMJVS"[weekday(date)] }), el("strong", { textContent: Number(date.slice(8)) })),
        el("div", { className: "day-events" }, holiday ? el("span", { className: "holiday", textContent: holiday }) : null, ...named.map((o) => chip(o, people))),
        ...bars);
      row.onclick = () => edit({ date });
      return row;
    }));
  }

  function daysView(people, events, dates) {
    const list = occurrences(events, dates[0], dates.at(-1)), today = localDate(new Date());
    return el("div", { className: "agenda-days" + (dates.length > 1 ? " week" : "") }, ...dates.map((date) => {
      const here = onDay(list, date), holiday = holidayName(date);
      const block = el("section", { className: "agenda-day" + (date === today ? " today" : "") },
        el("h2", { textContent: dayName(date) + (holiday ? ` · ${holiday}` : "") }),
        ...here.map((o) => line(o, people)),
        here.length ? null : el("p", { className: "muted", textContent: "Rien de prévu." }),
        button("+ Ajouter ce jour-là", "button secondary", () => edit({ date })));
      return block;
    }));
  }

  const title = () => {
    if (mode === "month" && monthCount() > 1) {
      const last = addMonths(cursor, monthCount() - 1);
      return `${format(cursor, { month: "short", year: "numeric" })} – ${format(last, { month: "short", year: "numeric" })}`;
    }
    if (mode === "month") return format(cursor, { month: "long", year: "numeric" });
    if (mode === "day") return format(cursor, { weekday: "short", day: "numeric", month: "long" });
    const a = weekStart(cursor), b = addDays(a, 6);
    return `${format(a, { day: "numeric", month: "short" })} – ${format(b, { day: "numeric", month: "short" })}`;
  };

  function render() {
    $("agenda-form").hidden = !editing;
    $("agenda-browse").hidden = !!editing;
    if (editing) return; // la saisie en cours n'est pas touchée par une synchronisation
    const people = persons(app.config), events = allEvents(app.lists);
    $("agenda-title").textContent = title();
    for (const b of $("agenda-modes").children) b.setAttribute("aria-pressed", String(b.dataset.mode === mode));
    const body = mode === "month" ? monthsView(people, events)
      : daysView(people, events, mode === "day" ? [cursor] : Array.from({ length: 7 }, (_, i) => addDays(weekStart(cursor), i)));
    $("agenda-body").replaceChildren(body);
    if (scrollToToday) { scrollToToday = false; $("agenda-body").querySelector(".today")?.scrollIntoView({ block: "center" }); }
  }

  function edit(event) {
    editing = { id: event.id || "", list: event.list || "", who: [...(event.who || [])], back: window.scrollY };
    $("event-heading").textContent = editing.id ? "Modifier l'événement" : "Nouvel événement";
    $("event-title").value = event.title || "";
    $("event-date").value = event.date || localDate(new Date());
    $("event-end").value = event.end || "";
    $("event-time").value = event.time || "";
    $("event-maybe").checked = !!event.maybe;
    $("event-yearly").checked = !!event.yearly;
    $("event-delete").hidden = !editing.id;
    renderWho();
    render();
    window.scrollTo(0, 0);
    if (!editing.id) $("event-title").focus();
  }
  function renderWho() {
    $("event-who").replaceChildren(...persons(app.config).map((p) => {
      const on = editing.who.includes(p.id);
      const node = el("button", { type: "button", className: "chip who", textContent: (on ? "✓ " : "") + (p.name || "?") });
      node.setAttribute("aria-pressed", String(on));
      node.style.background = p.color;
      node.onclick = () => { editing.who = on ? editing.who.filter((id) => id !== p.id) : [...editing.who, p.id]; renderWho(); };
      return node;
    }));
  }
  function close() {
    const back = editing?.back || 0;
    editing = null; render(); window.scrollTo(0, back);
  }

  $("agenda-form").onsubmit = (e) => {
    e.preventDefault();
    let fields;
    try {
      fields = cleanEvent({ title: $("event-title").value, date: $("event-date").value, end: $("event-end").value, time: $("event-time").value,
        who: editing.who, maybe: $("event-maybe").checked, yearly: $("event-yearly").checked });
    } catch (error) { alert(error.message); return; }
    upsert(app.lists[editing.list || roomiest(app.lists)], editing.id || newId(), fields);
    cursor = fields.date;
    app.changed();
    close();
  };
  $("event-cancel").onclick = close;
  $("event-delete").onclick = () => {
    if (!confirm("Supprimer cet événement ?")) return;
    remove(app.lists[editing.list], editing.id);
    app.changed();
    close();
  };
  for (const b of $("agenda-modes").children) b.onclick = () => { mode = b.dataset.mode; scrollToToday = mode === "month" && cursor === localDate(new Date()); window.scrollTo(0, 0); render(); };
  const move = (n) => {
    cursor = mode === "month" ? addMonths(cursor, n) : addDays(cursor, mode === "week" ? 7 * n : n);
    if (mode === "month" && cursor.slice(0, 7) === localDate(new Date()).slice(0, 7)) cursor = localDate(new Date());
    render(); window.scrollTo(0, 0);
  };
  $("agenda-prev").onclick = () => move(-1);
  $("agenda-next").onclick = () => move(1);
  $("agenda-today").onclick = () => { cursor = localDate(new Date()); scrollToToday = true; render(); };
  $("event-new").onclick = () => edit({ date: mode === "day" ? cursor : localDate(new Date()) });
  pruneEvents(app.lists, localDate(new Date()));
  for (const query of wide) query.addEventListener("change", () => { if (!$("agenda").hidden) { scrollToToday = true; render(); } });

  return {
    render, line, edit,
    open() { cursor = localDate(new Date()); scrollToToday = true; },
    close() { editing = null; },
  };
}
