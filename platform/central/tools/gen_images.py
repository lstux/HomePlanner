#!/usr/bin/env python3
"""Génère src/images.h : mascottes de la PWA pour l'écran de la centrale (palette + RLE).

Usage : python3 tools/gen_images.py   (nécessite Pillow et imagequant)
"""
import math
from pathlib import Path
import imagequant
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
MASCOTS = HERE.parents[2] / "web" / "mascots"
SIZE = 150
NAMES = [f"{who}-{mood}" for who in ("fox", "rabbit") for mood in ("bravo", "content", "think", "comfort")]
SKIES = ["sun", "partly", "cloud", "fog", "rain", "snow", "storm"]
ICON = 120


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rle(pixels):
    out, i = bytearray(), 0
    while i < len(pixels):
        n = 1
        while n < 255 and i + n < len(pixels) and pixels[i + n] == pixels[i]:
            n += 1
        out += bytes((n, pixels[i]))
        i += n
    return out


def array(values, width):
    return ",\n".join("  " + ",".join(values[i:i + width]) for i in range(0, len(values), width))


def encode(name, picture):
    """Palette de couleurs avec transparence : le fond (jour ou nuit) est ajouté à l'affichage."""
    indexed = imagequant.quantize_pil_image(picture, dithering_level=0.0, max_colors=128)
    raw = indexed.getpalette("RGBA")
    entries = [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]
    colors = [f"0x{rgb565(r, g, b):04x}" for r, g, b, _ in entries]
    alphas = [str(a) for _, _, _, a in entries]
    data = rle(indexed.tobytes())
    ident = name.replace("-", "_")
    return (f"static const uint16_t {ident}_palette[] = {{\n{array(colors, 12)}}};\n"
            f"static const uint8_t {ident}_alpha[] = {{\n{array(alphas, 24)}}};\n"
            f"static const uint8_t {ident}_data[] = {{\n{array([str(b) for b in data], 24)}}};\n"
            f"static const HpImage img_{ident} = {{{picture.width}, {picture.height}, {ident}_palette, {ident}_alpha, {ident}_data, {len(data)}}};\n"), len(data)


def weather_icon(kind):
    """Icônes dessinées ici (formes simples), en quatre fois la taille puis réduites."""
    k = 4
    picture = Image.new("RGBA", (ICON * k, ICON * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(picture)
    box = lambda x0, y0, x1, y1: [x0 * k, y0 * k, x1 * k, y1 * k]
    line = lambda points, color, width: d.line([(x * k, y * k) for x, y in points], fill=color, width=width * k)

    def sun(cx, cy, r):
        for i in range(8):
            a = math.pi * i / 4
            line([(cx + math.cos(a) * r * 1.35, cy + math.sin(a) * r * 1.35), (cx + math.cos(a) * r * 1.8, cy + math.sin(a) * r * 1.8)], (255, 205, 60, 255), 6)
        d.ellipse(box(cx - r, cy - r, cx + r, cy + r), fill=(255, 205, 60, 255))

    def cloud(dy, color):
        d.ellipse(box(14, 46 + dy, 58, 90 + dy), fill=color)
        d.ellipse(box(34, 24 + dy, 86, 76 + dy), fill=color)
        d.ellipse(box(64, 44 + dy, 108, 90 + dy), fill=color)
        d.rectangle(box(36, 66 + dy, 88, 90 + dy), fill=color)

    light, dark = (225, 232, 242, 255), (150, 164, 186, 255)
    if kind == "sun":
        sun(60, 60, 28)
    elif kind == "partly":
        sun(78, 40, 21)
        cloud(10, light)
    elif kind == "cloud":
        cloud(2, light)
    elif kind == "fog":
        cloud(-18, dark)
        for y, x0, x1 in ((84, 16, 96), (98, 30, 106), (112, 20, 86)):
            line([(x0, y), (x1, y)], light, 6)
    else:
        cloud(-16, dark)
        if kind == "rain":
            for x in (36, 60, 84):
                line([(x + 4, 82), (x - 6, 108)], (90, 180, 255, 255), 7)
        elif kind == "snow":
            for x, y in ((34, 88), (60, 104), (86, 88)):
                for i in range(3):
                    a = math.pi * i / 3
                    line([(x - math.cos(a) * 10, y - math.sin(a) * 10), (x + math.cos(a) * 10, y + math.sin(a) * 10)], (255, 255, 255, 255), 3)
        else:
            d.polygon([(x * k, y * k) for x, y in ((64, 68), (42, 96), (58, 96), (50, 118), (82, 86), (64, 86), (76, 68))], fill=(255, 205, 60, 255))
    return picture.resize((ICON, ICON), Image.LANCZOS)


def main():
    chunks, total = [], 0
    for name in NAMES:
        picture = Image.open(MASCOTS / f"{name}.webp").convert("RGBA").resize((SIZE, SIZE), Image.LANCZOS)
        text, size = encode(name, picture)
        chunks.append(text)
        total += size
    for kind in SKIES:
        text, size = encode(f"sky-{kind}", weather_icon(kind))
        chunks.append(text)
        total += size
    target = HERE.parent / "src" / "images.h"
    target.write_text("// Généré par tools/gen_images.py : ne pas modifier à la main.\n#pragma once\n#include \"draw.h\"\n\n" + "\n".join(chunks))
    print(f"{len(NAMES) + len(SKIES)} images, {total} octets")


if __name__ == "__main__":
    main()
