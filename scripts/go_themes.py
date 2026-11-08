#!/usr/bin/env python3
"""Generals Online UI themes: the palette of each theme and the files only a theme has.

A theme is a folder under GenOnline/GameData/Themes/<name> that the engine mounts over GameData when the player
picks the theme, so every theme file sits at the relative path of the base file it replaces. Classic (Generals Online)
is today's GO kit look in the colours and with the logo of the Generals Online launcher (ui/theme.slint and
ui/assets/generals-online-horizontal.svg in the launcher repository). The theme copies of the kit atlas and the menu
layouts come from scripts/go_ui_kit.py and scripts/go_wnd_layouts.py with the tables below; this script writes the
theme logo, which redefines the retail GeneralsLogo image.

Usage:
    python scripts/go_themes.py           rasterize the launcher logo with Inkscape and write the logo texture and INI
    python scripts/go_themes.py --check   exit 1 when the logo INI is stale, the logo texture is malformed or a theme
                                          file has no base counterpart
"""

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "GenOnline" / "GameData"
THEME_NAME = "Radar"
THEME_DIR = DATA / "Themes" / THEME_NAME

# launcher colour tokens (ui/theme.slint); "pushed" is the only derived one, a bronze-tinted dark for pressed fills
PALETTE = {
    "ground": "#0B1114",
    "panel": "#141C1F",
    "surface": "#1A2427",
    "panel-3": "#223034",
    "line": "#2C3B3F",
    "text": "#F2EDE0",
    "muted": "#A9A396",
    "legal": "#8F897C",
    "empty": "#6F7A7C",
    "tan": "#E3BA6C",
    "sand": "#F7DB9C",
    "bronze": "#BF9048",
    "ink": "#0D151B",
    "green": "#7CC46D",
    "green-dark": "#3F8A37",
    "danger": "#C9584A",
    "pushed": "#3A2F1E",
}
P = PALETTE

# kit colour (scripts/go_ui_kit.py) -> theme colour, by RGB; every kit colour needs an entry, alpha stays
KIT_COLORS = {
    # edge lines
    "#2F4BFF": P["bronze"],    # enabled line, also list selection and hover tints at their own alpha
    "#6A7BFF": P["tan"],       # hilite line, hilite scroll pill
    "#8C9BFF": P["sand"],      # pushed line
    "#4A4E6E": P["line"],      # disabled line
    "#24308C": P["line"],      # unlit slider block line
    # fills
    "#070A18": P["panel"],     # enabled fill
    "#152078": P["panel-3"],   # hilite fill
    "#2B34B8": P["pushed"],    # pushed fill, selected list row
    "#0A0A10": P["ground"],    # disabled fill
    "#05070F": P["ground"],    # entry field and map start fill
    "#0B1030": P["surface"],   # combo arrow and unlit slider block fill
    # slider and scroll bar
    "#3446FF": P["bronze"],    # slider value fill, lit block, scroll pill, chosen map start
    "#283489": P["line"],      # slider and scroll track
    "#3A4FE0": P["bronze"],    # retail scroll thumb
    "#404159": P["line"],      # disabled retail scroll thumb
    "#3A3C55": P["line"],      # disabled scroll pill
    # glyphs
    "#C9CEFF": P["text"],
    "#FFFFFF": P["sand"],      # active glyph
    "#5D6080": P["empty"],     # disabled glyph
}

# layout colour -> theme colour, by RGB, for every colour key of draw data and TEXTCOLOR; alpha stays, the rest is kept
_LINE, _HILITE = P["bronze"], P["tan"]
_LAYOUT_HEX = {
    # kit and retail blue lines and hilites (the base layouts already hold the kit colours for the COLOR_REMAP blues)
    "#2F4BFF": _LINE, "#2E37A8": _LINE, "#3137A7": _LINE, "#1D00FF": _LINE,
    "#6A7BFF": _HILITE, "#8080FF": _HILITE,
    # white and near white text
    "#FFFFFF": P["text"], "#FEFEFE": P["text"], "#FFFEFE": P["text"], "#FCF9F9": P["text"], "#FCFFFF": P["text"],
    "#FFFFFE": P["text"], "#FFFFFC": P["text"], "#F9F9FC": P["text"], "#F9FFFF": P["text"], "#FFFEFC": P["text"],
    "#FFFEFF": P["text"], "#FFFCFC": P["text"],
    # retail highlight text: yellow-green, green and blue
    "#BAFF0C": _HILITE, "#A8FF0C": _HILITE, "#D1FD04": _HILITE, "#03C400": _HILITE, "#2E5100": _HILITE,
    "#0000FE": _HILITE, "#062BCC": _HILITE,
    # disabled grey and retail disabled text
    "#C0C0C0": P["legal"], "#BFBEBD": P["legal"], "#8C8A8A": P["legal"], "#3E405C": P["legal"], "#7A3001": P["legal"],
    "#0000C0": P["legal"],
    # navy and dark green text borders and fills
    "#000080": P["ink"], "#00007F": P["ink"], "#000040": P["ink"], "#1F202F": P["ink"], "#004204": P["ink"],
    "#000032": P["ink"], "#000026": P["ink"], "#03005B": P["ink"], "#030060": P["ink"], "#01007C": P["ink"],
    # pure blue hover fill of USER windows
    "#0000FF": P["panel-3"],
    # progress bars and red text
    "#FFFF00": P["tan"], "#3BFF00": P["green"], "#FF0400": P["danger"],
}


def _rgb(hex_color):
    h = hex_color.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))


LAYOUT_COLORS = {_rgb(k): _rgb(v) for k, v in _LAYOUT_HEX.items()}

# logo: the launcher's horizontal logo replaces the retail GeneralsLogo (370x120 in SCSmShellUserInterface512_001.tga)
LOGO_SVG = Path("D:/Projects/Launcher.worktrees/burn-installer/prototypes/rust-launcher/ui/assets/generals-online-horizontal.svg")
INKSCAPE = Path("C:/Program Files/Inkscape/bin/inkscape.exe")
LOGO_TEXTURE = "GeneralsOnline_Logo.tga"
LOGO_TGA = f"Art/Textures/{LOGO_TEXTURE}"
# loaded after the retail SCSmShellUserInterface512.INI: INI::loadDirectory sorts a folder by name, case-insensitively
LOGO_INI = "Data/INI/MappedImages/TextureSize_512/ZZ_GeneralsOnline_ThemeLogo.INI"
LOGO_RECT = (370, 120)          # retail GeneralsLogo Coords size, in retail pixels
LOGO_SCALE = 2                  # texels per retail pixel; MainMenu draws the logo about 1.9 px per pixel at 1440p
LOGO_TEXTURE_SIZE = (1024, 256)  # power of two holding LOGO_RECT * LOGO_SCALE
ADDITIONS = (LOGO_TGA, LOGO_INI)  # theme files with no base counterpart


def logo_ini_text():
    tw, th = (v // LOGO_SCALE for v in LOGO_TEXTURE_SIZE)
    lines = [
        "; Generals Online theme: the launcher logo in place of the retail GeneralsLogo.",
        f"; Generated by scripts/go_themes.py; edit the script, not this file. The texture holds {LOGO_SCALE} texels per pixel.",
        "",
        "MappedImage GeneralsLogo",
        f"  Texture = {LOGO_TEXTURE}",
        f"  TextureWidth = {tw}",
        f"  TextureHeight = {th}",
        f"  Coords = Left:0 Top:0 Right:{LOGO_RECT[0]} Bottom:{LOGO_RECT[1]}",
        "  Status = NONE",
        "End",
        "",
    ]
    return "\r\n".join(lines)


def read_png(data):
    """Return (width, height, rows of (r, g, b, a) bytes) of an 8 bit RGBA, non-interlaced PNG."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a PNG")
    pos, idat, header = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
    width, height, depth, color, _, _, interlace = header
    if (depth, color, interlace) != (8, 6, 0):
        raise SystemExit("the logo PNG must be 8 bit RGBA, not interlaced")
    raw, stride, rows, prev = zlib.decompress(idat), width * 4, [], bytearray(width * 4)
    for y in range(height):
        start = y * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if kind == 1:
                line[i] = (line[i] + a) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + b) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append([tuple(line[x * 4:x * 4 + 4]) for x in range(width)])
        prev = line
    return width, height, rows


def build_logo(svg=LOGO_SVG, inkscape=INKSCAPE):
    """Rasterize the logo SVG to fit LOGO_RECT, centred, and return the logo texture as TGA bytes."""
    import go_ui_kit  # imported here: go_ui_kit imports this module for the kit palette

    rw, rh = (v * LOGO_SCALE for v in LOGO_RECT)
    with tempfile.TemporaryDirectory() as tmp:
        png = Path(tmp) / "logo.png"
        for size in (f"--export-height={rh}", f"--export-width={rw}"):
            subprocess.run([str(inkscape), str(svg), "--export-type=png", "--export-png-color-mode=RGBA_8",
                            size, f"--export-filename={png}"], check=True, capture_output=True)
            width, height, rows = read_png(png.read_bytes())
            if width <= rw and height <= rh:
                break
        else:
            raise SystemExit(f"the logo does not fit {rw}x{rh} texels")
    left, top = (rw - width) // 2, (rh - height) // 2
    tw, th = LOGO_TEXTURE_SIZE
    atlas = [[go_ui_kit.TRANSPARENT] * tw for _ in range(th)]
    for y, row in enumerate(rows):
        for x, (r, g, b, a) in enumerate(row):
            if a:
                atlas[top + y][left + x] = (r / 255.0, g / 255.0, b / 255.0, a / 255.0)
    return go_ui_kit.tga_bytes(atlas)


def check():
    """Return the problems of the committed theme files the other generators do not cover."""
    problems = []
    ini = THEME_DIR / LOGO_INI
    if not ini.exists() or ini.read_bytes() != logo_ini_text().encode("ascii"):
        problems.append(f"out of date: {ini.relative_to(ROOT).as_posix()}")
    tga = THEME_DIR / LOGO_TGA
    data = tga.read_bytes() if tga.exists() else b""
    if len(data) < 18 or data[2] not in (2, 10) or struct.unpack("<HHB", data[12:17]) != (*LOGO_TEXTURE_SIZE, 32):
        problems.append(f"not a {LOGO_TEXTURE_SIZE[0]}x{LOGO_TEXTURE_SIZE[1]} 32 bit TGA: {tga.relative_to(ROOT).as_posix()}")
    for p in sorted(THEME_DIR.rglob("*")) if THEME_DIR.exists() else []:
        rel = p.relative_to(THEME_DIR).as_posix()
        if p.is_file() and rel not in ADDITIONS and not (DATA / rel).is_file():
            problems.append(f"no base file for theme file: {p.relative_to(ROOT).as_posix()}")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when the theme files are out of date")
    parser.add_argument("--svg", type=Path, default=LOGO_SVG, help="the launcher's generals-online-horizontal.svg")
    parser.add_argument("--inkscape", type=Path, default=Path(shutil.which("inkscape") or INKSCAPE), help="inkscape executable")
    args = parser.parse_args()
    if args.check:
        problems = check()
        for problem in problems:
            print(problem)
        return 1 if problems else 0
    texture = build_logo(args.svg, args.inkscape)
    for rel, data in ((LOGO_TGA, texture), (LOGO_INI, logo_ini_text().encode("ascii"))):
        path = THEME_DIR / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote {path.relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
