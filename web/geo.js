// Recherche d'adresses et de villes. Rien n'est envoyé avant que l'on touche « Chercher ».
//  - France : service de géocodage de la Géoplateforme IGN (Base Adresse Nationale) ;
//  - villes (surtout à l'étranger) : Open-Meteo, uniquement pour une saisie sans chiffre.
import { searchTown } from "./weather.js";

export function parseAddresses(json, minScore = 0.5) {
  return (json?.features || []).flatMap((f) => {
    const [lon, lat] = f.geometry?.coordinates || [];
    const p = f.properties || {};
    return Number.isFinite(lat) && Number.isFinite(lon) && p.label && (p.score ?? 1) >= minScore
      ? [{ label: p.context && p.type === "municipality" ? `${p.label} (${p.context})` : p.label, lat, lon }] : [];
  });
}

async function searchFrance(query) {
  const url = `https://data.geopf.fr/geocodage/search?limit=5&q=${encodeURIComponent(query)}`;
  const response = await fetch(url, { signal: AbortSignal.timeout(10000) });
  if (!response.ok) throw new Error("Recherche indisponible");
  return parseAddresses(await response.json());
}
const towns = async (query) => (await searchTown(query)).map((r) => ({
  label: [r.name, r.admin1, r.country].filter(Boolean).join(", "), lat: r.latitude, lon: r.longitude }));

export async function searchPlaces(query) {
  const sources = /\d/.test(query) ? [searchFrance] : [towns, searchFrance];
  const answers = await Promise.allSettled(sources.map((search) => search(query)));
  if (answers.every((a) => a.status === "rejected")) throw new Error("Recherche indisponible : vérifie la connexion.");
  return answers.flatMap((a) => a.value || []).slice(0, 8);
}
