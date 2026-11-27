// Page d'accueil : l'essentiel de la maison entre deux routines.
import { $, el } from "./dom.js";
import { alive } from "./lists.js";
import { describe, advice } from "./weather.js";
import { greeting, nextRoutine } from "./moments.js";
import { allEvents, persons, upcoming, localDate } from "./events.js";
import { addDays } from "./bins.js";
import { dayName } from "./agenda.js";

const hour = (time) => time.replace(/^0/, "").replace(":", " h ");

export function initDashboard(app, { weather, bins, agenda, open }) {
  function render() {
    const now = new Date(), hello = greeting(app.config, now);
    $("dash-time").textContent = now.toLocaleTimeString("fr-FR", { hour: "2-digit", minute: "2-digit" });
    $("dash-date").textContent = now.toLocaleDateString("fr-FR", { weekday: "long", day: "numeric", month: "long" });
    const image = `./mascots/${hello.who}-${hello.mood}.webp`;
    if ($("dash-mascot").getAttribute("src") !== image) $("dash-mascot").src = image;
    $("dash-speech").textContent = hello.text;
    $("dashboard").dataset.night = String(hello.night);

    const days = weather.days().slice(0, 2);
    $("dash-weather-days").replaceChildren(...days.map((d, i) => el("span", { className: "dash-day" },
      el("small", { textContent: i ? "Demain" : "Aujourd'hui" }),
      el("span", { className: "day-icon", textContent: describe(d.code)[0], title: describe(d.code)[1] }),
      el("strong", { textContent: `${Math.round(d.min)}° / ${Math.round(d.max)}°` }))));
    // Le soir, le conseil concerne le lendemain : on prépare les affaires.
    const later = now.getHours() >= 17 && days[1];
    $("dash-weather-tip").textContent = days.length ? `${later ? "Demain : " : ""}${advice(later ? days[1] : days[0]).text}`
      : app.config.weather ? "Météo indisponible pour le moment." : "Choisir la ville de la météo";

    const due = bins.status();
    $("dash-bins").hidden = !due;
    if (due) {
      $("dash-bins-title").textContent = due.title;
      $("dash-bins-note").textContent = due.note;
      $("dash-bins-out").onclick = () => { due.toggle(); render(); };
    }

    const today = localDate(now), people = persons(app.config);
    const soon = upcoming(allEvents(app.lists), now);
    const heading = (date) => (date === today ? "Aujourd'hui" : date === addDays(today, 1) ? "Demain" : dayName(date));
    $("dash-agenda-list").replaceChildren(...soon.flatMap((day) => [
      el("small", { className: "dash-date", textContent: heading(day.date) }),
      ...day.items.map((o) => agenda.line(o, people, () => open("agenda")))]));
    if (!soon.length) $("dash-agenda-list").replaceChildren(el("p", { className: "muted", textContent: "Rien de prévu." }));

    const items = alive(app.lists.shopping).filter((i) => !i.done).map((i) => i.name).sort((a, b) => a.localeCompare(b, "fr"));
    $("dash-shopping").hidden = !items.length;
    $("dash-shopping-text").textContent = `${items.length} article${items.length > 1 ? "s" : ""} : ${items.slice(0, 5).join(", ")}${items.length > 5 ? "…" : ""}`;

    const next = nextRoutine(app.config, now);
    $("dash-routine").hidden = !next;
    if (next) $("dash-routine-text").textContent =
      `${next.tomorrow ? "Demain" : "Aujourd'hui"} : ${next.routine.icon} ${next.routine.name} · ${next.routine.deadlineLabel.toLowerCase()} à ${hour(next.routine.deadline)}`;
  }
  $("dash-weather").onclick = () => open("weather");
  $("dash-shopping").onclick = () => open("shopping");
  $("dash-routine").onclick = () => open("home");
  $("dash-bins-dates").onclick = $("dash-bins-link").onclick = () => open("bins");
  $("dash-agenda-open").onclick = () => open("agenda");
  $("dash-agenda-add").onclick = () => { open("agenda"); agenda.edit({ date: localDate(new Date()) }); };
  return render;
}
