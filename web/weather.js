// Météo : prévisions Open-Meteo (sans clé), gardées en cache pour le hors ligne.
import { $, el } from "./dom.js";

const CACHE = "home.weather.v1";
const FRESH_MS = 30 * 60 * 1000;

export function describe(code) {
  if (code === 0) return ["☀️", "Grand soleil"];
  if (code <= 2) return ["🌤️", "Quelques nuages"];
  if (code === 3) return ["☁️", "Nuageux"];
  if (code === 45 || code === 48) return ["🌫️", "Brouillard"];
  if (code >= 51 && code <= 57) return ["🌦️", "Bruine"];
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return ["❄️", "Neige"];
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return ["🌧️", "Pluie"];
  if (code >= 95) return ["⛈️", "Orage"];
  return ["🌡️", "Temps variable"];
}

// Conseil d'habillage pour la journée.
export function advice({ code, min, max, rain }) {
  const icon = describe(code)[0];
  if (code >= 95) return { who: "rabbit", mood: "think", text: "Orage possible : on reste à l'abri s'il gronde." };
  if (icon === "❄️") return { who: "fox", mood: "bravo", text: "De la neige ! Bonnet, gants et bottes." };
  if (rain >= 50 || icon === "🌧️") return { who: "fox", mood: "think", text: "Risque de pluie : prends ton manteau de pluie." };
  if (max < 8) return { who: "rabbit", mood: "think", text: "Il fait froid : gros manteau et bonnet." };
  if (min < 12) return { who: "rabbit", mood: "content", text: "Un peu frais : une veste sera parfaite." };
  if (max >= 27) return { who: "fox", mood: "content", text: "Il fait chaud : casquette, et pense à boire !" };
  return { who: "rabbit", mood: "content", text: "Temps agréable, bonne journée !" };
}

export async function searchTown(name) {
  const url = `https://geocoding-api.open-meteo.com/v1/search?count=5&language=fr&name=${encodeURIComponent(name)}`;
  const response = await fetch(url, { signal: AbortSignal.timeout(10000) });
  if (!response.ok) throw new Error("Recherche indisponible");
  return (await response.json()).results || [];
}

function read() { try { return JSON.parse(localStorage.getItem(CACHE)); } catch { return null; } }

export function initWeather(app) {
  let loading = false, lastTry = 0, error = "";
  const place = () => app.config.weather;
  const cached = () => { const c = read(), p = place(); return c && p && c.lat === p.lat && c.lon === p.lon ? c : null; };

  async function refresh(force) {
    const p = place(), c = cached();
    if (!p || loading || (!force && c && Date.now() - c.fetched < FRESH_MS) || (!force && Date.now() - lastTry < 60000)) return;
    loading = true; lastTry = Date.now();
    try {
      const url = `https://api.open-meteo.com/v1/forecast?latitude=${p.lat}&longitude=${p.lon}`
        + "&current=temperature_2m,apparent_temperature,weather_code"
        + "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=auto&forecast_days=4";
      const response = await fetch(url, { signal: AbortSignal.timeout(10000) });
      if (!response.ok) throw new Error();
      const data = await response.json();
      localStorage.setItem(CACHE, JSON.stringify({ lat: p.lat, lon: p.lon, fetched: Date.now(), data }));
      error = "";
    } catch { error = "Météo indisponible pour le moment."; }
    loading = false;
    render();
  }

  // Journée d'aujourd'hui dans la prévision en cache (undefined si trop ancienne).
  function days() {
    const d = cached()?.data.daily;
    if (!d) return [];
    const today = new Date().toLocaleDateString("sv-SE");
    return d.time.map((date, i) => ({ date, code: d.weather_code[i], max: d.temperature_2m_max[i], min: d.temperature_2m_min[i], rain: d.precipitation_probability_max[i] ?? 0 }))
      .filter((day) => day.date >= today);
  }

  function render() {
    const p = place(), c = cached(), list = days();
    const chip = $("weather-chip");
    chip.hidden = !list.length;
    if (list.length) chip.textContent = `${describe(list[0].code)[0]} ${Math.round(list[0].min)}° / ${Math.round(list[0].max)}° · ${advice(list[0]).text}`;
    $("weather-place").textContent = p ? p.name : "Choisis une ville";
    $("weather-error").textContent = error;
    $("weather-body").hidden = !list.length;
    if (!list.length) return;
    const tip = advice(list[0]);
    $("weather-mascot").src = `./mascots/${tip.who}-${tip.mood}.webp`;
    $("weather-advice").textContent = tip.text;
    const now = c.data.current;
    const recent = Date.now() - c.fetched < 3 * 3600 * 1000;
    $("weather-now").textContent = recent ? `${describe(now.weather_code)[0]} ${Math.round(now.temperature_2m)}°` : describe(list[0].code)[0];
    $("weather-feel").textContent = recent ? `${describe(now.weather_code)[1]} · ressenti ${Math.round(now.apparent_temperature)}°`
      : "Prévision enregistrée le " + new Date(c.fetched).toLocaleString("fr-FR");
    $("weather-days").replaceChildren(...list.map((day, i) => el("li", {},
      el("span", { textContent: i === 0 ? "Aujourd'hui" : new Date(day.date + "T12:00").toLocaleDateString("fr-FR", { weekday: "long" }) }),
      el("span", { className: "day-icon", textContent: describe(day.code)[0], title: describe(day.code)[1] }),
      el("span", { textContent: `💧 ${day.rain} %` }),
      el("strong", { textContent: `${Math.round(day.min)}° / ${Math.round(day.max)}°` }))));
  }

  function choose(name, lat, lon) {
    if (!app.adult()) return;
    // Position arrondie : une précision d'environ 1 km suffit à la météo.
    app.config.weather = { name, lat: Math.round(lat * 100) / 100, lon: Math.round(lon * 100) / 100 };
    app.configChanged();
    $("town-results").replaceChildren();
    $("town-input").value = "";
    refresh(true); render();
  }

  $("town-form").onsubmit = async (event) => {
    event.preventDefault();
    const name = $("town-input").value.trim();
    if (!name) return;
    try {
      const results = await searchTown(name);
      error = results.length ? "" : "Aucune ville trouvée.";
      $("town-results").replaceChildren(...results.map((r) => el("li", {}, el("button", { type: "button", className: "button secondary",
        textContent: [r.name, r.admin1, r.country].filter(Boolean).join(", "), onclick: () => choose(r.name, r.latitude, r.longitude) }))));
    } catch (e) { error = e.message; }
    render();
  };
  $("locate").onclick = () => navigator.geolocation?.getCurrentPosition(
    (pos) => choose("Ma position", pos.coords.latitude, pos.coords.longitude),
    () => { error = "Position indisponible."; render(); }, { timeout: 10000 });

  return { render, refresh };
}
