#!/usr/bin/env python3
"""Make the Generals Online map preview icons and lobby symbols as PNG art in GenOnline/tools/art/map-icons, which
genonline_ui_kit.py packs into the map icon atlas.

The look is measured on the retail Cash and TecBuilding images. They are 30 pixel renders of small soft solids: no
outline, one smooth sweep from a pale top left that leans to yellow to a deep bottom right, a thin soft edge that is
lighter where it faces the light and deeper where it faces away, and a thin dark fringe in the icon's own hue. Each
icon here starts as a flat SVG silhouette. Microsoft Edge rasterises the silhouette, then this script lights it at
the retail size and doubles the pixels for the atlas.

The dollar and the star follow the outlines of the retail Cash and TecBuilding images.

Usage:
    python GenOnline/tools/genonline_map_icons.py          write the SVG silhouettes and the PNGs
    python GenOnline/tools/genonline_map_icons.py --svg    write the SVG silhouettes only
"""

import argparse
import colorsys
import math
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import genonline_themes  # noqa: E402

ART = Path(__file__).resolve().parent / "art" / "map-icons"
RETAIL_PX = 30   # the size of the retail Cash and TecBuilding images
RENDER_PX = 60   # 15 retail pixels at the UI kit SCALE of 4 texels per pixel
SUPER = 6        # the silhouette is lit at this many samples per retail pixel and axis
BEVEL = 1.3      # width of the soft edge in retail pixels
EDGE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft" / "Edge" / "Application" / "msedge.exe"

# Glyphs on a 64 unit grid, filled with the even odd rule.
GLYPHS = {
    # The dollar and the star follow the outlines of the retail Cash and TecBuilding images.
    "dollar": "M 29.86 2.13 L 34.13 2.13 L 34.13 8.64 C 41.81 8.85 47.14 12.48 50.77 17.81 L 40.10 19.52 C 39.14 16.74 37.01 15.25 34.13 15.04 L 34.13 25.38 C 44.26 28.16 50.77 31.25 50.77 39.36 C 50.77 47.04 45.12 52.05 35.84 53.01 L 35.84 57.6 L 29.86 57.6 L 29.86 53.01 C 19.84 52.16 13.65 47.04 12.8 38.4 L 24.85 38.4 C 25.17 42.98 26.77 45.54 29.86 46.29 L 29.86 34.24 C 20.90 31.68 15.14 28.48 15.14 21.44 C 15.14 14.18 20.8 9.17 29.86 8.64 Z M 29.86 15.57 C 26.98 15.89 25.38 17.6 25.38 20.58 C 25.38 22.82 26.66 23.78 29.86 24.64 Z M 34.13 35.62 L 34.13 46.50 C 37.54 46.08 38.82 43.84 38.82 40.64 C 38.82 38.18 37.54 36.8 34.13 35.62 Z",
    "star": "M 32.96 8.10 L 38.93 27.52 L 58.24 27.52 L 42.77 39.57 L 51.52 58.66 L 32.96 44.37 L 15.25 58.66 L 22.72 39.57 L 7.04 27.52 L 27.52 27.52 Z",
    "derrick": "M32 2C26 14 13 28 13 41a19 19 0 0 0 38 0C51 28 38 14 32 2z",
    "refinery": "M3 61V32l14 9V32l14 9V32l13 9V9h10v52z M53 61V21h8v40z",
    "hospital": "M23 4h18v19h19v18H41v19H23V41H4V23h19z",
    "reinforce": "M3 30C3 13 16 3 32 3s29 10 29 27zM6 32h6l14 14h12l14-14h6L42 50v12H22V50z",
    "repair": "M50 3a13 13 0 0 0-15 17L5 50a6 6 0 0 0 9 9l30-30a13 13 0 0 0 17-15l-8 8-8-3-3-8z",
    "artillery": "M29 2h6v12a19 19 0 0 1 15 15h12v6H50a19 19 0 0 1-15 15v12h-6V50a19 19 0 0 1-15-15H2v-6h12a19 19 0 0 1 15-15zM32 22a10 10 0 1 0 0 20 10 10 0 0 0 0-20z",
    "stats": "M8 4h48v56H8zM16 50h8V32h-8zM28 50h8V20h-8zM40 50h8V38h-8z",
    "lock": "M17 28v-7a15 15 0 0 1 30 0v7h6v32H11V28zm8 0h14v-7a7 7 0 0 0-14 0z",
    "eye": "M2 32C10 17 20 11 32 11s22 6 30 21c-8 15-18 21-30 21S10 47 2 32zm30-12a12 12 0 1 0 0 24 12 12 0 0 0 0-24z",
}
# The hue of the deep shade, None for the grey of steel.
HUES = {"green": 120, "blue": 210, "orange": 20, "amber": 40, "red": 2, "purple": 280, "teal": 168, "yellow": 50, "silver": None}
# (lightness, saturation, share of the lean to yellow) from the deepest shade to the highlight, after the retail Cash
# image, which runs from #175D12 at hue 124 to #F0FCA6 at hue 69.
SHADES = ((.17, .72, -.12), (.31, .66, 0.0), (.50, .58, .35), (.72, .78, .8), (.90, .95, 1.0))
LIGHT = (-.55, -.62, .56)  # towards the light, z points at the viewer
STEEPNESS = .75            # how steep the soft edge reads in the lighting

# image name -> (glyph, colour). The names are the MappedImage names in ZZ_GenOnline_UIKit2.INI.
ICONS = {
    "GOMapIconSupplyDock": ("dollar", "green"),
    "GOMapIconSupplyPile": ("dollar", "blue"),
    "GOMapIconTech": ("star", "yellow"),
    "GOMapIconOilDerrick": ("derrick", "amber"),
    "GOMapIconOilRefinery": ("refinery", "orange"),
    "GOMapIconHospital": ("hospital", "red"),
    "GOMapIconReinforcementPad": ("reinforce", "purple"),
    "GOMapIconRepairBay": ("repair", "teal"),
    "GOMapIconArtillery": ("artillery", "silver"),
    "GOLobbyIconStats": ("stats", "green"),
    "GOLobbyIconPassword": ("lock", "yellow"),
    "GOLobbyIconObserver": ("eye", "silver"),
}


def shades(color):
    """The five colours from the deepest shade to the highlight as (r, g, b) in 0 .. 1."""
    base = HUES[color]
    out = []
    for light, sat, lean in SHADES:
        if base is None:
            out.append(colorsys.hls_to_rgb(220 / 360, min(.97, light + .08), .07))
        else:
            toward_yellow = max(-48.0, min(48.0, (((60 - base + 180) % 360) - 180) * .6))
            out.append(colorsys.hls_to_rgb(((base + toward_yellow * lean) % 360) / 360, light, sat))
    return out


def shade_at(palette, t):
    """The palette colour at t in 0 .. 1."""
    t = max(0.0, min(1.0, t)) * (len(palette) - 1)
    i = min(int(t), len(palette) - 2)
    k = t - i
    return tuple(palette[i][c] + (palette[i + 1][c] - palette[i][c]) * k for c in range(3))


def svg(glyph):
    """The flat silhouette, the source art of an icon."""
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="-4 -4 72 72">'
            f'<path d="{GLYPHS[glyph]}" fill-rule="evenodd" fill="#fff"/></svg>\n')


def rasterise(svg_path, size):
    """The coverage of an SVG at size pixels square, as rows of 0 .. 1."""
    with tempfile.TemporaryDirectory() as tmp:
        page = Path(tmp) / "icon.html"
        png = Path(tmp) / "icon.png"
        page.write_text(f'<!doctype html><html><body style="margin:0;background:transparent">'
                        f'<img src="{svg_path.as_uri()}" width="{size}" height="{size}" style="display:block"></body></html>',
                        encoding="utf-8")
        subprocess.run([str(EDGE), "--headless=new", "--disable-gpu", "--hide-scrollbars", "--default-background-color=00000000",
                        f"--user-data-dir={Path(tmp) / 'profile'}", f"--window-size={size},{size}",
                        f"--screenshot={png}", page.as_uri()], check=True, capture_output=True, timeout=120)
        width, height, rows = genonline_themes.read_png(png.read_bytes())
    if (width, height) != (size, size):
        raise SystemExit(f"{svg_path.name} rendered at {width}x{height}, expected {size} square")
    return [[p[3] / 255.0 for p in row] for row in rows]


def edge_distance(cover):
    """Chamfer distance in pixels from every covered pixel to the nearest pixel outside the shape."""
    n = len(cover)
    far = 1e9
    dist = [[far if cover[y][x] >= .5 else 0.0 for x in range(n)] for y in range(n)]
    for y in range(n):
        for x in range(n):
            if dist[y][x]:
                best = dist[y][x]
                if x > 0:
                    best = min(best, dist[y][x - 1] + 1.0)
                if y > 0:
                    best = min(best, dist[y - 1][x] + 1.0)
                    if x > 0:
                        best = min(best, dist[y - 1][x - 1] + 1.4142)
                    if x < n - 1:
                        best = min(best, dist[y - 1][x + 1] + 1.4142)
                dist[y][x] = best if x and y else min(best, 1.0)
    for y in range(n - 1, -1, -1):
        for x in range(n - 1, -1, -1):
            if dist[y][x]:
                best = dist[y][x]
                if x < n - 1:
                    best = min(best, dist[y][x + 1] + 1.0)
                if y < n - 1:
                    best = min(best, dist[y + 1][x] + 1.0)
                    if x < n - 1:
                        best = min(best, dist[y + 1][x + 1] + 1.4142)
                    if x > 0:
                        best = min(best, dist[y + 1][x - 1] + 1.4142)
                dist[y][x] = best if x < n - 1 and y < n - 1 else min(best, 1.0)
    return dist


def smooth(field, radius):
    """A box blur, which rounds the stair steps of the chamfer distance without softening the facets."""
    n = len(field)
    out = field
    for axis in (0, 1):
        src, out = out, [[0.0] * n for _ in range(n)]
        for y in range(n):
            for x in range(n):
                total, count = 0.0, 0
                for k in range(-radius, radius + 1):
                    yy, xx = (y + k, x) if axis else (y, x + k)
                    if 0 <= yy < n and 0 <= xx < n:
                        total += src[yy][xx]
                        count += 1
                out[y][x] = total / count
    return out


def chisel(cover, color):
    """Light the silhouette as a chiselled solid and return RETAIL_PX rows of (r, g, b, a) in 0 .. 1."""
    n = len(cover)
    bevel = BEVEL * SUPER
    height = smooth([[min(d, bevel) for d in row] for row in edge_distance(cover)], 4)
    palette = shades(color)
    length = math.sqrt(sum(v * v for v in LIGHT))
    lx, ly, lz = (v / length for v in LIGHT)
    flat = lz
    lit = [[None] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            if cover[y][x] <= 0.0:
                continue
            gx = (height[y][min(x + 1, n - 1)] - height[y][max(x - 1, 0)]) * .5 * STEEPNESS
            gy = (height[min(y + 1, n - 1)][x] - height[max(y - 1, 0)][x]) * .5 * STEEPNESS
            norm = math.sqrt(gx * gx + gy * gy + 1.0)
            diffuse = (-gx * lx - gy * ly + lz) / norm
            # the flat top sits at the middle of the palette, a facet turned to the light at its top and one turned away at its bottom
            t = .56 + .44 * (diffuse - flat) / (1.0 - flat) if diffuse >= flat else .56 * (diffuse + .32) / (flat + .32)
            # most of the shading is one smooth sweep from the lit top left to the deep bottom right, as on the retail Cash image
            t += (.5 - y / n) * .62 + (.5 - x / n) * .2
            lit[y][x] = shade_at(palette, t)
    rows = []
    for py in range(RETAIL_PX):
        row = []
        for px in range(RETAIL_PX):
            acc, alpha = [0.0, 0.0, 0.0], 0.0
            for sy in range(py * SUPER, (py + 1) * SUPER):
                for sx in range(px * SUPER, (px + 1) * SUPER):
                    a = cover[sy][sx]
                    if a > 0.0:
                        c = lit[sy][sx]
                        acc[0] += c[0] * a
                        acc[1] += c[1] * a
                        acc[2] += c[2] * a
                        alpha += a
            row.append((acc[0] / alpha, acc[1] / alpha, acc[2] / alpha, alpha / (SUPER * SUPER)) if alpha else (0.0, 0.0, 0.0, 0.0))
        rows.append(row)
    return fringe(rows, palette[0])


def fringe(rows, dark):
    """Put the thin dark fringe of the retail icons under the edge pixels, a little stronger to the bottom right."""
    n = len(rows)
    alpha = lambda y, x: rows[y][x][3] if 0 <= y < n and 0 <= x < n else 0.0
    out = []
    for y in range(n):
        line = []
        for x in range(n):
            r, g, b, a = rows[y][x]
            towards = max(alpha(y - 1, x), alpha(y, x - 1), alpha(y - 1, x - 1))
            others = max(alpha(y + 1, x), alpha(y, x + 1), alpha(y + 1, x + 1), alpha(y - 1, x + 1), alpha(y + 1, x - 1))
            under = min(.5, max(.5 * towards, .2 * others, .5 * a))
            total = a + under * (1.0 - a)
            if total <= 0.0:
                line.append((0.0, 0.0, 0.0, 0.0))
                continue
            mix = lambda top, bottom: (top * a + bottom * under * (1.0 - a)) / total
            line.append((mix(r, dark[0] * .8), mix(g, dark[1] * .8), mix(b, dark[2] * .8), total))
        out.append(line)
    return out


def to_bytes(rows):
    """Rows of (r, g, b, a) in 0 .. 1 as bytes."""
    return [[tuple(round(max(0.0, min(1.0, v)) * 255) for v in p) for p in row] for row in rows]


def write_png(png_path, rows):
    """Write rows of (r, g, b, a) bytes with every pixel repeated to RENDER_PX."""
    scale = RENDER_PX // len(rows)
    out = []
    for row in rows:
        line = []
        for pixel in row:
            line += [pixel] * scale
        out += [line] * scale
    raw = b"".join(b"\0" + bytes(v for p in line for v in p) for line in out)
    chunk = lambda kind, body: struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)
    png_path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", len(out[0]), len(out), 8, 6, 0, 0, 0)) +
                         chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--svg", action="store_true", help="write the SVG silhouettes only")
    args = parser.parse_args()
    ART.mkdir(parents=True, exist_ok=True)
    for name, (glyph, color) in ICONS.items():
        svg_path = ART / f"{name}.svg"
        svg_path.write_text(svg(glyph), encoding="utf-8", newline="\n")
        if not args.svg:
            rows = chisel(rasterise(svg_path, RETAIL_PX * SUPER), color)
            write_png(ART / f"{name}.png", to_bytes(rows))
    print(f"wrote {len(ICONS)} icons into {ART.relative_to(ART.parents[3])}")


if __name__ == "__main__":
    sys.exit(main())
