#!/usr/bin/env python3
"""Generate the Generals Online UI kit: one texture atlas and the MappedImage INI that points
the retail shell gadget image names at it.

The pieces redraw the retail shell gadget art (SCSmShellUserInterface512_001.tga) as vector
shapes in its colors, at SCALE texels per pixel. Coords and TextureWidth/Height stay in retail
pixels, the engine takes the image size from Coords and the UVs from Coords / TextureWidth, so
every piece keeps the size of the retail image it replaces and only gets sharper. Each piece is
padded and its edge texels are extruded so stretched and tiled pieces do not sample their
neighbours.

Usage:
    python scripts/go_ui_kit.py          write the atlas and the INI into GenOnline/GameData
    python scripts/go_ui_kit.py --check  exit 1 when the committed files differ from a fresh run
"""

import argparse
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "GenOnline" / "GameData"
TEXTURE_NAME = "GeneralsOnline_UIKit.tga"
TEXTURE_PATH = DATA / "Art" / "Textures" / TEXTURE_NAME
INI_PATH = DATA / "Data" / "INI" / "MappedImages" / "TextureSize_512" / "ZZ_GeneralsOnline_UIKit.INI"

ATLAS_WIDTH = 256  # in retail pixels
SCALE = 4          # texels per retail pixel
EXTRUDE = 2        # retail pixels of padding around each piece
SUPERSAMPLE = 2    # samples per texel and axis


def rgba(hex_color, alpha=1.0):
    h = hex_color.lstrip("#")
    return (int(h[0:2], 16) / 255.0, int(h[2:4], 16) / 255.0, int(h[4:6], 16) / 255.0, alpha)


def shade(color, factor, alpha):
    return (color[0] * factor, color[1] * factor, color[2] * factor, alpha)


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(4))


TRANSPARENT = (0.0, 0.0, 0.0, 0.0)
BLACK = rgba("#000000")
EDGE = rgba("#000000", 144 / 255.0)
SHADE = rgba("#000000", 128 / 255.0)

# retail frame lines, outermost first: (outer, middle, inner)
BLUE_FRAME = (rgba("#2534FF"), rgba("#00E3FF"), rgba("#2534FF"))
HOVER_FRAME = (rgba("#626DFA"), rgba("#7BEDFB"), rgba("#626DFA"))
PUSHED_FRAME = (rgba("#5862EC"), rgba("#D0D4FF"), rgba("#5862EC"))
GREY_FRAME = (rgba("#404159"), rgba("#8588B9"), rgba("#404159"))

# state suffix -> (frame, gradient fill from the outer end to the centre or None)
BUTTON_STATES = {
    "": (BLUE_FRAME, None),
    "HiLite-": (HOVER_FRAME, (rgba("#141853"), rgba("#2B34B8"))),
    "Pushed-": (PUSHED_FRAME, (rgba("#1D215C"), rgba("#4049CD"))),
    "Disabled-": (GREY_FRAME, None),
}
# state -> (frame, indicator (ring, ring, fill) or None)
RADIO_STATES = {
    "Enabled": ((rgba("#2534FE"), rgba("#00E3FF"), rgba("#2534FE")), None),
    "Hilighted": ((rgba("#626DFA"), rgba("#00E3FF"), rgba("#626DFA")), (rgba("#626DFA"), rgba("#8089FF"), rgba("#B2B7FF"))),
    "Pushed": ((rgba("#7D86FF"), rgba("#DFE1FF"), rgba("#2534FF")), (rgba("#2534FF"), rgba("#7D86FF"), rgba("#DFE1FF"))),
    "Disabled": ((rgba("#414360"), rgba("#7F83BD"), rgba("#414360")), None),
}
# state -> (outer line, inner line); the field itself is opaque black
ENTRY_STATES = {
    "Enabled": (rgba("#00E3FF"), rgba("#626DFA")),
    "Hilite": (rgba("#FFFFFF"), rgba("#00E3FF")),
    "Disabled": (rgba("#8589C5"), rgba("#4E5073")),
}
# state -> (outer line, inner line, bar, glyph, glyph outline or None, band and lower fill or None)
ARROW_STATES = {
    "Enabled": (rgba("#00E3FF"), rgba("#5862EC"), rgba("#5862EC"), rgba("#B2B7FF"), None, None),
    "Hilite": (rgba("#00E3FF"), rgba("#5862EC"), rgba("#5862EC"), rgba("#B2B7FF"), rgba("#0A0B23"), rgba("#2F37A8")),
    "HiliteSelected": (rgba("#FFFFFF"), rgba("#00E3FF"), rgba("#B2B7FF"), rgba("#FFFFFF"), rgba("#060614"), rgba("#2F37A8")),
    "Disabled": (rgba("#8589C5"), rgba("#4E5073"), rgba("#4E5073"), rgba("#8589C5"), None, None),
}
# state -> rings from the outside in, then the fill
THUMB_STATES = {
    "Enabled": ([rgba("#5862EC"), rgba("#00E3FF"), rgba("#5862EC")], BLACK),
    "Hilite": ([rgba("#626DFA"), rgba("#00E3FF"), rgba("#5862EC"), BLACK, rgba("#181C55")], rgba("#2F37A8")),
    "HiliteSelected": ([rgba("#00E3FF"), rgba("#FFFFFF"), rgba("#B2B7FF"), rgba("#030307"), rgba("#0B0C25"), rgba("#2A3196")], rgba("#2F37A8")),
    "Disabled": ([rgba("#4E5073"), rgba("#8589C5"), rgba("#4E5073")], BLACK),
}
LIST_EDGE = rgba("#B2B7FF")
LIST_LINE = rgba("#5B66FF")
LIST_FILL = rgba("#545FFF")
# alpha of the selected row fill by distance from its top or bottom line, retail ramp
LIST_RAMP = [0.0, 31, 53, 79, 100, 114, 122, 126, 127]


def over(top, bottom):
    ta = top[3]
    a = ta + bottom[3] * (1.0 - ta)
    if a <= 0.0:
        return TRANSPARENT
    return tuple((top[i] * ta + bottom[i] * bottom[3] * (1.0 - ta)) / a for i in range(3)) + (a,)


def paint(w, h, sample):
    """Supersample sample(x, y) -> rgba, in retail pixels, over a w x h piece at SCALE texels per pixel."""
    pixels = []
    n = SUPERSAMPLE
    step = 1.0 / (SCALE * n)
    for ty in range(h * SCALE):
        row = []
        for tx in range(w * SCALE):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(n):
                for sx in range(n):
                    c = sample((tx * n + sx + 0.5) * step, (ty * n + sy + 0.5) * step)
                    acc[0] += c[0] * c[3]
                    acc[1] += c[1] * c[3]
                    acc[2] += c[2] * c[3]
                    acc[3] += c[3]
            a = acc[3] / (n * n)
            row.append(TRANSPARENT if a <= 0.0 else (acc[0] / acc[3], acc[1] / acc[3], acc[2] / acc[3], a))
        pixels.append(row)
    return pixels


def ring(x, y, x0, y0, x1, y1, left=True, right=True):
    """Whole pixels between the point and the closest closed edge of the rect, -1 outside."""
    if x < x0 or x >= x1 or y < y0 or y >= y1:
        return -1
    d = min(y - y0, y1 - y)
    if left:
        d = min(d, x - x0)
    if right:
        d = min(d, x1 - x)
    return int(math.floor(d))


def ends(side):
    return side in ("left", "all"), side in ("right", "all")


def button_piece(w, h, side, frame, gradient):
    left, right = ends(side)

    def sample(x, y):
        r = ring(x, y, 0.0, 0.0, w, h, left, right)
        if r < 3:
            return frame[r]
        if r == 3:
            return EDGE
        if gradient is None or r == 4:
            return SHADE
        # darker towards the outer ends, the middle pieces carry the centre colour
        if side == "left":
            t = min(max((x - 7.0) / (w - 8.0), 0.0), 1.0)
        elif side == "right":
            t = min(max((w - 1.0 - x) / (w - 8.0), 0.0), 1.0)
        else:
            t = 1.0
        body = lerp(gradient[0], gradient[1], t)
        if r == 5:
            return shade(body, 0.13, 146 / 255.0)
        if r == 6:
            return shade(body, 0.35, 173 / 255.0)
        return shade(body, 1.0, 192 / 255.0)

    return paint(w, h, sample)


def radio_piece(w, h, side, frame, indicator):
    box = float(h)

    def sample(x, y):
        if side == "left" and x < box:
            r = ring(x, y, 0.0, 0.0, box, h)
            if r < 3:
                return frame[r]
            if indicator is not None and r >= 4:
                return indicator[min(r - 4, 2)]
            return TRANSPARENT
        x0 = box + 1.0 if side == "left" else 0.0
        if x < x0:
            return TRANSPARENT
        r = ring(x, y, x0, 0.0, w, h, side == "left", side == "right")
        return frame[r] if 0 <= r < 3 else TRANSPARENT

    return paint(w, h, sample)


def entry_piece(w, h, side, outer, inner):
    left, right = ends(side)
    lines = (outer, inner)

    def sample(x, y):
        r = ring(x, y, 0.0, 0.0, w, h, left, right)
        return lines[r] if r < 2 else BLACK

    return paint(w, h, sample)


def list_piece(w, h, side, filled):
    def sample(x, y):
        if y < 1.0 or y >= h - 1.0:
            return LIST_LINE
        if (side == "left" and x < 1.0) or (side == "right" and x >= w - 1.0):
            return LIST_EDGE
        if not filled:
            return TRANSPARENT
        d = min(y, h - y)
        i = min(int(d), len(LIST_RAMP) - 2)
        a = LIST_RAMP[i] + (LIST_RAMP[i + 1] - LIST_RAMP[i]) * (d - i) if d < len(LIST_RAMP) - 1 else LIST_RAMP[-1]
        return LIST_FILL[:3] + (a / 255.0,)

    return paint(w, h, sample)


def triangle_inside(x, y, a, b, c):
    def cross(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    d1, d2, d3 = cross(a, b, (x, y)), cross(b, c, (x, y)), cross(c, a, (x, y))
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))


def segment_distance(x, y, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(x - (ax + t * dx), y - (ay + t * dy))


def arrow_piece(w, h, up, outer, inner, bar, glyph, outline, band):
    tri = ((3.0, 8.0), (18.0, 8.0), (10.5, 16.5))

    def sample(x, y):
        if up:
            y = h - y
        r = ring(x, y, 0.0, 0.0, w, h)
        if r == 0:
            return outer
        if r == 1:
            return inner
        if triangle_inside(x, y, *tri):
            return glyph
        if outline is not None:
            d = min(segment_distance(x, y, *tri[0], *tri[2]), segment_distance(x, y, *tri[1], *tri[2]))
            if d < 1.0 and y >= 8.0:
                return outline
        if 7.0 <= y < 8.0:
            return bar
        if band is not None:
            if 3.0 <= x < 18.0 and 3.0 <= y < 6.0:
                return band if 4.0 <= y < 5.0 or outline is None else lerp(band, BLACK, 0.5)
            if 3.0 <= x < 18.0 and 9.0 <= y < 19.0:
                return band
        return BLACK

    return paint(w, h, sample)


def thumb_piece(w, h, rings, fill):
    def sample(x, y):
        r = ring(x, y, 0.0, 0.0, w, h)
        return rings[r] if r < len(rings) else fill

    return paint(w, h, sample)


def glyph_arrow(w, h):
    top, bottom = rgba("#D4D7FF"), rgba("#A4AAFF")

    def sample(x, y):
        if triangle_inside(x, y, (0.0, h), (w, h), (w / 2.0, 0.0)):
            return lerp(top, bottom, min(max(y / h, 0.0), 1.0))
        return TRANSPARENT

    return paint(w, h, sample)


def pieces():
    """(image name, width, height, texels). Sizes are the retail Coords sizes of each name."""
    out = []
    for state, (frame, gradient) in BUTTON_STATES.items():
        out.append((f"Buttons-{state}Left", 45, 40, button_piece(45, 40, "left", frame, gradient)))
        out.append((f"Buttons-{state}Middle", 10, 40, button_piece(10, 40, "middle", frame, gradient)))
        out.append((f"Buttons-{state}Right", 45, 40, button_piece(45, 40, "right", frame, gradient)))
    for state, (frame, indicator) in RADIO_STATES.items():
        out.append((f"RadioButton{state}Left", 34, 24, radio_piece(34, 24, "left", frame, indicator)))
        out.append((f"RadioButton{state}Middle", 10, 24, radio_piece(10, 24, "middle", frame, indicator)))
        out.append((f"RadioButton{state}Right", 34, 24, radio_piece(34, 24, "right", frame, indicator)))
    for state, (outer, inner) in ENTRY_STATES.items():
        out.append((f"TextEntry{state}LeftEnd", 10, 22, entry_piece(10, 22, "left", outer, inner)))
        out.append((f"TextEntry{state}RightEnd", 10, 22, entry_piece(10, 22, "right", outer, inner)))
        out.append((f"TextEntry{state}RepeatingCenter", 20, 22, entry_piece(20, 22, "middle", outer, inner)))
        out.append((f"TextEntry{state}SmallRepeatingCenter", 10, 22, entry_piece(10, 22, "middle", outer, inner)))
    for prefix, filled in (("ListBoxHiliteItem", False), ("ListBoxHiliteSelectedItem", True)):
        out.append((f"{prefix}LeftEnd", 10, 20, list_piece(10, 20, "left", filled)))
        out.append((f"{prefix}RightEnd", 10, 20, list_piece(10, 20, "right", filled)))
        out.append((f"{prefix}RepeatingCenter", 10, 20, list_piece(10, 20, "middle", filled)))
        out.append((f"{prefix}SmallRepeatingCenter", 5, 20, list_piece(5, 20, "middle", filled)))
    for state, style in ARROW_STATES.items():
        out.append((f"VSliderUpButton{state}", 21, 22, arrow_piece(21, 22, True, *style)))
        out.append((f"VSliderDownButton{state}", 21, 22, arrow_piece(21, 22, False, *style)))
    for state, (rings, fill) in THUMB_STATES.items():
        out.append((f"ScrollBarThumb{state}", 21, 22, thumb_piece(21, 22, rings, fill)))
    out.append(("arrow", 13, 16, glyph_arrow(13, 16)))
    return out


def pack(items):
    """Shelf-pack (name, w, h, texels) into the atlas in retail pixels; returns placements and atlas height."""
    order = sorted(items, key=lambda it: (-it[2], -it[1], it[0]))
    placements = {}
    x = y = shelf = 0
    for name, w, h, _ in order:
        cw, ch = w + 2 * EXTRUDE, h + 2 * EXTRUDE
        if cw > ATLAS_WIDTH:
            raise SystemExit(f"{name} is wider than the atlas")
        if x + cw > ATLAS_WIDTH:
            x, y, shelf = 0, y + shelf, 0
        placements[name] = (x + EXTRUDE, y + EXTRUDE)
        x += cw
        shelf = max(shelf, ch)
    used = y + shelf
    height = 1
    while height < used:
        height *= 2
    return placements, height


def build():
    items = pieces()
    names = [it[0] for it in items]
    if len(names) != len(set(names)):
        raise SystemExit("duplicate image names")
    placements, height = pack(items)
    atlas = [[TRANSPARENT] * (ATLAS_WIDTH * SCALE) for _ in range(height * SCALE)]
    pad = EXTRUDE * SCALE
    for name, w, h, texels in items:
        left, top = placements[name][0] * SCALE, placements[name][1] * SCALE
        tw, th = w * SCALE, h * SCALE
        for ty in range(-pad, th + pad):
            for tx in range(-pad, tw + pad):
                atlas[top + ty][left + tx] = texels[min(max(ty, 0), th - 1)][min(max(tx, 0), tw - 1)]

    for a in items:
        ax, ay = placements[a[0]]
        for b in items:
            if a[0] >= b[0]:
                continue
            bx, by = placements[b[0]]
            if ax - EXTRUDE < bx + b[1] + EXTRUDE and bx - EXTRUDE < ax + a[1] + EXTRUDE and ay - EXTRUDE < by + b[2] + EXTRUDE and by - EXTRUDE < ay + a[2] + EXTRUDE:
                raise SystemExit(f"{a[0]} overlaps {b[0]}")

    return tga_bytes(atlas), ini_text(items, placements, height)


def tga_bytes(atlas):
    height, width = len(atlas), len(atlas[0])
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 0x08)
    body = bytearray()
    for row in reversed(atlas):
        for r, g, b, a in row:
            body += bytes((round(b * 255), round(g * 255), round(r * 255), round(a * 255)))
    return header + bytes(body)


def ini_text(items, placements, height):
    lines = [
        "; Generals Online UI kit: points the shell gadget images at GeneralsOnline_UIKit.tga.",
        f"; Generated by scripts/go_ui_kit.py; edit the script, not this file. The texture holds {SCALE} texels per pixel.",
        "",
    ]
    for name, w, h, _ in sorted(items, key=lambda it: it[0].lower()):
        left, top = placements[name]
        lines += [
            f"MappedImage {name}",
            f"  Texture = {TEXTURE_NAME}",
            f"  TextureWidth = {ATLAS_WIDTH}",
            f"  TextureHeight = {height}",
            f"  Coords = Left:{left} Top:{top} Right:{left + w} Bottom:{top + h}",
            "  Status = NONE",
            "End",
            "",
        ]
    return "\r\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when the committed files are out of date")
    args = parser.parse_args()
    texture, ini = build()
    ini_bytes = ini.encode("ascii")
    if args.check:
        stale = [p for p, data in ((TEXTURE_PATH, texture), (INI_PATH, ini_bytes)) if not p.exists() or p.read_bytes() != data]
        for p in stale:
            print(f"out of date: {p.relative_to(ROOT)}")
        return 1 if stale else 0
    TEXTURE_PATH.parent.mkdir(parents=True, exist_ok=True)
    INI_PATH.parent.mkdir(parents=True, exist_ok=True)
    TEXTURE_PATH.write_bytes(texture)
    INI_PATH.write_bytes(ini_bytes)
    print(f"wrote {TEXTURE_PATH.relative_to(ROOT)} and {INI_PATH.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
