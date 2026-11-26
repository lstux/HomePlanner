// Fonds de carte proposés ; le choix est propre à chaque appareil.
const ign = (layer, format) => "https://data.geopf.fr/wmts?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0&STYLE=normal&TILEMATRIXSET=PM"
  + `&LAYER=${layer}&FORMAT=image/${format}&TILEMATRIX={z}&TILEROW={y}&TILECOL={x}`;
const OSM = '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>';

export const BASES = {
  osm: { name: "OpenStreetMap", url: "https://tile.openstreetmap.org/{z}/{x}/{y}.png", options: { attribution: OSM } },
  plan: { name: "Plan IGN", url: ign("GEOGRAPHICALGRIDSYSTEMS.PLANIGNV2", "png"), options: { attribution: "Plan IGN v2 &copy; IGN" } },
  photo: { name: "Photos aériennes", url: ign("ORTHOIMAGERY.ORTHOPHOTOS", "jpeg"), options: { attribution: "Photographies aériennes &copy; IGN" } },
  topo: { name: "Relief (OpenTopoMap)", url: "https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png",
    options: { maxNativeZoom: 17, attribution: `${OSM}, SRTM | <a href="https://opentopomap.org">opentopomap.org</a> (CC-BY-SA)` } },
};
const KEY = "home.mapbase";
export function savedBase() {
  try { const id = localStorage.getItem(KEY); return Object.hasOwn(BASES, id) ? id : "osm"; } catch { return "osm"; }
}
export function saveBase(id) { try { localStorage.setItem(KEY, id); } catch { /* choix non mémorisé */ } }
