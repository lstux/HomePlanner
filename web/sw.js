const CACHE = "homeplanner-pwa-v6";
const ASSETS = ["./", "./index.html", "./styles.css", "./app.js", "./routine.js", "./mascots.js", "./store.js", "./data.js", "./central.js", "./sync.js",
  "./dom.js", "./lists.js", "./lock.js", "./shopping.js", "./weather.js", "./map.js", "./contacts.js",
  "./vendor/leaflet/leaflet.css", "./vendor/leaflet/leaflet.js",
  "./manifest.webmanifest", "./icons/icon.svg", "./icons/icon-192.png", "./icons/icon-512.png", "./mascots/duo-walk.webp",
  ...["fox", "rabbit"].flatMap((who) => ["content", "bravo", "think", "comfort"].map((mood) => `./mascots/${who}-${mood}.webp`))];

self.addEventListener("install", (event) => event.waitUntil(
  caches.open(CACHE).then((cache) => cache.addAll(ASSETS.map((url) => new Request(url, { cache: "reload" })))).then(() => self.skipWaiting())));
self.addEventListener("activate", (event) => event.waitUntil(
  caches.keys().then((keys) => Promise.all(keys.filter((k) => k.startsWith("homeplanner-") && k !== CACHE).map((k) => caches.delete(k)))).then(() => self.clients.claim())));

// Réseau d'abord (mises à jour immédiates), cache si hors ligne.
self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  if (event.request.method !== "GET" || url.origin !== self.location.origin) return;
  event.respondWith(fetch(event.request).then((response) => {
    if (response.ok) { const copy = response.clone(); event.waitUntil(caches.open(CACHE).then((c) => c.put(event.request, copy))); }
    return response;
  }).catch(async () => (await caches.match(event.request, { cacheName: CACHE, ignoreSearch: true }))
    || (event.request.mode === "navigate" ? caches.match("./index.html", { cacheName: CACHE }) : Response.error())));
});
