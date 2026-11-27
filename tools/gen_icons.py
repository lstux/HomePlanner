#!/usr/bin/env python3
"""Génère les icônes de la PWA : le renard et le lapin devant la maison, ciel bleu.

Usage (Pillow requis) : python3 tools/gen_icons.py
Écrit dans web/icons/ à partir des mascottes de web/mascots/.
"""
from pathlib import Path

from PIL import Image, ImageDraw

WEB = Path(__file__).resolve().parent.parent / "web"
S = 1024
SKY = ((120, 196, 240), (196, 232, 250))
WALL, ROOF, DOOR = (255, 246, 230), (214, 92, 66), (118, 78, 56)
GRASS, LIGHT = (104, 168, 86), (255, 214, 110)


def background():
    """Ciel et colline, à bords perdus."""
    im = Image.new("RGBA", (S, S))
    d = ImageDraw.Draw(im)
    for y in range(S):
        color = tuple(int(a + (b - a) * y / S) for a, b in zip(*SKY))
        d.line([(0, y), (S, y)], fill=color + (255,))
    d.ellipse([-260, 760, S + 260, 1450], fill=GRASS)
    return im


def foreground():
    """Maison et mascottes, sur fond transparent."""
    im = Image.new("RGBA", (S, S))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([650, 230, 732, 420], 12, fill=ROOF)  # cheminée
    d.rounded_rectangle([232, 462, 792, 830], 34, fill=WALL, corners=(False, False, True, True))
    d.polygon([(232, 463), (232, 402), (512, 168), (792, 402), (792, 463)], fill=WALL)
    d.line([(160, 462), (512, 168), (864, 462)], fill=ROOF, width=70, joint="curve")
    for x, y in [(160, 462), (864, 462), (512, 168)]:
        d.ellipse([x - 35, y - 35, x + 35, y + 35], fill=ROOF)
    d.ellipse([452, 310, 572, 430], fill=LIGHT, outline=DOOR, width=14)
    d.line([(512, 315), (512, 425)], fill=DOOR, width=12)
    d.line([(457, 370), (567, 370)], fill=DOOR, width=12)
    d.rounded_rectangle([432, 560, 592, 850], 70, fill=DOOR)
    for name, center in (("fox-content", 350), ("rabbit-content", 690)):
        mascot = Image.open(WEB / "mascots" / f"{name}.webp").convert("RGBA")
        mascot = mascot.crop(mascot.getbbox())
        mascot = mascot.resize((round(mascot.width * 500 / mascot.height), 500), Image.LANCZOS)
        im.alpha_composite(mascot, (center - mascot.width // 2, 420))
    return im


def icon(scale=1.0):
    front = foreground()
    if scale != 1.0:
        side = round(S * scale)
        small = front.resize((side, side), Image.LANCZOS)
        front = Image.new("RGBA", (S, S))
        front.alpha_composite(small, ((S - side) // 2, (S - side) // 2))
    im = background()
    im.alpha_composite(front)
    return im


def rounded(im, size):
    """Carré arrondi à coins transparents, pour les usages sans recadrage."""
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], 220, fill=255)
    out = im.copy()
    out.putalpha(mask)
    return out.resize((size, size), Image.LANCZOS)


def main():
    out = WEB / "icons"
    out.mkdir(exist_ok=True)
    plain = icon()
    for size in (192, 512):
        rounded(plain, size).save(out / f"home-{size}.png", optimize=True)
    rounded(plain, 64).save(out / "home-64.png", optimize=True)
    # « maskable » : le lanceur Android recadre les bords, la scène est réduite.
    icon(0.82).resize((512, 512), Image.LANCZOS).convert("RGB").save(
        out / "home-maskable-512.png", optimize=True)


if __name__ == "__main__":
    main()
