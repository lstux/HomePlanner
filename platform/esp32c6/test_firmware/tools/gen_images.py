#!/usr/bin/env python3
"""Génère src/images.h : mascottes de la PWA pour l'écran de la centrale (palette + RLE).

Usage : python3 tools/gen_images.py   (nécessite Pillow)
"""
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
MASCOTS = HERE.parents[3] / "web" / "mascots"
SIZE = 150
BACKGROUND = (6, 8, 24)  # C_BG
NAMES = ["fox-content", "rabbit-content"]


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
    flat = Image.new("RGBA", picture.size, BACKGROUND + (255,))
    flat.alpha_composite(picture)
    indexed = flat.convert("RGB").quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = indexed.getpalette()[:128 * 3]
    colors = [rgb565(*palette[i:i + 3]) for i in range(0, len(palette), 3)]
    # La couleur la plus proche du fond devient exactement le fond (pas de cadre visible).
    corner = indexed.getpixel((0, 0))
    colors[corner] = rgb565(*BACKGROUND)
    data = rle(indexed.tobytes())
    ident = name.replace("-", "_")
    return (f"static const uint16_t {ident}_palette[] = {{\n{array([f'0x{c:04x}' for c in colors], 12)}}};\n"
            f"static const uint8_t {ident}_data[] = {{\n{array([str(b) for b in data], 24)}}};\n"
            f"static const HpImage img_{ident} = {{{picture.width}, {picture.height}, {ident}_palette, {ident}_data, {len(data)}}};\n"), len(data)


def main():
    chunks, total = [], 0
    for name in NAMES:
        picture = Image.open(MASCOTS / f"{name}.webp").convert("RGBA").resize((SIZE, SIZE), Image.LANCZOS)
        text, size = encode(name, picture)
        chunks.append(text)
        total += size
    target = HERE.parent / "src" / "images.h"
    target.write_text("// Généré par tools/gen_images.py : ne pas modifier à la main.\n#pragma once\n#include \"draw.h\"\n\n" + "\n".join(chunks))
    print(f"{len(NAMES)} images, {total} octets")


if __name__ == "__main__":
    main()
