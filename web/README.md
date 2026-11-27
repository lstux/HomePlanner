# PWA Home

Application statique, sans étape de build : servir ce dossier sur un site **HTTPS** (GitHub Pages assemble aussi le flasher avec `tools/build_site.sh`).

## Déploiement GitHub Pages

1. Dépôt GitHub → **Settings → Pages → Build and deployment → Source : GitHub Actions**.
2. **Actions → Deploy PWA to GitHub Pages → Run workflow** (ensuite automatique à chaque modification de `web/` sur `main`).
3. Adresse par défaut : `https://lstux.github.io/HomePlanner/`.

## Fonctionnalités

- Routines matin / soir / vacances, proposées selon le jour et l'heure.
- Étapes à cocher, compte à rebours, état (avance, juste, retard).
- Page d'accueil entre deux routines (heure, mascotte du moment, météo du jour et du lendemain, poubelles, courses, prochaine routine) ; la routine s'affiche d'elle-même d'une heure avant l'échéance, ou dès la première case cochée, à 15 min après.
- Réactions du renard et du lapin ; idées d'activités calmes (sans écran) s'il reste de la marge.
- Plusieurs enfants, routines modifiables.
- Courses : liste partagée, cochable par tout le monde.
- Météo ([Open-Meteo](https://open-meteo.com/), usage non commercial) avec conseil d'habillage ; la position de la ville (arrondie à environ 1 km) est envoyée à Open-Meteo.
- Soir : une étape peut avoir une heure fixe (« À table » à 19 h), décalable pour ce soir par un adulte ; la jauge vise cette heure puis l'heure de fin. Une étape « temps libre » (« Quartier libre ») dure ce qui reste avant le prochain repère.
- Mode adulte : code facultatif devant les réglages, la remise à zéro, la ville, les lieux et les contacts (garde-fou contre les manipulations d'enfant, pas une sécurité). Un code par adulte ; code oublié, un autre adulte en redonne un, sinon effacement après 24 h (annulable).
- Sauvegarde dans un fichier (⚙️) : réglages, routines et listes, sans les codes adultes ni le code d'appairage ; la restauration remplace les réglages et ramène les éléments du fichier.
- Poubelles : tri un jeudi sur deux et reste chaque mardi (pré-rempli, réglable), rappel la veille et « C'est sorti » partagé ; jours fériés décalés d'un jour par défaut, chaque date ajustable (la règle réelle dépend de la commune). Rappel visible à l'ouverture de l'app, sans notification.
- Carte (Leaflet 1.9.4, fonds au choix : [OpenStreetMap](https://operations.osmfoundation.org/policies/tiles/), plan et photos IGN, OpenTopoMap ; connexion nécessaire) : maison, sorties, « où est mamie ? ». Lieux placés à la main ou par recherche d'adresse (Géoplateforme IGN, envoyée seulement sur demande), aucun suivi.
- Contacts du foyer et des proches, avec bouton d'appel.
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
