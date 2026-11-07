#!/usr/bin/env python3
"""Generate the Generals Online UI kit: one texture atlas and the MappedImage INI that points
the retail shell gadget image names at it.

The engine draws gadget end caps and tiles at their Coords pixel size and only stretches the
height, so every piece keeps the size of the retail image it replaces. Each piece is padded and
its edge pixels are extruded so stretched and tiled pieces do not sample their neighbours.

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

ATLAS_WIDTH = 256
EXTRUDE = 2
SUPERSAMPLE = 4


def rgba(hex_color, alpha=1.0):
    h = hex_color.lstrip("#")
    return (int(h[0:2], 16) / 255.0, int(h[2:4], 16) / 255.0, int(h[4:6], 16) / 255.0, alpha)


TRANSPARENT = (0.0, 0.0, 0.0, 0.0)
BORDER = rgba("#2F37A8")
HOVER = rgba("#5B6CF2")
PRESSED = rgba("#1D2580")
DISABLED = rgba("#3A3F55")
TEXT = rgba("#EEF1F8")
TITLE = rgba("#E9D65A")

# state suffix -> (border, fill) for framed pieces
BUTTON_STATES = {
    "": (BORDER, rgba("#060912", 0.72)),
    "HiLite-": (HOVER, rgba("#283496", 0.55)),
    "Pushed-": (HOVER, PRESSED),
    "Disabled-": (DISABLED, rgba("#060912", 0.60)),
}
RADIO_STATES = {
    "Enabled": BUTTON_STATES[""],
    "Hilighted": BUTTON_STATES["HiLite-"],
    "Pushed": BUTTON_STATES["Pushed-"],
    "Disabled": BUTTON_STATES["Disabled-"],
}
ENTRY_STATES = {
    "Enabled": (BORDER, rgba("#060912", 0.85)),
    "Hilite": (HOVER, rgba("#060912", 0.85)),
    "Disabled": (DISABLED, rgba("#060912", 0.60)),
}
ARROW_STATES = {
    "Enabled": (BORDER, PRESSED, TEXT),
    "Hilite": (HOVER, rgba("#2A339A"), rgba("#FFFFFF")),
    "HiliteSelected": (HOVER, rgba("#141A5E"), TITLE),
    "Disabled": (DISABLED, rgba("#10142A"), rgba("#5D6380")),
}
THUMB_STATES = {
    "Enabled": rgba("#4A5BE6"),
    "Hilite": rgba("#6B7CFF"),
    "HiliteSelected": rgba("#8FA0FF"),
    "Disabled": DISABLED,
}
LIST_SELECTION = {
    "ListBoxHiliteItem": rgba("#2A339A"),
    "ListBoxHiliteSelectedItem": rgba("#3341B8"),
}


def over(top, bottom):
    ta = top[3]
    a = ta + bottom[3] * (1.0 - ta)
    if a <= 0.0:
        return TRANSPARENT
    return tuple((top[i] * ta + bottom[i] * bottom[3] * (1.0 - ta)) / a for i in range(3)) + (a,)


def in_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    if r <= 0.0:
        return True
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def paint(w, h, sample):
    """Supersample sample(x, y) -> rgba over a w x h piece."""
    pixels = []
    n = SUPERSAMPLE
    for py in range(h):
        row = []
        for px in range(w):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(n):
                for sx in range(n):
                    c = sample(px + (sx + 0.5) / n, py + (sy + 0.5) / n)
                    acc[0] += c[0] * c[3]
                    acc[1] += c[1] * c[3]
                    acc[2] += c[2] * c[3]
                    acc[3] += c[3]
            a = acc[3] / (n * n)
            row.append(TRANSPARENT if a <= 0.0 else (acc[0] / acc[3], acc[1] / acc[3], acc[2] / acc[3], a))
        pixels.append(row)
    return pixels


OPEN = 1000.0


def framed(w, h, border, fill, side, side_width, edge_width, radius):
    """A frame piece. side is 'left', 'right', 'middle' or 'all': which ends are closed."""
    x0 = 0.0 if side in ("left", "all") else -OPEN
    x1 = float(w) if side in ("right", "all") else w + OPEN

    def sample(x, y):
        if not in_round_rect(x, y, x0, 0.0, x1, float(h), radius):
            return TRANSPARENT
        inner_r = max(radius - min(side_width, edge_width), 0.0)
        if in_round_rect(x, y, x0 + side_width, edge_width, x1 - side_width, h - edge_width, inner_r):
            return fill
        return border

    return paint(w, h, sample)


def segment_distance(x, y, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(x - (ax + t * dx), y - (ay + t * dy))


def arrow_button(w, h, border, fill, glyph, up):
    frame = framed(w, h, border, fill, "all", 2.0, 2.0, 2.0)
    cx, cy = w / 2.0, h / 2.0
    half, rise = 4.5, 2.5 if up else -2.5
    points = [(cx - half, cy + rise), (cx, cy - rise), (cx + half, cy + rise)]

    def sample(x, y):
        d = min(segment_distance(x, y, *points[0], *points[1]), segment_distance(x, y, *points[1], *points[2]))
        return glyph if d <= 1.1 else TRANSPARENT

    chevron = paint(w, h, sample)
    return [[over(chevron[y][x], frame[y][x]) for x in range(w)] for y in range(h)]


def inset_bar(w, h, fill, inset_x, inset_y, radius, border=None):
    def sample(x, y):
        if not in_round_rect(x, y, inset_x, inset_y, w - inset_x, h - inset_y, radius):
            return TRANSPARENT
        if border is not None and not in_round_rect(x, y, inset_x + 1, inset_y + 1, w - inset_x - 1, h - inset_y - 1, max(radius - 1, 0)):
            return border
        return fill

    return paint(w, h, sample)


def pieces():
    """(image name, width, height, pixels). Sizes are the retail Coords sizes of each name."""
    out = []
    for state, (border, fill) in BUTTON_STATES.items():
        out.append((f"Buttons-{state}Left", 45, 40, framed(45, 40, border, fill, "left", 2.0, 2.0, 2.0)))
        out.append((f"Buttons-{state}Middle", 10, 40, framed(10, 40, border, fill, "middle", 2.0, 2.0, 2.0)))
        out.append((f"Buttons-{state}Right", 45, 40, framed(45, 40, border, fill, "right", 2.0, 2.0, 2.0)))
    for state, (border, fill) in RADIO_STATES.items():
        out.append((f"RadioButton{state}Left", 34, 24, framed(34, 24, border, fill, "left", 2.0, 1.0, 2.0)))
        out.append((f"RadioButton{state}Middle", 10, 24, framed(10, 24, border, fill, "middle", 2.0, 1.0, 2.0)))
        out.append((f"RadioButton{state}Right", 34, 24, framed(34, 24, border, fill, "right", 2.0, 1.0, 2.0)))
    for state, (border, fill) in ENTRY_STATES.items():
        out.append((f"TextEntry{state}LeftEnd", 10, 22, framed(10, 22, border, fill, "left", 2.0, 1.0, 2.0)))
        out.append((f"TextEntry{state}RightEnd", 10, 22, framed(10, 22, border, fill, "right", 2.0, 1.0, 2.0)))
        out.append((f"TextEntry{state}RepeatingCenter", 20, 22, framed(20, 22, border, fill, "middle", 2.0, 1.0, 2.0)))
        out.append((f"TextEntry{state}SmallRepeatingCenter", 10, 22, framed(10, 22, border, fill, "middle", 2.0, 1.0, 2.0)))
    for prefix, fill in LIST_SELECTION.items():
        out.append((f"{prefix}LeftEnd", 10, 20, framed(10, 20, fill, fill, "left", 0.0, 0.0, 1.0)))
        out.append((f"{prefix}RightEnd", 10, 20, framed(10, 20, fill, fill, "right", 0.0, 0.0, 1.0)))
        out.append((f"{prefix}RepeatingCenter", 10, 20, framed(10, 20, fill, fill, "middle", 0.0, 0.0, 1.0)))
        out.append((f"{prefix}SmallRepeatingCenter", 5, 20, framed(5, 20, fill, fill, "middle", 0.0, 0.0, 1.0)))
    for state, (border, fill, glyph) in ARROW_STATES.items():
        out.append((f"VSliderUpButton{state}", 21, 22, arrow_button(21, 22, border, fill, glyph, True)))
        out.append((f"VSliderDownButton{state}", 21, 22, arrow_button(21, 22, border, fill, glyph, False)))
    for state, fill in THUMB_STATES.items():
        out.append((f"ScrollBarThumb{state}", 21, 22, inset_bar(21, 22, fill, 3.0, 1.0, 1.0)))
    out.append(("hilightedbox", 11, 13, inset_bar(11, 13, rgba("#4A5BE6"), 0.0, 0.0, 1.0)))
    out.append(("dehilightedbox", 11, 13, inset_bar(11, 13, rgba("#1F2550"), 0.0, 0.0, 1.0, BORDER)))
    out.append(("linebox", 13, 15, inset_bar(13, 15, rgba("#5B6CF2", 0.25), 0.0, 0.0, 1.0)))
    out.append(("arrow", 13, 16, inset_bar(13, 16, TEXT, 4.0, 0.0, 1.0, HOVER)))
    return out


def pack(items):
    """Shelf-pack (name, w, h, pixels) into the atlas; returns placements and atlas height."""
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
    atlas = [[TRANSPARENT] * ATLAS_WIDTH for _ in range(height)]
    for name, w, h, pixels in items:
        left, top = placements[name]
        for py in range(-EXTRUDE, h + EXTRUDE):
            for px in range(-EXTRUDE, w + EXTRUDE):
                atlas[top + py][left + px] = pixels[min(max(py, 0), h - 1)][min(max(px, 0), w - 1)]

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
        "; Generated by scripts/go_ui_kit.py; edit the script, not this file.",
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
