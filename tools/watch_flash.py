#!/usr/bin/env python3
"""Prépare les fichiers du flasher web de la montre à partir du journal d'un envoi PlatformIO.

Usage : watch_flash.py JOURNAL DOSSIER_PROJET SORTIE

La commande esptool affichée par `pio run -v -t upload` donne exactement les fichiers, adresses et
réglages de flash qu'un envoi par câble utiliserait.
"""
import json, os, re, shlex, shutil, subprocess, sys

FLASH_SIZE = "16MB"  # T-Watch 2020 (esptool « detect » en envoi par câble)
APP, BOOT = 0x10000, 0x1000


def parse(log):
    lines = [l for l in log.splitlines() if "write_flash" in l and "--flash_mode" in l]
    if not lines:
        sys.exit("Commande write_flash introuvable dans le journal")
    words = shlex.split(lines[-1])
    option = lambda name: words[words.index(name) + 1]
    images = {int(a, 16): b for a, b in zip(words, words[1:])
              if re.fullmatch(r"0x[0-9a-fA-F]+", a) and b.endswith(".bin")}
    if sorted(images) != [BOOT, 0x8000, 0xE000, APP]:
        sys.exit("Images inattendues : " + ", ".join(map(hex, sorted(images))))
    return option("--flash_mode"), option("--flash_freq"), images


def main(journal, project, out):
    mode, freq, images = parse(open(journal, encoding="utf-8", errors="replace").read())
    os.makedirs(out, exist_ok=True)
    parts = []
    for offset, path in sorted(images.items()):
        source = path if os.path.isabs(path) else os.path.join(project, path)
        name = {BOOT: "bootloader.bin", 0x8000: "partitions.bin", 0xE000: "boot_app0.bin", APP: "firmware.bin"}[offset]
        target = os.path.join(out, name)
        if offset == BOOT:
            # L'en-tête du chargeur porte mode, fréquence et taille : le navigateur ne les corrige pas.
            subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32", "merge_bin", "--target-offset", hex(BOOT),
                            "--flash_mode", mode, "--flash_freq", freq, "--flash_size", FLASH_SIZE,
                            "-o", target, hex(BOOT), source], check=True)
        else:
            shutil.copyfile(source, target)
        parts.append({"path": name, "offset": offset})
    print(f"flash {mode} {freq} {FLASH_SIZE}")
    return parts


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    journal, project, out = sys.argv[1:]
    parts = main(journal, project, out)
    env = os.path.basename(os.path.normpath(out))
    manifest = {"name": f"Home - {env}", "version": os.environ.get("VERSION", "dev"),
                "new_install_prompt_erase": True, "builds": [{"chipFamily": "ESP32", "parts": parts}]}
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
