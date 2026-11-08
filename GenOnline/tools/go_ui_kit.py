#!/usr/bin/env python3
"""Generate the Generals Online UI kit: one texture atlas and the MappedImage INI that points
the retail shell gadget image names at it.

The pieces draw a clean take on the retail shell gadgets: the retail blues, one even line on every
edge and flat fills, at SCALE texels per pixel. Coords and
TextureWidth/Height stay in retail pixels, the engine takes the image size from Coords and the
UVs from Coords / TextureWidth, so every piece keeps the size of the retail image it replaces
and only gets sharper. Each piece is padded and its edge texels are extruded so stretched and
tiled pieces do not sample their neighbours.

Usage:
    python GenOnline/tools/go_ui_kit.py          write the atlas and the INI into GenOnline/data
    python GenOnline/tools/go_ui_kit.py --check  exit 1 when the committed files differ from a fresh run
"""

import argparse
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "GenOnline" / "data" / "core"
TEXTURE_NAME = "GeneralsOnline_UIKit.tga"
TEXTURE_PATH = DATA / "Art" / "Textures" / TEXTURE_NAME
INI_PATH = DATA / "Data" / "INI" / "MappedImages" / "TextureSize_512" / "ZZ_GeneralsOnline_UIKit.INI"

ATLAS_WIDTH = 256  # in retail pixels
SCALE = 4          # texels per retail pixel
EXTRUDE = 2        # retail pixels of padding around each piece
SUPERSAMPLE = 2    # samples per texel and axis
# edge line width per piece, in its own pixels: LINE_LAYOUT layout pixels at the height the shell layouts usually draw it
LINE_LAYOUT = 1.5
BUTTON_LINE = 1.25 * LINE_LAYOUT  # 40 px art, usually about 32 layout px tall
RADIO_LINE = 1.45 * LINE_LAYOUT   # 24 px art, usually about 17 layout px tall
FIELD_LINE = 0.95 * LINE_LAYOUT   # 22 px art, usually about 23 layout px tall
THUMB_LINE = 1.8 * LINE_LAYOUT    # 22 px art, drawn at its own size
BOX_LINE = (1.25 * 1.2, 1.45 * 1.2)  # slider blocks: 11x13 art drawn as squares, so the line differs per axis; small, so a thinner line


def rgba(hex_color, alpha=1.0):
    h = hex_color.lstrip("#")
    return (int(h[0:2], 16) / 255.0, int(h[2:4], 16) / 255.0, int(h[4:6], 16) / 255.0, alpha)


TRANSPARENT = (0.0, 0.0, 0.0, 0.0)
FIELD = rgba("#05070F", 0.92)
GLYPH = rgba("#C9CEFF")
GLYPH_ACTIVE = rgba("#FFFFFF")
GLYPH_DISABLED = rgba("#5D6080")

# state -> (edge line, fill)
BUTTON_STATES = {
    "": (rgba("#2F4BFF"), rgba("#070A18", 0.82)),
    "HiLite-": (rgba("#6A7BFF"), rgba("#152078", 0.88)),
    "Pushed-": (rgba("#8C9BFF"), rgba("#2B34B8", 0.92)),
    "Disabled-": (rgba("#404159"), rgba("#0A0A10", 0.70)),
}
RADIO_STATES = {
    "Enabled": BUTTON_STATES[""],
    "Hilighted": BUTTON_STATES["HiLite-"],
    "Pushed": BUTTON_STATES["Pushed-"],
    "Disabled": BUTTON_STATES["Disabled-"],
}
# state -> edge line; the field fill is the same in every state
ENTRY_STATES = {
    "Enabled": rgba("#2F4BFF"),
    "Hilite": rgba("#6A7BFF"),
    "Disabled": rgba("#404159"),
}
# arrow buttons join the field they sit beside: no left edge, the field line colour, the state in the fill
ARROW_STATES = {
    "Enabled": (rgba("#2F4BFF"), rgba("#0B1030", 0.92), GLYPH),
    "Hilite": (rgba("#2F4BFF"), rgba("#152078", 0.92), GLYPH_ACTIVE),
    "HiliteSelected": (rgba("#2F4BFF"), rgba("#2B34B8", 0.92), GLYPH_ACTIVE),
    "Disabled": (rgba("#404159"), rgba("#0A0A10", 0.70), GLYPH_DISABLED),
}
# state -> (edge line, fill)
THUMB_STATES = {
    "Enabled": (rgba("#2F4BFF"), rgba("#1B2780")),
    "Hilite": (rgba("#6A7BFF"), rgba("#2B3BC8")),
    "HiliteSelected": (rgba("#8C9BFF"), rgba("#3A4CFF")),
    "Disabled": (rgba("#404159"), rgba("#15161F")),
}
# horizontal slider blocks: (edge line, fill)
SLIDER_ON = (rgba("#6A7BFF"), rgba("#3446FF"))
SLIDER_OFF = (rgba("#24308C"), rgba("#0B1030", 0.92))
SLIDER_HOVER = rgba("#2F4BFF", 0.22)
# track slider (drawn only by the GO track slider draw): (edge line, fill)
SLIDER_TRACK = (rgba("#3A48B0"), rgba("#1A2260", 0.92))
SLIDER_FILL = (rgba("#6A7BFF"), rgba("#3446FF"))
# the thumb is a slim marker at the value: (colour, width in thumb pixels)
SLIDER_MARKER = {
    "": (rgba("#C9CEFF"), 1.15),
    "Hilite": (rgba("#FFFFFF"), 1.6),
    "Disabled": (rgba("#5D6080"), 1.15),
}
TRACK_LINE = 0.55  # 8 px art, drawn about 14 px tall at 1080p
LIST_SELECTION = {
    "ListBoxHiliteItem": rgba("#2F4BFF", 0.25),
    "ListBoxHiliteSelectedItem": rgba("#2B34B8", 0.80),
}


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


def edge(x, y, w, h, left, right):
    """Distance to the closest closed edge, top and bottom are always closed."""
    d = min(y, h - y)
    if left:
        d = min(d, x)
    if right:
        d = min(d, w - x)
    return d


def ends(side):
    return side in ("left", "all"), side in ("right", "all")


def button_piece(w, h, side, line, fill, width):
    left, right = ends(side)

    def sample(x, y):
        return line if edge(x, y, w, h, left, right) < width else fill

    return paint(w, h, sample)


def entry_piece(w, h, side, line):
    left, right = ends(side)

    def sample(x, y):
        return line if edge(x, y, w, h, left, right) < FIELD_LINE else FIELD

    return paint(w, h, sample)


def triangle_inside(x, y, a, b, c):
    def cross(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    d1, d2, d3 = cross(a, b, (x, y)), cross(b, c, (x, y)), cross(c, a, (x, y))
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))


def arrow_piece(w, h, up, line, fill, glyph):
    cx, cy = (w - FIELD_LINE) / 2.0, h / 2.0
    tri = ((cx - 4.5, cy - 2.2), (cx + 4.5, cy - 2.2), (cx, cy + 2.8))

    def sample(x, y):
        if up:
            y = h - y
        if edge(x, y, w, h, False, True) < FIELD_LINE:
            return line
        return glyph if triangle_inside(x, y, *tri) else fill

    return paint(w, h, sample)


def thumb_piece(w, h, line, fill):
    def sample(x, y):
        return line if edge(x, y, w, h, True, True) < THUMB_LINE else fill

    return paint(w, h, sample)


def box_piece(w, h, line, fill):
    lx, ly = BOX_LINE

    def sample(x, y):
        return line if min(x, w - x) < lx or min(y, h - y) < ly else fill

    return paint(w, h, sample)


def round_end(w, h, side, line, fill):
    """Half of a pill: a semicircle of radius h/2 closing the bar on one side."""
    r = h / 2.0

    def sample(x, y):
        cx = r if side == "left" else w - r
        if (side == "left" and x >= cx) or (side == "right" and x <= cx):
            d = min(y, h - y)
        else:
            d = r - math.hypot(x - cx, y - r)
        if d < 0.0:
            return TRANSPARENT
        return line if d < TRACK_LINE else fill

    return paint(w, h, sample)


def marker_piece(w, h, color, width):
    """A slim vertical line in the middle of the thumb rect, 70% of its height."""
    def sample(x, y):
        return color if abs(x - w / 2.0) < width / 2.0 and 0.15 * h <= y < 0.85 * h else TRANSPARENT

    return paint(w, h, sample)


def flat_piece(w, h, color):
    return paint(w, h, lambda x, y: color)


def glyph_arrow(w, h):
    def sample(x, y):
        return GLYPH if triangle_inside(x, y, (0.0, h), (w, h), (w / 2.0, 0.0)) else TRANSPARENT

    return paint(w, h, sample)


def pieces():
    """(image name, width, height, texels). Sizes are the retail Coords sizes of each name."""
    out = []
    for state, (line, fill) in BUTTON_STATES.items():
        out.append((f"Buttons-{state}Left", 45, 40, button_piece(45, 40, "left", line, fill, BUTTON_LINE)))
        out.append((f"Buttons-{state}Middle", 10, 40, button_piece(10, 40, "middle", line, fill, BUTTON_LINE)))
        out.append((f"Buttons-{state}Right", 45, 40, button_piece(45, 40, "right", line, fill, BUTTON_LINE)))
    for state, (line, fill) in RADIO_STATES.items():
        out.append((f"RadioButton{state}Left", 34, 24, button_piece(34, 24, "left", line, fill, RADIO_LINE)))
        out.append((f"RadioButton{state}Middle", 10, 24, button_piece(10, 24, "middle", line, fill, RADIO_LINE)))
        out.append((f"RadioButton{state}Right", 34, 24, button_piece(34, 24, "right", line, fill, RADIO_LINE)))
    for state, line in ENTRY_STATES.items():
        out.append((f"TextEntry{state}LeftEnd", 10, 22, entry_piece(10, 22, "left", line)))
        out.append((f"TextEntry{state}RightEnd", 10, 22, entry_piece(10, 22, "right", line)))
        out.append((f"TextEntry{state}RepeatingCenter", 20, 22, entry_piece(20, 22, "middle", line)))
        out.append((f"TextEntry{state}SmallRepeatingCenter", 10, 22, entry_piece(10, 22, "middle", line)))
    for prefix, color in LIST_SELECTION.items():
        out.append((f"{prefix}LeftEnd", 10, 20, flat_piece(10, 20, color)))
        out.append((f"{prefix}RightEnd", 10, 20, flat_piece(10, 20, color)))
        out.append((f"{prefix}RepeatingCenter", 10, 20, flat_piece(10, 20, color)))
        out.append((f"{prefix}SmallRepeatingCenter", 5, 20, flat_piece(5, 20, color)))
    for state, style in ARROW_STATES.items():
        out.append((f"VSliderUpButton{state}", 21, 22, arrow_piece(21, 22, True, *style)))
        out.append((f"VSliderDownButton{state}", 21, 22, arrow_piece(21, 22, False, *style)))
    for state, (line, fill) in THUMB_STATES.items():
        out.append((f"ScrollBarThumb{state}", 21, 22, thumb_piece(21, 22, line, fill)))
    out.append(("arrow", 13, 16, glyph_arrow(13, 16)))
    out.append(("hilightedbox", 11, 13, box_piece(11, 13, *SLIDER_ON)))
    out.append(("dehilightedbox", 11, 13, box_piece(11, 13, *SLIDER_OFF)))
    out.append(("linebox", 13, 15, flat_piece(13, 15, SLIDER_HOVER)))
    # rounded track ends; the fill keeps a square end where it meets the marker
    out.append(("GOSliderTrackLeft", 4, 8, round_end(4, 8, "left", *SLIDER_TRACK)))
    out.append(("GOSliderTrackCenter", 4, 8, button_piece(4, 8, "middle", *SLIDER_TRACK, TRACK_LINE)))
    out.append(("GOSliderTrackRight", 4, 8, round_end(4, 8, "right", *SLIDER_TRACK)))
    out.append(("GOSliderFillLeft", 4, 8, round_end(4, 8, "left", *SLIDER_FILL)))
    out.append(("GOSliderFillCenter", 4, 8, button_piece(4, 8, "middle", *SLIDER_FILL, TRACK_LINE)))
    out.append(("GOSliderFillRight", 4, 8, button_piece(4, 8, "middle", *SLIDER_FILL, TRACK_LINE)))
    for state, (color, width) in SLIDER_MARKER.items():
        out.append((f"GOSliderThumb{state}", 8, 16, marker_piece(8, 16, color, width)))
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
    """32 bit run length encoded TGA (type 10), packets never cross a scanline."""
    height, width = len(atlas), len(atlas[0])
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 10, 0, 0, 0, 0, 0, width, height, 32, 0x08)
    body = bytearray()
    for row in reversed(atlas):
        texels = [bytes((round(b * 255), round(g * 255), round(r * 255), round(a * 255))) for r, g, b, a in row]
        x = 0
        while x < width:
            run = 1
            while x + run < width and run < 128 and texels[x + run] == texels[x]:
                run += 1
            if run > 1:
                body.append(0x80 | (run - 1))
                body += texels[x]
                x += run
                continue
            raw = 1
            while x + raw < width and raw < 128 and (x + raw + 1 >= width or texels[x + raw] != texels[x + raw + 1]):
                raw += 1
            body.append(raw - 1)
            for t in texels[x:x + raw]:
                body += t
            x += raw
    return header + bytes(body)


def ini_text(items, placements, height):
    lines = [
        "; Generals Online UI kit: points the shell gadget images at GeneralsOnline_UIKit.tga.",
        f"; Generated by GenOnline/tools/go_ui_kit.py; edit the script, not this file. The texture holds {SCALE} texels per pixel.",
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
