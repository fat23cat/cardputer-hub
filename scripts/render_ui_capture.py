"""Render native UI drawing captures using the pinned M5GFX Font0 (Pillow required)."""
import argparse
from pathlib import Path
import re
import shlex


def load_font(root: Path) -> bytearray:
    source = (root / "managed_components/m5stack__m5gfx/src/lgfx/Fonts/glcdfont.h").read_text()
    body = source[source.index("{") + 1 : source.index("}")]
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", body, flags=re.S)
    font = bytearray(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", body))
    assert len(font) == 1280, len(font)
    extension = (root / "src/hardware/cardputer/assets/system_font_extension.h").read_text()
    for code, latin, rows in re.findall(
        r"\{0x([0-9A-F]{2}), (0|'.'), \{((?:\s*\"[.#]{5}\",?){8})?\}\}", extension
    ):
        code = int(code, 16)
        if latin != "0":
            source = ord(latin[1]) * 5
            font[code * 5 : code * 5 + 5] = font[source : source + 5]
            continue
        pattern = re.findall(r"\"([.#]{5})\"", rows)
        for column in range(5):
            font[code * 5 + column] = sum(
                1 << row for row in range(8) if pattern[row][column] == "#"
            )
    return font


def glyph_code(character: str) -> int:
    """The firmware's one-byte glyph for a code point (core/display/display_glyphs.cpp)."""
    value = ord(character)
    if 0x20 <= value <= 0x7E:
        return value
    if 0x410 <= value <= 0x44F:
        return 0x80 + value - 0x410
    special = {0x401: 0xC0, 0x451: 0xC1, 0xAB: 0xC2, 0xBB: 0xC3, 0x2116: 0xC4, 0xB0: 0xC5,
               0x2026: 0xC6, 0xA0: 0x20, 0x2009: 0x20,
               0x2010: 0x2D, 0x2011: 0x2D, 0x2012: 0x2D, 0x2013: 0x2D, 0x2014: 0x2D,
               0x2212: 0x2D, 0x2018: 0x27, 0x2019: 0x27,
               0x201C: 0x22, 0x201D: 0x22, 0x201E: 0x22,
               0x406: 0x49, 0x456: 0x69, 0x408: 0x4A, 0x458: 0x6A}
    return special.get(value, 0x7F)


def draw_text(draw, font, text, x, y, scale, foreground, background, clip=None):
    # Match M5GFX's 16.16 scaling and its per-glyph integer advance.
    fixed_scale = int(scale * 65536)
    advance = (6 * fixed_scale) >> 16
    height = (8 * fixed_scale) >> 16

    def rectangle(left, top, right, bottom, color):
        if clip is not None:
            cx, cy, cw, ch = clip
            left, top = max(left, cx), max(top, cy)
            right, bottom = min(right, cx + cw - 1), min(bottom, cy + ch - 1)
        if left <= right and top <= bottom:
            draw.rectangle((left, top, right, bottom), fill=color)

    for character in text:
        rectangle(x, y, x + advance - 1, y + height - 1, background)
        for col in range(5):
            bits = font[glyph_code(character) * 5 + col]
            for row in range(8):
                if bits & (1 << row):
                    rectangle(x + ((col * fixed_scale) >> 16),
                              y + ((row * fixed_scale) >> 16),
                              x + (((col + 1) * fixed_scale) >> 16) - 1,
                              y + (((row + 1) * fixed_scale) >> 16) - 1, foreground)
        x += advance


def render_capture(capture, font):
    from PIL import Image, ImageDraw

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
            clip = tuple(map(int, parts[12:16])) if len(parts) > 11 and parts[11] == "clip" else None
            draw_text(draw, font, parts[10], x, y, scale, (r, g, b), (br, bg, bb), clip)
    image.save(capture.with_suffix(".png"))
    image.resize((720, 405), Image.Resampling.NEAREST).save(capture.with_name(capture.stem + "-3x.png"))
    print(capture.with_suffix(".png"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    font = load_font(Path(__file__).resolve().parents[1])
    for capture in sorted(args.directory.glob("*.draw")):
        render_capture(capture, font)


if __name__ == "__main__":
    main()
