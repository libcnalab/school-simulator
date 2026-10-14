#!/usr/bin/env python3
"""Rasterises glyph atlases for the museum's SpriteFonts (needs Pillow and DejaVu Sans).

    python3 tools/gen_font.py [path/to/DejaVuSans.ttf]

Writes Content/Fonts/<name>.png (white glyphs, coverage in alpha) and <name>.txt
(metrics). The generated files are committed, so building the museum does not need
Pillow or the font. DejaVu Sans is under the Bitstream Vera / DejaVu license, which
allows redistribution; see Content/Fonts/LICENSE-DejaVu.txt.

Metrics file format (one record per line, whitespace separated):
    font <lineSpacing> <spacing> <defaultCharCode>
    glyph <code> <atlasX> <atlasY> <w> <h> <cropX> <cropY> <kernLeft> <kernWidth> <kernRight>
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content", "Fonts")
DEFAULT_TTF = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
CHARS = [chr(c) for c in range(32, 127)] + ["·", "•", "→", "✓", "°", "×", "←", "↑", "↓", "▲", "▼", "▶", "◀", "≈", "≤", "≥", "²", "³", "±", "π", "µ", "–", "—", "…", "é", "ö", "ü", "á", "í", "ó", "ž", "š", "č", "ř", "ě", "ý", "★", "✦"]
ATLAS = 512


def build(name, ttf, size):
    font = ImageFont.truetype(ttf, size)
    ascent, descent = font.getmetrics()
    line = ascent + descent
    atlas = Image.new("RGBA", (ATLAS, ATLAS), (255, 255, 255, 0))
    cx = cy = 1
    row_h = 0
    records = []
    for ch in CHARS:
        left, top, right, bottom = font.getbbox(ch, anchor="la")
        w, h = max(0, right - left), max(0, bottom - top)
        adv = font.getlength(ch)
        glyph = Image.new("L", (max(w, 1), max(h, 1)), 0)
        if w and h:
            ImageDraw.Draw(glyph).text((-left, -top), ch, fill=255, font=font, anchor="la")
        if cx + w + 1 > ATLAS:
            cx, cy, row_h = 1, cy + row_h + 1, 0
        if cy + h + 1 > ATLAS:
            raise SystemExit("atlas too small for " + name)
        solid = Image.new("RGBA", (max(w, 1), max(h, 1)), (255, 255, 255, 255))
        atlas.paste(solid, (cx, cy), glyph)
        # XNA kerning triple: left bearing, glyph width, right bearing (advance = sum).
        records.append((ord(ch), cx, cy, w, h, left, top, left, w, adv - left - w))
        cx += w + 1
        row_h = max(row_h, h)
    os.makedirs(ROOT, exist_ok=True)
    atlas.save(os.path.join(ROOT, name + ".png"), optimize=True)
    with open(os.path.join(ROOT, name + ".txt"), "w") as f:
        f.write("font %d 0 %d\n" % (line, ord("?")))
        for r in records:
            f.write("glyph %d %d %d %d %d %d %d %.3f %.3f %.3f\n" % r)
    print("wrote", name, "(%d glyphs, line height %d)" % (len(records), line))


def main():
    ttf = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_TTF
    build("ui", ttf, 22)
    build("title", ttf, 46)


if __name__ == "__main__":
    main()
