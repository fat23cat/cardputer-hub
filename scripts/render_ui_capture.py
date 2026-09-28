"""Render native UI drawing captures using the pinned M5GFX Font0 (Pillow required)."""
import argparse
import math
from pathlib import Path
import re
import shlex
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "managed_components/m5stack__m5gfx/src/lgfx/Fonts/glcdfont.h").read_text()
body = source[source.index("{") + 1 : source.index("}")]
body = re.sub(r"//[^\n]*|/\*.*?\*/", "", body, flags=re.S)
font = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", body))
assert len(font) == 1280, len(font)
for capture in sorted(args.directory.glob("*.draw")):
    image = Image.new("RGB", (240, 135))
    draw = ImageDraw.Draw(image)
    for line in capture.read_text().splitlines():
        parts = shlex.split(line)
        if parts[0] == "C":
            draw.rectangle((0, 0, 239, 134), fill=tuple(map(int, parts[1:])))
        elif parts[0] == "R":
            x, y, w, h, r, g, b = map(int, parts[1:])
            draw.rectangle((x, y, x + w - 1, y + h - 1), fill=(r, g, b))
        elif parts[0] == "T":
            x, y = map(int, parts[1:3])
            scale = float(parts[3])
            r, g, b, br, bg, bb = map(int, parts[4:10])
            origin_x = x
            cursor = 0
            for character in parts[10]:
                glyph_x = origin_x + cursor * scale
                left = math.floor(glyph_x)
                right = math.ceil(glyph_x + 6 * scale) - 1
                bottom = y + math.ceil(8 * scale) - 1
                draw.rectangle((left, y, right, bottom), fill=(br, bg, bb))
                for col in range(5):
                    bits = font[(ord(character) if ord(character) < 256 else ord("?")) * 5 + col]
                    for row in range(8):
                        if bits & (1 << row):
                            draw.rectangle((math.floor(glyph_x + col * scale),
                                            y + math.floor(row * scale),
                                            math.floor(glyph_x + (col + 1) * scale) - 1,
                                            y + math.floor((row + 1) * scale) - 1), fill=(r, g, b))
                cursor += 6
    image.save(capture.with_suffix(".png"))
    image.resize((720, 405), Image.Resampling.NEAREST).save(capture.with_name(capture.stem + "-3x.png"))
    print(capture.with_suffix(".png"))
