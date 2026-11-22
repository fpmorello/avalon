#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generates the Avalon UI assets (odd/tasks/b4-ui.md, T2).

Writes into src/eden_horizon/ui_assets/:
  font_small.bin, font_body.bin, font_title.bin  glyph atlases (8-bit coverage)
  logo.bin                                        the emblem, straight RGBA
  icon.jpg                                        256x256 homebrew menu icon
  OFL.txt                                         the font's licence

The device build embeds the .bin files, so it needs neither Python nor a font library.
Run again only to change the look: needs Pillow and Noto Sans (OFL-1.1).

Formats, little endian:
  font  "AVF1" u16 line_height u16 ascent u16 count u16 0, then count records of
        u16 codepoint u8 w u8 h i8 x_offset i8 y_offset u8 advance u8 0 u32 offset,
        then the coverage bytes; y_offset is from the top of the line.
  image "AVI1" u16 width u16 height, then width*height RGBA pixels.

Usage: make_ui_assets.py [--fonts DIR] [--licence FILE]
"""

import argparse
import math
import os
import struct

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
OUT = os.path.join(ROOT, "src", "eden_horizon", "ui_assets")

# ASCII and Latin-1, enough for game names in western languages.
CODEPOINTS = list(range(32, 127)) + list(range(160, 256))

# The emblem's palette: dusk violet to sea teal.
VIOLET = (124, 108, 242)
TEAL = (61, 214, 196)
NIGHT = (18, 20, 28)


def write_font(path, ttf, size):
    font = ImageFont.truetype(ttf, size)
    ascent, descent = font.getmetrics()
    records, blob = [], bytearray()
    for cp in CODEPOINTS:
        ch = chr(cp)
        advance = round(font.getlength(ch))
        x0, y0, x1, y1 = font.getbbox(ch)
        w, h = max(x1 - x0, 0), max(y1 - y0, 0)
        if w == 0 or h == 0:
            records.append((cp, 0, 0, 0, 0, advance, len(blob)))
            continue
        glyph = Image.new("L", (w, h), 0)
        ImageDraw.Draw(glyph).text((-x0, -y0), ch, font=font, fill=255)
        records.append((cp, w, h, x0, y0, advance, len(blob)))
        blob += glyph.tobytes()
    with open(path, "wb") as out:
        out.write(b"AVF1" + struct.pack("<HHHH", ascent + descent, ascent, len(records), 0))
        for cp, w, h, x0, y0, advance, offset in records:
            out.write(struct.pack("<HBBbbBBI", cp, w, h, x0, y0, advance, 0, offset))
        out.write(blob)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def emblem(size):
    """An isle under a dusk sky: original artwork, drawn 4x and scaled down."""
    s = size * 4
    sky = Image.new("RGBA", (s, s))
    px = sky.load()
    for y in range(s):
        row = lerp(VIOLET, TEAL, y / (s - 1))
        for x in range(s):
            px[x, y] = row + (255,)
    art = ImageDraw.Draw(sky)
    # Moon.
    r = s * 0.11
    cx, cy = s * 0.68, s * 0.30
    art.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 247, 228, 255))
    art.ellipse((cx - r * 0.55, cy - r * 1.05, cx + r * 1.35, cy + r * 0.85),
                fill=lerp(VIOLET, TEAL, 0.24) + (255,))
    # The isle: two hills and a tower.
    hill = [(s * 0.08, s * 0.70)]
    for i in range(41):
        t = i / 40
        x = s * (0.08 + 0.84 * t)
        y = s * (0.70 - 0.20 * math.sin(math.pi * t) ** 1.6 - 0.06 * math.sin(math.pi * 2.2 * t) ** 2)
        hill.append((x, y))
    hill.append((s * 0.92, s * 0.70))
    art.polygon(hill, fill=(30, 26, 52, 255))
    tx = s * 0.40
    art.rectangle((tx - s * 0.035, s * 0.40, tx + s * 0.035, s * 0.56), fill=(30, 26, 52, 255))
    art.polygon([(tx - s * 0.05, s * 0.41), (tx, s * 0.33), (tx + s * 0.05, s * 0.41)],
                fill=(30, 26, 52, 255))
    # Water.
    art.rectangle((0, s * 0.70, s, s), fill=(22, 30, 58, 255))
    for i, y in enumerate((0.76, 0.83, 0.90)):
        inset = s * (0.18 + 0.08 * i)
        art.rounded_rectangle((inset, s * y, s - inset, s * y + s * 0.022), radius=s * 0.011,
                              fill=TEAL + (200 - 50 * i,))
    # Round it off.
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, s - 1, s - 1), fill=255)
    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    out.paste(sky, (0, 0), mask)
    ring = ImageDraw.Draw(out)
    ring.ellipse((s * 0.01, s * 0.01, s * 0.99, s * 0.99), outline=(255, 255, 255, 70),
                 width=max(int(s * 0.02), 1))
    return out.resize((size, size), Image.LANCZOS)


def write_image(path, image):
    image = image.convert("RGBA")
    with open(path, "wb") as out:
        out.write(b"AVI1" + struct.pack("<HH", image.width, image.height))
        out.write(image.tobytes())


def write_icon(path, ttf_bold):
    icon = Image.new("RGB", (256, 256), NIGHT)
    glow = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((28, 6, 228, 206), fill=VIOLET + (90,))
    icon.paste(glow.filter(ImageFilter.GaussianBlur(24)), (0, 0), glow.filter(ImageFilter.GaussianBlur(24)))
    mark = emblem(168)
    icon.paste(mark, (44, 18), mark)
    font = ImageFont.truetype(ttf_bold, 40)
    draw = ImageDraw.Draw(icon)
    width = draw.textlength("Avalon", font=font)
    draw.text(((256 - width) / 2, 192), "Avalon", font=font, fill=(236, 238, 248))
    icon.save(path, "JPEG", quality=92)


def font_copyright(ttf):
    """The copyright string (name ID 0) of a TrueType font."""
    data = open(ttf, "rb").read()
    for i in range(struct.unpack(">H", data[4:6])[0]):
        tag, _, offset, _ = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        if tag != b"name":
            continue
        table = data[offset:]
        count, strings = struct.unpack(">HH", table[2:6])
        for j in range(count):
            pid, _, _, nid, length, at = struct.unpack(">6H", table[6 + 12 * j:18 + 12 * j])
            if nid == 0 and pid == 3:
                return table[strings + at:strings + at + length].decode("utf-16-be")
    raise SystemExit(f"no copyright string in {ttf}")


def write_licence(path, ttf, ofl_source):
    text = open(ofl_source, encoding="utf-8").read()
    start = text.find("SIL OPEN FONT LICENSE Version 1.1")
    if start < 0:
        raise SystemExit(f"{ofl_source} does not contain the OFL 1.1 text")
    start = text.rfind("-" * 20, 0, start)
    with open(path, "w", encoding="utf-8") as out:
        out.write(font_copyright(ttf) + "\n\n")
        out.write("This Font Software is licensed under the SIL Open Font License, Version 1.1.\n")
        out.write("This license is copied below, and is also available with a FAQ at:\n")
        out.write("https://openfontlicense.org\n\n")
        out.write("The glyphs in font_*.bin are rasterized from Noto Sans Regular and Bold.\n\n")
        out.write(text[start:])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fonts", default="/usr/share/fonts/noto")
    # Arch's noto-fonts ships an Apache text although the fonts are OFL-1.1, so the OFL body
    # comes from a package that carries it verbatim; the copyright line is Noto Sans' own.
    parser.add_argument("--licence", default="/usr/share/licenses/noto-fonts-cjk/LICENSE")
    args = parser.parse_args()
    regular = os.path.join(args.fonts, "NotoSans-Regular.ttf")
    bold = os.path.join(args.fonts, "NotoSans-Bold.ttf")
    os.makedirs(OUT, exist_ok=True)
    write_font(os.path.join(OUT, "font_small.bin"), regular, 17)
    write_font(os.path.join(OUT, "font_body.bin"), regular, 23)
    write_font(os.path.join(OUT, "font_title.bin"), bold, 34)
    write_image(os.path.join(OUT, "logo.bin"), emblem(64))
    write_icon(os.path.join(OUT, "icon.jpg"), bold)
    write_licence(os.path.join(OUT, "OFL.txt"), regular, args.licence)
    for name in sorted(os.listdir(OUT)):
        print(f"{name}: {os.path.getsize(os.path.join(OUT, name))} bytes")


if __name__ == "__main__":
    main()
