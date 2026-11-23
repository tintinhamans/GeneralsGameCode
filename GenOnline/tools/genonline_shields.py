#!/usr/bin/env python3
"""Generals Online anti-cheat shields: the icons the lobby list and the game setup screen draw for a lobby anti-cheat.

One 192x32 atlas of six 32x32 heater shields, drawn by supersampling (no third party packages): no anti-cheat (grey
outline with a dash), the built in GenOnline anti-cheat (kit blue with a check mark), Easy Anti-Cheat (green with a check
mark) and a dimmed copy of each for a lobby this client cannot join.

Outputs (GenOnline/data/core):
    Art/Textures/GeneralsOnline_Shields.tga                              the atlas, 32 bit RGBA, uncompressed
    Data/INI/MappedImages/TextureSize_512/ZZ_GeneralsOnline_Shields.INI  the six MappedImages

Usage:
    python GenOnline/tools/genonline_shields.py           write the files
    python GenOnline/tools/genonline_shields.py --check   exit 1 when a file is missing or out of date
"""

import argparse
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "GenOnline" / "data" / "core"

TEXTURE = "GeneralsOnline_Shields"
TGA = f"Art/Textures/{TEXTURE}.tga"
IMAGE_INI = "Data/INI/MappedImages/TextureSize_512/ZZ_GeneralsOnline_Shields.INI"

CELL = 32
SAMPLES = 4  # per axis
OUTLINE = 2.0
DIM_ALPHA = 0.35

GREY = (150, 150, 150)
BLUE = (47, 75, 255)  # the kit blue
GREEN = (56, 224, 122)
WHITE = (255, 255, 255)

# (image name, fill, outline, mark); the dim variants follow in the same order
KINDS = [
    ("GOShieldNone", None, GREY, "dash"),
    ("GOShieldBuiltIn", BLUE, None, "check"),
    ("GOShieldEac", GREEN, None, "check"),
]


def tint(color, amount):
    return tuple(round(c + (255 - c) * amount) for c in color)


def bezier(p0, p1, p2, p3, steps=24):
    out = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out


# heater shield: flat top, straight sides, curved to a point at the bottom
SHIELD = ([(5.0, 4.0), (27.0, 4.0), (27.0, 15.0)]
          + bezier((27.0, 15.0), (27.0, 22.0), (21.5, 26.5), (16.0, 29.0))
          + bezier((16.0, 29.0), (10.5, 26.5), (5.0, 22.0), (5.0, 15.0)))
CHECK = [(10.5, 16.0), (14.5, 20.5), (22.0, 11.5)]
DASH = [(11.0, 16.0), (21.0, 16.0)]


def inside(poly, x, y):
    hit = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            hit = not hit
        j = i
    return hit


def segment_distance(x, y, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = dx * dx + dy * dy
    t = 0.0 if length == 0 else max(0.0, min(1.0, ((x - a[0]) * dx + (y - a[1]) * dy) / length))
    return math.hypot(x - a[0] - t * dx, y - a[1] - t * dy)


def path_distance(path, x, y, closed=False):
    pts = path + [path[0]] if closed else path
    return min(segment_distance(x, y, pts[i], pts[i + 1]) for i in range(len(pts) - 1))


def coverage():
    """Per pixel of one cell: (outline, fill, check, dash) coverage in 0..1, from SAMPLES x SAMPLES samples."""
    rows = []
    for py in range(CELL):
        row = []
        for px in range(CELL):
            o = f = c = d = 0
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    x = px + (sx + 0.5) / SAMPLES
                    y = py + (sy + 0.5) / SAMPLES
                    if inside(SHIELD, x, y):
                        if path_distance(SHIELD, x, y, True) < OUTLINE:
                            o += 1
                        else:
                            f += 1
                    if path_distance(CHECK, x, y) < 1.6:
                        c += 1
                    if path_distance(DASH, x, y) < 1.0:
                        d += 1
            n = SAMPLES * SAMPLES
            row.append((o / n, f / n, c / n, d / n))
        rows.append(row)
    return rows


def over(dst, color, alpha):
    """Composite color at alpha over a premultiplied (r, g, b, a) float pixel."""
    r, g, b, a = dst
    k = 1 - alpha
    return (r * k + color[0] * alpha, g * k + color[1] * alpha, b * k + color[2] * alpha, a * k + alpha)


def render(cov, fill, outline, mark, dim):
    """One cell as rows of (r, g, b, a) bytes."""
    edge = outline or tint(fill, 0.55)
    mark_color = GREY if mark == "dash" else WHITE
    rows = []
    for row in cov:
        out = []
        for o, f, c, d in row:
            px = (0.0, 0.0, 0.0, 0.0)
            if fill:
                px = over(px, fill, f)
            px = over(px, edge, o)
            px = over(px, mark_color, c if mark == "check" else d)
            a = px[3]
            rgb = edge if a == 0 else tuple(px[i] / a for i in range(3))
            out.append((*(round(v) for v in rgb), round(a * 255 * (DIM_ALPHA if dim else 1))))
        rows.append(out)
    return rows


def names():
    return [k[0] for k in KINDS] + [k[0] + "Dim" for k in KINDS]


def build_tga():
    cov = coverage()
    cells = [render(cov, fill, outline, mark, dim) for dim in (False, True) for _, fill, outline, mark in KINDS]
    width = CELL * len(cells)
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, CELL, 32, 0x08)
    body = bytearray()
    for y in reversed(range(CELL)):
        for cell in cells:
            for r, g, b, a in cell[y]:
                body += bytes((b, g, r, a))
    return header + bytes(body)


def image_ini_text():
    width = CELL * len(KINDS) * 2
    out = ("; Generals Online: the anti-cheat shields of the lobby list and the game setup screen.\n"
           "; Generated by GenOnline/tools/genonline_shields.py; edit the script, not this file.\n")
    for i, name in enumerate(names()):
        out += (f"\nMappedImage {name}\n"
                f"  Texture = {TEXTURE}.tga\n"
                f"  TextureWidth = {width}\n"
                f"  TextureHeight = {CELL}\n"
                f"  Coords = Left:{i * CELL} Top:0 Right:{(i + 1) * CELL} Bottom:{CELL}\n"
                "  Status = NONE\n"
                "End\n")
    return out


def crlf(text):
    """INI bytes with CRLF line ends, like the other GO INIs."""
    return text.replace("\n", "\r\n").encode("ascii")


def outputs():
    """(path, bytes) of every generated file."""
    return [(DATA / TGA, build_tga()), (DATA / IMAGE_INI, crlf(image_ini_text()))]


def check():
    """Return the generated files that are missing or differ from what the script writes."""
    return [f"out of date: {path.relative_to(ROOT).as_posix()}"
            for path, data in outputs() if not path.exists() or path.read_bytes() != data]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when a shield file is missing or out of date")
    args = parser.parse_args()
    if args.check:
        problems = check()
        for problem in problems:
            print(problem)
        return 1 if problems else 0
    for path, data in outputs():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote {path.relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
