# PWA Home

Application statique, sans étape de build : déployer le contenu de ce dossier à la racine d'un site **HTTPS**.

## Déploiement GitHub Pages

1. Dépôt GitHub → **Settings → Pages → Build and deployment → Source : GitHub Actions**.
2. **Actions → Deploy PWA to GitHub Pages → Run workflow** (ensuite automatique à chaque modification de `web/` sur `main`).
3. Adresse par défaut : `https://lstux.github.io/HomePlanner/`.

## Fonctionnalités

- Routines matin / soir / vacances, proposées selon le jour et l'heure.
- Étapes à cocher, compte à rebours, état (avance, juste, retard).
- Réactions du renard et du lapin ; idées d'activités calmes (sans écran) s'il reste de la marge.
- Plusieurs enfants, routines modifiables.
- Fonctionne hors ligne après la première visite ; installable sur Android.

## Synchronisation (firmware 0.3.0)

Dans ⚙️ → « Synchronisation avec la maison » : adresse et code affichés sur l'écran de la C6. Chrome Android demande l'autorisation d'accéder au réseau local : accepter.

- Progression : fusion étape par étape, la dernière action gagne (les horloges des téléphones doivent être à l'heure).
- Réglages : si deux appareils les modifient avant de s'être synchronisés, ceux de la centrale gagnent.
- Écart avec [le protocole](../docs/synchronization-protocol.md) : pas de file de commandes séparée (l'état local en tient lieu), instantané complet au lieu du flux `pull`.

## Limites

- Sans centrale, les données restent propres à chaque appareil.
- Réglages non protégés (pas de mode adulte).
- La liaison avec la C6 n'a pas été essayée sur le matériel.
- Le code d'appairage circule en HTTP : réseau domestique de confiance uniquement.

## Développement

    python3 -m http.server 8000 -d web
    node --test web/tests/*.test.mjs
