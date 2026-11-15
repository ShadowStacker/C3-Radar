#!/usr/bin/env python3
"""Generate the embedded anti-aliased UI fonts (Processing .vlw format).

Each size is rendered natively so text stays crisp on the 360x360 panel
(the firmware never scales a font).

    python3 scripts/build_fonts.py [path/to/Font-Bold.otf]

Output: data/ui_font_<px>.vlw (embedded via board_build.embed_files).
"""

import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

DEFAULT_FONT = "/usr/share/fonts/opentype/inter/Inter-Bold.otf"
SIZES = (14, 16, 20, 22, 26)
CHARS = [chr(c) for c in range(0x20, 0x7F)] + ["…"]

OUT_DIR = Path(__file__).resolve().parent.parent / "data"


def build(font_path: str, px: int) -> bytes:
    font = ImageFont.truetype(font_path, px)
    glyphs = []
    for ch in sorted(CHARS, key=ord):
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        advance = int(round(font.getlength(ch)))
        bitmap = b""
        if ch == " " or w == 0 or h == 0:
            w = h = 0
            x0 = y0 = 0
        else:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
            bitmap = img.tobytes()
        glyphs.append((ord(ch), h, w, advance, -y0, x0, bitmap))

    ascent = -font.getbbox("d", anchor="ls")[1]
    descent = font.getbbox("p", anchor="ls")[3]

    out = bytearray()
    out += struct.pack(">6i", len(glyphs), 11, px, 0, ascent, descent)
    for code, h, w, adv, dy, dx, _ in glyphs:
        out += struct.pack(">7i", code, h, w, adv, dy, dx, 0)
    for g in glyphs:
        out += g[6]
    return bytes(out)


def main() -> None:
    font_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_FONT
    OUT_DIR.mkdir(exist_ok=True)
    for px in SIZES:
        data = build(font_path, px)
        path = OUT_DIR / f"ui_font_{px}.vlw"
        path.write_bytes(data)
        print(f"{path.name}: {len(data)} bytes")


if __name__ == "__main__":
    main()
