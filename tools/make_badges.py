"""Generates data/badges/badges.png, the move-quality badges shown in analysis and review.

The sheet holds six 128 px cells in the order of core::MoveQuality:
best, excellent, good, inaccuracy, mistake, blunder.

Needs Pillow (pip install pillow). Run from the repository root:

    python tools/make_badges.py
"""

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

CELL = 128          # size of one badge in the sheet
SCALE = 8           # drawn this many times larger, then scaled down for smooth edges
RADIUS = 54         # disc radius inside the cell; the rest is transparent padding
WHITE = (255, 255, 255, 255)

BADGES = [
    ("best", (129, 182, 76)),
    ("excellent", (129, 182, 76)),
    ("good", (119, 153, 108)),
    ("inaccuracy", (240, 190, 48)),
    ("mistake", (236, 140, 54)),
    ("blunder", (222, 62, 48)),
]

FONT_CANDIDATES = [
    "C:/Windows/Fonts/segoeuib.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/Library/Fonts/Arial Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
]


def s(value):
    return value * SCALE


def shade(color, factor):
    return tuple(int(c * factor) for c in color) + (255,)


def stroke(draw, points, width, color):
    """A polyline with round caps and round joins."""
    scaled = [(s(x), s(y)) for x, y in points]
    draw.line(scaled, fill=color, width=int(s(width)), joint="curve")
    r = s(width) / 2
    for x, y in scaled:
        draw.ellipse([x - r, y - r, x + r, y + r], fill=color)


def tick(dx=0.0, dy=0.0, size=1.0):
    base = [(-26.5, 1.0), (-8.5, 19.0), (26.5, -19.0)]
    return [(64 + dx + x * size, 64 + dy + y * size) for x, y in base]


def draw_star(draw, color):
    cx, cy = 64, 66
    points = []
    for i in range(10):
        angle = -math.pi / 2 + i * math.pi / 5
        r = 35 if i % 2 == 0 else 15.5
        points.append((cx + math.cos(angle) * r, cy + math.sin(angle) * r))
    draw.polygon([(s(x), s(y)) for x, y in points], fill=color)
    # A thin outline with round joins softens the tips.
    stroke(draw, points + points[:2], 5, color)


def draw_text(draw, text, size, color):
    font = None
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            font = ImageFont.truetype(path, s(size))
            break
    if font is None:
        sys.exit("No bold font found; add one to FONT_CANDIDATES.")
    left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
    x = s(64) - (left + right) / 2
    y = s(64) - (top + bottom) / 2
    draw.text((x, y), text, font=font, fill=color)


def make_badge(name, color):
    image = Image.new("RGBA", (s(CELL), s(CELL)), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    c, r = s(64), s(RADIUS)

    # Soft dark rim so the badge stands out on light squares and pieces.
    draw.ellipse([c - r - s(3), c - r - s(2), c + r + s(3), c + r + s(4)], fill=(0, 0, 0, 70))
    # A darker lip at the bottom gives the disc a little depth.
    draw.ellipse([c - r, c - r, c + r, c + r], fill=shade(color, 0.78))
    face = Image.new("RGBA", image.size, (0, 0, 0, 0))
    ImageDraw.Draw(face).ellipse([c - r, c - r - s(5), c + r, c + r - s(5)], fill=color + (255,))
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).ellipse([c - r, c - r, c + r, c + r], fill=255)
    image.paste(face, (0, 0), Image.composite(face.getchannel("A"), Image.new("L", image.size, 0), mask))

    draw = ImageDraw.Draw(image)
    if name == "best":
        draw_star(draw, WHITE)
    elif name == "excellent":
        # Two ticks side by side; the front one is outlined in the badge colour so the
        # two stay clearly apart.
        stroke(draw, tick(dx=15, dy=-2, size=0.80), 10, WHITE)
        stroke(draw, tick(dx=-13, dy=-2, size=0.80), 19, color + (255,))
        stroke(draw, tick(dx=-13, dy=-2, size=0.80), 10, WHITE)
    elif name == "good":
        stroke(draw, tick(dy=-3), 13, WHITE)
    elif name == "inaccuracy":
        draw_text(draw, "?!", 74, WHITE)
    elif name == "mistake":
        draw_text(draw, "?", 84, WHITE)
    elif name == "blunder":
        draw_text(draw, "??", 70, WHITE)

    return image.resize((CELL, CELL), Image.LANCZOS)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(root, "data", "badges")
    os.makedirs(out_dir, exist_ok=True)

    sheet = Image.new("RGBA", (CELL * len(BADGES), CELL), (0, 0, 0, 0))
    for index, (name, color) in enumerate(BADGES):
        sheet.paste(make_badge(name, color), (index * CELL, 0))
    path = os.path.join(out_dir, "badges.png")
    sheet.save(path)
    print("wrote", path)


if __name__ == "__main__":
    main()
