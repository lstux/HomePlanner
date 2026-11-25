# PWA Home

Application statique, sans étape de build : déployer le contenu de ce dossier à la racine d'un site **HTTPS**.

## Fonctionnalités

- Routines matin / soir / vacances, proposées selon le jour et l'heure.
- Étapes à cocher, compte à rebours, état (avance, juste, retard).
- Réactions du renard et du lapin ; idées d'activités calmes (sans écran) s'il reste de la marge.
- Plusieurs enfants, routines modifiables.
- Fonctionne hors ligne après la première visite ; installable sur Android.

## Limites

- Données propres à chaque appareil : aucune synchronisation avec la C6 pour l'instant.
- Réglages non protégés (pas de mode adulte).
- La liaison C6 (réglages, « Centrale C6 ») n'a pas été essayée sur le matériel.

## Développement

    python3 -m http.server 8000 -d web
    node --test web/tests/*.test.mjs
