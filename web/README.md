# PWA Home

Application statique, sans étape de build : servir ce dossier sur un site **HTTPS** (GitHub Pages assemble aussi le flasher avec `tools/build_site.sh`).

## Déploiement GitHub Pages

1. Dépôt GitHub → **Settings → Pages → Build and deployment → Source : GitHub Actions**.
2. **Actions → Deploy PWA to GitHub Pages → Run workflow** (ensuite automatique à chaque modification de `web/` sur `main`).
3. Adresse par défaut : `https://lstux.github.io/HomePlanner/`.

## Fonctionnalités

- Routines matin / soir / vacances, proposées selon le jour et l'heure.
- Étapes à cocher, compte à rebours, état (avance, juste, retard).
- Réactions du renard et du lapin ; idées d'activités calmes (sans écran) s'il reste de la marge.
- Plusieurs enfants, routines modifiables.
- Courses : liste partagée, cochable par tout le monde.
- Météo ([Open-Meteo](https://open-meteo.com/), usage non commercial) avec conseil d'habillage ; la position de la ville (arrondie à environ 1 km) est envoyée à Open-Meteo.
- Carte (Leaflet 1.9.4, fonds au choix : [OpenStreetMap](https://operations.osmfoundation.org/policies/tiles/), plan et photos IGN, OpenTopoMap ; connexion nécessaire) : maison, sorties, « où est mamie ? ». Lieux placés à la main ou par recherche d'adresse (Géoplateforme IGN, envoyée seulement sur demande), aucun suivi.
- Contacts du foyer et des proches, avec bouton d'appel.
- Mode adulte : code facultatif devant les réglages, la remise à zéro, la ville, les lieux et les contacts. Garde-fou contre les manipulations d'enfant, pas une sécurité.
- Fonctionne hors ligne après la première visite ; installable sur Android.
- `flash/` : installation du firmware de la centrale depuis le navigateur.

## Synchronisation (firmware 0.4.0)

Dans ⚙️ → « Synchronisation avec la maison » : adresse et code affichés sur l'écran de la C6. Chrome Android demande l'autorisation d'accéder au réseau local : accepter.

- Progression : fusion étape par étape, la dernière action gagne (les horloges des téléphones doivent être à l'heure).
- Réglages : si deux appareils les modifient avant de s'être synchronisés, ceux de la centrale gagnent.
- Courses, contacts, lieux : fusion élément par élément (12 Kio maximum par liste, soit quelques dizaines de contacts). Les suppressions sont oubliées après 30 jours : un appareil resté éteint plus longtemps peut faire réapparaître un élément.
- Écart avec [le protocole](../docs/synchronization-protocol.md) : pas de file de commandes séparée (l'état local en tient lieu), instantané complet au lieu du flux `pull`.

## Limites

- Sans centrale, les données restent propres à chaque appareil.
- Contacts et lieux sont stockés en clair sur les téléphones et la centrale. Ne jamais mettre de vraies données dans ce dépôt public.
- La liaison avec la C6 n'a pas été essayée sur le matériel.
- Le code d'appairage circule en HTTP : réseau domestique de confiance uniquement.

## Développement

    python3 -m http.server 8000 -d web
    node --test web/tests/*.test.mjs
