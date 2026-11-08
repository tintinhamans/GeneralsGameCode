#!/usr/bin/env python3
"""Generate the sign-in provider logos of the GO sign-in screen: one small atlas and its MappedImage INI.

The sources sit in scripts/logos: the Discord and Steam marks are 24x24 white SVG paths and the GameReplays
logo is a 116x120 RGBA PNG, all from the Generals Online login page. Inkscape rasterizes the SVGs; the PNG is
scaled down with a premultiplied box filter. Each logo fits a LOGO_SIZE square, centred, in a CELL wide slot
with PAD clear texels around it so bilinear sampling never reaches a neighbour. The marks keep their brand
colours: Discord and Steam stay white (they draw on the kit button fill), GameReplays as given.

WOLLoginMenu.cpp draws GOLogoDiscord, GOLogoSteam and GOLogoGameReplays on the provider buttons, left of the
label. Both themes use the same logos, so the Classic theme has no copy.

Usage:
    python scripts/go_logos.py          write the atlas and the INI into GenOnline/GameData
    python scripts/go_logos.py --check  exit 1 when the committed files differ from a fresh run
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_themes  # noqa: E402
import go_ui_kit  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "GenOnline" / "GameData"
SOURCES = Path(__file__).resolve().parent / "logos"
TEXTURE_NAME = "GeneralsOnline_Logos.tga"
TEXTURE_PATH = DATA / "Art" / "Textures" / TEXTURE_NAME
INI_PATH = DATA / "Data" / "INI" / "MappedImages" / "TextureSize_512" / "ZZ_GeneralsOnline_Logos.INI"

# the sign-in buttons draw a logo about 20 layout units square: 36 px at 1080p, 48 px at 1440p
LOGO_SIZE = 60   # texels
PAD = 2          # clear texels around each logo
CELL = LOGO_SIZE + 2 * PAD
ATLAS_SIZE = (256, CELL)
# image name and source, in the order of the provider buttons
LOGOS = (
    ("GOLogoDiscord", SOURCES / "discord.svg"),
    ("GOLogoSteam", SOURCES / "steam.svg"),
    ("GOLogoGameReplays", SOURCES / "gamereplays.png"),
)


def fit(width, height, rows, size=LOGO_SIZE):
    """Scale rows of (r, g, b, a) bytes to fit a size square, keeping the aspect; premultiplied box filter."""
    scale = size / max(width, height)
    out_w, out_h = max(1, round(width * scale)), max(1, round(height * scale))
    sx, sy = width / out_w, height / out_h
    out = []
    for oy in range(out_h):
        y0, y1 = oy * sy, (oy + 1) * sy
        line = []
        for ox in range(out_w):
            x0, x1 = ox * sx, (ox + 1) * sx
            acc = [0.0, 0.0, 0.0, 0.0]
            for y in range(int(y0), min(height, int(-(-y1 // 1)))):
                cy = min(y1, y + 1) - max(y0, y)
                if cy <= 0:
                    continue
                for x in range(int(x0), min(width, int(-(-x1 // 1)))):
                    cx = min(x1, x + 1) - max(x0, x)
                    if cx <= 0:
                        continue
                    r, g, b, a = rows[y][x]
                    w = cx * cy * a
                    acc[0] += r * w
                    acc[1] += g * w
                    acc[2] += b * w
                    acc[3] += w
            alpha = acc[3] / (sx * sy)
            if acc[3] > 0:
                line.append((round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]), round(alpha)))
            else:
                line.append((0, 0, 0, 0))
        out.append(line)
    return out_w, out_h, out


def rasterize_svg(svg, inkscape):
    """(width, height, rows) of the SVG rendered LOGO_SIZE square by Inkscape."""
    with tempfile.TemporaryDirectory() as tmp:
        png = Path(tmp) / "logo.png"
        subprocess.run([str(inkscape), str(svg), "--export-type=png", "--export-png-color-mode=RGBA_8",
                        f"--export-width={LOGO_SIZE}", f"--export-height={LOGO_SIZE}", f"--export-filename={png}"],
                       check=True, capture_output=True)
        return go_themes.read_png(png.read_bytes())[:3]


def logo_texels(source, inkscape):
    if source.suffix.lower() == ".svg":
        width, height, rows = rasterize_svg(source, inkscape)
        if (width, height) != (LOGO_SIZE, LOGO_SIZE):
            raise SystemExit(f"{source.name} rendered {width}x{height}, not {LOGO_SIZE}x{LOGO_SIZE}")
        return width, height, rows
    return fit(*go_themes.read_png(source.read_bytes())[:3])


def build_texture(inkscape):
    """The atlas as TGA bytes: each logo centred in its LOGO_SIZE square."""
    tw, th = ATLAS_SIZE
    atlas = [[go_ui_kit.TRANSPARENT] * tw for _ in range(th)]
    for i, (_, source) in enumerate(LOGOS):
        width, height, rows = logo_texels(source, inkscape)
        left = i * CELL + PAD + (LOGO_SIZE - width) // 2
        top = PAD + (LOGO_SIZE - height) // 2
        for y, row in enumerate(rows):
            for x, (r, g, b, a) in enumerate(row):
                if a:
                    atlas[top + y][left + x] = (r / 255.0, g / 255.0, b / 255.0, a / 255.0)
    return go_ui_kit.tga_bytes(atlas)


def ini_text():
    lines = [
        f"; Generals Online sign-in provider logos in {TEXTURE_NAME}, drawn on the GO sign-in screen buttons.",
        "; Generated by scripts/go_logos.py; edit the script, not this file. Coords are in texels.",
        "",
    ]
    for i, (name, _) in enumerate(LOGOS):
        left = i * CELL + PAD
        lines += [
            f"MappedImage {name}",
            f"  Texture = {TEXTURE_NAME}",
            f"  TextureWidth = {ATLAS_SIZE[0]}",
            f"  TextureHeight = {ATLAS_SIZE[1]}",
            f"  Coords = Left:{left} Top:{PAD} Right:{left + LOGO_SIZE} Bottom:{PAD + LOGO_SIZE}",
            "  Status = NONE",
            "End",
            "",
        ]
    return "\r\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when the committed files are out of date")
    parser.add_argument("--inkscape", type=Path, default=Path(shutil.which("inkscape") or go_themes.INKSCAPE),
                        help="inkscape executable")
    args = parser.parse_args()
    outputs = ((TEXTURE_PATH, build_texture(args.inkscape)), (INI_PATH, ini_text().encode("ascii")))
    if args.check:
        stale = [p for p, data in outputs if not p.exists() or p.read_bytes() != data]
        for p in stale:
            print(f"out of date: {p.relative_to(ROOT)}")
        return 1 if stale else 0
    for p, data in outputs:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    print("wrote " + ", ".join(str(p.relative_to(ROOT)) for p, _ in outputs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
