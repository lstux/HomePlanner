#!/usr/bin/env bash
# Tests des écrans sur ordinateur, pour chaque carte. Usage : run_screen_tests.sh <dossier de ArduinoJson.h> [dossier d'images]
set -euo pipefail
cd "$(dirname "$0")/.."
g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined -Isrc tests/pairing_tests.cpp -o /tmp/pairing_tests && /tmp/pairing_tests
for board in boards/*/; do
  name="$(basename "$board")"
  g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined -I"$1" -Isrc -I"$board" \
    tests/screen_tests.cpp src/screen.cpp src/screen_wide.cpp src/draw.cpp src/fonts.cpp src/life.cpp src/hp_core_link.cpp -o "/tmp/screen_tests_$name"
  if [ -n "${2:-}" ]; then mkdir -p "$2/$name"; "/tmp/screen_tests_$name" "$2/$name"; else "/tmp/screen_tests_$name"; fi
done
