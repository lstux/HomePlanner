// Carte (Leaflet + tuiles OpenStreetMap) : les lieux du foyer et où sont les proches.
/* global L */
import { $, el, button } from "./dom.js";
import { upsert, remove, alive, newId } from "./lists.js";
import { searchTown } from "./weather.js";

export const KINDS = { home: "Maison", outing: "Où on va", family: "Chez un proche", other: "Autre" };

export function distanceKm(a, b) {
  const rad = (d) => (d * Math.PI) / 180;
  const h = Math.sin(rad(b.lat - a.lat) / 2) ** 2 + Math.cos(rad(a.lat)) * Math.cos(rad(b.lat)) * Math.sin(rad(b.lon - a.lon) / 2) ** 2;
  return 12742 * Math.asin(Math.sqrt(h));
}
export const formatKm = (km) => (km < 1 ? `${Math.round(km * 100) * 10} m` : km < 10 ? `${km.toFixed(1).replace(".", ",")} km` : `${Math.round(km)} km`);
// Lieu où se trouve une personne : « en ce moment » s'il est renseigné, sinon chez elle.
export const whereIs = (contact, places) => places.find((p) => p.id === contact.nowId) || places.find((p) => p.id === contact.homeId) || null;

export function initMap(app) {
  let map, markers, line, adding = false, draft = null, selected = null;
  const places = () => alive(app.lists.places);

  function ensureMap() {
    if (map || !window.L) return;
    map = L.map("map", { zoomControl: true }).setView([46.6, 2.4], 5);
    L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {
      maxZoom: 19, referrerPolicy: "strict-origin-when-cross-origin",
      attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>',
    }).addTo(map);
    markers = L.layerGroup().addTo(map);
    map.on("click", (e) => { if (adding) edit({ lat: e.latlng.lat, lon: e.latlng.wrap().lng }); });
    const all = places();
    if (all.length) map.fitBounds(all.map((p) => [p.lat, p.lon]), { padding: [40, 40], maxZoom: 13 });
  }

  function show(placeId, person) {
    selected = { placeId, personId: person?.id };
    const place = places().find((p) => p.id === placeId);
    if (place && map) {
      const home = places().find((p) => p.kind === "home" && p.id !== place.id);
      line?.remove(); line = null;
      if (home) {
        line = L.polyline([[home.lat, home.lon], [place.lat, place.lon]], { color: "#f28c28", weight: 5, dashArray: "8 10" }).addTo(map);
        map.fitBounds(line.getBounds(), { padding: [50, 50], maxZoom: 15 });
      } else map.setView([place.lat, place.lon], 14);
    }
    card();
    $("map").scrollIntoView({ block: "nearest", behavior: "smooth" });
  }

  function card() {
    const place = places().find((p) => p.id === selected?.placeId);
    $("map-card").hidden = !place;
    if (!place) return;
    const person = alive(app.lists.contacts).find((c) => c.id === selected.personId);
    const here = alive(app.lists.contacts).filter((c) => whereIs(c, places())?.id === place.id);
    const home = places().find((p) => p.kind === "home" && p.id !== place.id);
    $("map-card").replaceChildren(
      el("h2", { textContent: person ? `${person.emoji || "🙂"} ${person.name} : ${place.emoji} ${place.name}` : `${place.emoji} ${place.name}` }),
      home ? el("p", { textContent: `À ${formatKm(distanceKm(home, place))} de la maison, à vol d'oiseau.` }) : null,
      here.length ? el("p", { textContent: "Ici en ce moment : " + here.map((c) => c.name).join(", ") }) : null,
      el("div", { className: "row" },
        el("a", { className: "button secondary", textContent: "Itinéraire", target: "_blank", rel: "noopener",
          href: `https://www.google.com/maps/dir/?api=1&destination=${place.lat},${place.lon}` }),
        button("Modifier", "button secondary", () => edit(place))));
  }

  function edit(place) {
    if (!app.adult()) return;
    adding = false; draft = place;
    $("place-form").hidden = false;
    $("place-name").value = place.name || "";
    $("place-emoji").value = place.emoji || "📍";
    $("place-kind").value = place.kind || (places().some((p) => p.kind === "home") ? "outing" : "home");
    $("place-delete").hidden = !place.id;
    $("map-hint").hidden = true;
    $("place-form").scrollIntoView({ block: "nearest" });
  }
  const close = () => { draft = null; $("place-form").hidden = true; };

  function render() {
    ensureMap();
    const all = places();
    const people = alive(app.lists.contacts).filter((c) => whereIs(c, all)).sort((a, b) => a.name.localeCompare(b.name, "fr"));
    $("map-people").replaceChildren(...people.map((c) => button(`${c.emoji || "🙂"} ${c.name}`, "chip", () => show(whereIs(c, all).id, c))));
    $("map-places").replaceChildren(...all.sort((a, b) => a.name.localeCompare(b.name, "fr")).map((p) => button(`${p.emoji} ${p.name}`, "chip", () => show(p.id))));
    $("map-empty").hidden = all.length > 0;
    if (!map) { $("map").textContent = "Carte indisponible."; return; }
    markers.clearLayers();
    for (const p of all) {
      const who = people.filter((c) => whereIs(c, all).id === p.id).map((c) => c.emoji || "🙂").join("");
      const icon = L.divIcon({ className: "pin", html: el("span", { textContent: p.emoji + who }), iconSize: null });
      L.marker([p.lat, p.lon], { icon, title: p.name }).on("click", () => show(p.id)).addTo(markers);
    }
    card();
    setTimeout(() => map.invalidateSize(), 0);
  }

  $("place-add").onclick = () => { adding = true; $("map-hint").hidden = false; $("place-form").hidden = true; };
  $("place-search").onsubmit = async (event) => {
    event.preventDefault();
    try {
      const [first] = await searchTown($("place-query").value.trim());
      if (!first) { alert("Ville introuvable. Tu peux aussi toucher la carte."); return; }
      map?.setView([first.latitude, first.longitude], 13);
      adding = true; $("map-hint").hidden = false;
    } catch (e) { alert(e.message); }
  };
  $("place-form").onsubmit = (event) => {
    event.preventDefault();
    const name = $("place-name").value.trim();
    if (!name || !draft) return;
    const kind = $("place-kind").value, id = draft.id || newId();
    // Une seule maison.
    if (kind === "home") places().filter((p) => p.kind === "home" && p.id !== id).forEach((p) => upsert(app.lists.places, p.id, { kind: "other" }));
    upsert(app.lists.places, id, { name, kind, emoji: $("place-emoji").value.trim() || "📍",
      lat: Math.round(draft.lat * 1e5) / 1e5, lon: Math.round(draft.lon * 1e5) / 1e5 });
    close(); app.changed(); selected = { placeId: id }; render();
  };
  $("place-cancel").onclick = close;
  $("place-delete").onclick = () => {
    if (!draft?.id || !confirm("Supprimer ce lieu ?")) return;
    remove(app.lists.places, draft.id); selected = null; line?.remove(); close(); app.changed(); render();
  };
  return render;
}
