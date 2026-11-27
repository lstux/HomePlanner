#!/usr/bin/env bash
# Assemble le site GitHub Pages dans _site/ : PWA + flashers web (centrale et montre).
# Prérequis : firmwares compilés (pio run dans platform/central) et npm.
set -euo pipefail
cd "$(dirname "$0")/.."
fw=platform/central
core="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"

rm -rf _site
mkdir _site
cp -r web/. _site/
rm -r _site/tests
rm _site/README.md _site/mascots/README.md

boot_app0="$(find "$core/packages" -path '*/tools/partitions/boot_app0.bin' | head -n1)"
[ -n "$boot_app0" ] || { echo "boot_app0.bin introuvable" >&2; exit 1; }
tmp="$(mktemp -d)"
npm install --prefix "$tmp" --no-save --ignore-scripts esp-web-tools@10.5.2 >/dev/null
cp -r "$tmp/node_modules/esp-web-tools/dist/web" _site/flash/esp-web-tools
cp "$tmp/node_modules/esp-web-tools/LICENSE" _site/flash/esp-web-tools/LICENSE 2>/dev/null || true
rm -rf "${tmp:?}"

version="$(grep -o 'HP_FIRMWARE "[^"]*"' "$fw/src/version.h" | cut -d'"' -f2)"
[ -n "$version" ] || { echo "version du firmware introuvable" >&2; exit 1; }
# Un flasheur par carte. Parties séparées : une mise à jour sans effacement conserve la NVS (Wi-Fi, code).
# Arguments : carte, dossier du site, nom, famille de puce, adresse du bootloader.
central() {
  local build="$fw/.pio/build/$1" dir="_site/$2"
  mkdir -p "$dir"
  cp "$build/bootloader.bin" "$build/partitions.bin" "$build/firmware.bin" "$dir/"
  cp "$boot_app0" "$dir/boot_app0.bin"
  cat > "$dir/manifest.json" <<JSON
{
  "name": "$3",
  "version": "$version",
  "new_install_prompt_erase": true,
  "builds": [{
    "chipFamily": "$4",
    "parts": [
      { "path": "bootloader.bin", "offset": $5 },
      { "path": "partitions.bin", "offset": 32768 },
      { "path": "boot_app0.bin", "offset": 57344 },
      { "path": "firmware.bin", "offset": 65536 }
    ]
  }]
}
JSON
}
central waveshare-c6 flash "Home - centrale ESP32-C6" ESP32-C6 0
central lilygo-t-hmi flash/thmi "Home - centrale T-HMI" ESP32-S3 0
# Montre : fichiers préparés par tools/watch_flash.py, un dossier par version.
if [ -d _watch ]; then
  cp -r _watch/. _site/flash/watch/
fi

echo "Site prêt dans _site/ (firmware $version ; montre : $(ls -d _site/flash/watch/*/ 2>/dev/null | wc -l) version(s))"
