# App Home pour la T-Watch 2020

Routine du jour au poignet : étapes à cocher, compte à rebours, renard et lapin, synchronisation
avec la centrale ESP32-C6. Comme l'app Slovingo, elle se greffe sur le firmware
[T-Watch-2020](https://github.com/lstux/T-Watch-2020) sans le modifier.

**Statut : compilée en CI (v1, v2, v3), jamais essayée sur une montre.**

## Installer depuis le navigateur

Sur un ordinateur avec Chrome ou Edge : <https://lstux.github.io/HomePlanner/flash/watch/>. Faire la sauvegarde
ci-dessous avant la première fois, et laisser « Erase device » décochée.

Le site compile l'app avec le fork `lstux/T-Watch-2020` ; après un changement dans le fork seul, relancer
le workflow « Deploy PWA to GitHub Pages ».

## Installer en ligne de commande

```bash
pip install platformio
git clone https://github.com/lstux/T-Watch-2020.git
git clone https://github.com/lstux/HomePlanner.git
HomePlanner/platform/twatch/install.sh T-Watch-2020
cd T-Watch-2020
pio run -e t-watch2020-v1 -t upload     # remplace v1 par v2 ou v3 selon la montre
```

Sauvegarde conseillée avant le premier flash :

```bash
python ~/.platformio/packages/tool-esptoolpy/esptool.py --port <PORT> --baud 921600 read_flash 0 0x1000000 sauvegarde-twatch.bin
```

## Régler

1. Réglages Wi-Fi de la montre, et heure à jour (synchronisation NTP).
2. App **home** → roue dentée : adresse de la centrale (`http://homeplanner.local`, ou l'adresse IP si le nom
   ne répond pas) et code affiché sur son écran.
3. Flèche de retour : enregistre et synchronise.

Ou préparer `data/homeplanner.json` d'après `conf/homeplanner.json.example`, puis
`pio run -e t-watch2020-v1 -t uploadfs`. Attention : `uploadfs` réécrit **toute** la mémoire de la montre.

## Utiliser

- Toucher une étape la coche ou la décoche (envoi après quelques secondes) ; l'ordre est libre.
- Un appui long sur une étape lance son minuteur (« je fais celle-là ») ; sinon « C'est parti » lance
  la première, et chaque étape validée lance la suivante. La flèche » montre l'étape en cours.
- Soir : le compte à rebours vise l'heure fixe du repas (décalée depuis un téléphone), et le temps libre
  affiche ce qui reste avant.
- Jauge : au milieu « pile poil », à droite l'avance, à gauche le retard (±15 min).
- Bouton du haut : changer de routine ; bouton du bas (s'il y a plusieurs enfants) : changer d'enfant.
- La montre garde réglages et coches pour fonctionner sans la maison ; elle ne modifie jamais les réglages.

## Développement

`hp_core` (routines, marques, synchronisation) ne dépend pas du matériel :

```bash
g++ -std=c++11 -I<dossier de ArduinoJson.h 6.21> -Ihomeplanner tests/core_tests.cpp homeplanner/hp_core.cpp -o core_tests && ./core_tests
python3 tools/gen_assets.py   # regénère polices et images
```
