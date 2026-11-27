#!/usr/bin/env python3
"""Génère src/fonts.h : police Nunito (licence SIL OFL 1.1) lissée sur 16 niveaux.

Usage : python3 tools/gen_fonts.py <dossier files du paquet npm @fontsource/nunito>
(nécessite Pillow ; paquet : npm pack @fontsource/nunito)
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
TEXT = [chr(c) for c in range(32, 127)] + list("°«»ÀÂÇÈÉÊËÎÏÔÙÛÜàâäçèéêëîïôöùûüÿŒœ’…")
# nom, taille, graisse, caractères
FONTS = [("small", 15, 700, TEXT), ("medium", 20, 800, TEXT), ("large", 28, 800, TEXT),
         ("clock", 56, 800, list(" 0123456789:"))]


def build(files, name, size, weight, chars):
    font = ImageFont.truetype(str(files / f"nunito-latin-{weight}-normal.woff"), size)
    chars = sorted(set(chars))
    boxes = {c: font.getbbox(c) for c in chars}
    ink = [b for c, b in boxes.items() if c != " "]
    top, bottom = min(b[1] for b in ink), max(b[3] for b in ink)
    # Chiffres de même largeur : l'heure ne tremble pas.
    digit = max(round(font.getlength(c)) for c in "0123456789")
    glyphs, bits = [], bytearray()
    for c in chars:
        x0, y0, x1, y1 = boxes[c]
        advance = round(font.getlength(c))
        shift = 0
        if name == "clock" and c.isdigit():
            shift, advance = (digit - advance) // 2, digit
        w, h = (0, 0) if c == " " else (x1 - x0, y1 - y0)
        offset = len(bits)
        if w and h:
            picture = Image.new("L", (w, h), 0)
            ImageDraw.Draw(picture).text((-x0, -y0), c, font=font, fill=255)
            levels = [(v * 15 + 127) // 255 for v in picture.tobytes()]
            if len(levels) % 2:
                levels.append(0)
            bits += bytes(levels[i] << 4 | levels[i + 1] for i in range(0, len(levels), 2))
        glyphs.append(f"{{0x{ord(c):04x},{w},{h},{x0 + shift},{y0 - top if h else 0},{advance},{offset}}}")
    rows = lambda v, n: ",\n".join("  " + ",".join(v[i:i + n]) for i in range(0, len(v), n))
    text = (f"static const HpGlyph {name}_glyphs[] = {{\n{rows(glyphs, 5)}}};\n"
            f"static const uint8_t {name}_bits[] = {{\n{rows([str(b) for b in bits], 28)}}};\n"
            f"extern const HpFont hp_font_{name} = {{{name}_glyphs, {len(glyphs)}, {name}_bits, {bottom - top}}};\n")
    return text, len(bits) + 12 * len(glyphs), bottom - top


def main():
    files = Path(sys.argv[1])
    chunks, total = [], 0
    for spec in FONTS:
        text, size, height = build(files, *spec)
        chunks.append(text)
        total += size
        print(spec[0], "hauteur", height, size, "octets")
    (HERE.parent / "src" / "fonts.cpp").write_text(
        "// Généré par tools/gen_fonts.py : ne pas modifier à la main.\n"
        "// Police Nunito, Copyright 2014 The Nunito Project Authors, SIL Open Font License 1.1.\n"
        "#include \"draw.h\"\n\n" + "\n".join(chunks))
    print("total", total, "octets")


if __name__ == "__main__":
    main()
