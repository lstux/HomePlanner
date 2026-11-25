#!/usr/bin/env bash
# Ajoute l'app Home au firmware T-Watch (fork lstux/T-Watch-2020 ou d03n3rfr1tz3/TTGO.T-Watch.2020).
#
# Usage : ./install.sh /chemin/vers/T-Watch-2020
#
#   1. copie homeplanner/ -> <firmware>/src/app/homeplanner
#   2. copie conf/homeplanner.json.example -> <firmware>/conf/
#   3. déclare l'app dans NOT_ESSENTIAL de support/apps_off.py (cartes à petite flash)
#   4. ajoute data/*.json à .gitignore (mots de passe et code d'appairage)
# Il peut être relancé sans risque (pour mettre l'app à jour).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fw="${1:-}"

if [ -z "$fw" ] || [ ! -f "$fw/platformio.ini" ] || [ ! -d "$fw/src/app" ]; then
    echo "Usage : $0 /chemin/vers/T-Watch-2020 (dossier contenant platformio.ini)" >&2
    exit 1
fi

mkdir -p "$fw/src/app/homeplanner" "$fw/conf"
cp -r "$here/homeplanner/." "$fw/src/app/homeplanner/"
cp "$here/conf/homeplanner.json.example" "$fw/conf/"

apps="$fw/support/apps_off.py"
if [ -f "$apps" ] && ! grep -q '"app/homeplanner"' "$apps"; then
    python3 - "$apps" <<'PY'
import sys

path = sys.argv[1]
text = open(path, encoding="utf-8").read()
marker = "NOT_ESSENTIAL = [\n"
if marker not in text:
    sys.exit("NOT_ESSENTIAL introuvable dans " + path)
text = text.replace(marker, marker + '    "app/homeplanner",\n', 1)
open(path, "w", encoding="utf-8").write(text)
PY
fi

if ! grep -qx 'data/\*.json' "$fw/.gitignore" 2>/dev/null; then
    printf '\ndata/*.json\n' >> "$fw/.gitignore"
fi

echo "OK : app Home installée dans $fw/src/app/homeplanner"
