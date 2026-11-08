#!/usr/bin/env python3
"""Generals Online UI themes: the palette of each theme and the files only a theme has.

A theme is a folder under GenOnline/GameData/Themes/<name> that the engine mounts over GameData when the player
picks the theme, so every theme file sits at the relative path of the base file it replaces. Classic (Generals Online)
is today's GO kit look in the colours and with the logo of the Generals Online launcher (ui/theme.slint and
ui/assets/generals-online-horizontal.svg in the launcher repository). The theme copies of the kit atlas and the menu
layouts come from scripts/go_ui_kit.py and scripts/go_wnd_layouts.py with the tables below; this script writes the
theme logo, which redefines the retail GeneralsLogo image, and the theme ruler, a recolour of the retail MainMenuRuler
frame drawn behind the menus, which redefines that image. It also writes the base GO logo INI, the retail GeneralsLogo
definition with the KEEP_ASPECT image status, so the logo keeps its aspect in menus stretched to a wide screen, and the
load screen credit logo: a base copy of the theme logo texture under its own image name, GOLoadLogo, cropped to the logo,
which every interface style and palette mounts because the credit is attribution, not theming.

Usage:
    python scripts/go_themes.py                 write the logo (rasterized with Inkscape), the logo INIs and the ruler
                                                (from the retail art)
    python scripts/go_themes.py --no-inkscape   write every file but the logo texture (--ruler is an alias); the
                                                load logo is copied from the committed logo texture
    python scripts/go_themes.py --check         exit 1 when a logo INI, the load logo or the ruler is stale, the logo texture is
                                                malformed, the retail archives are missing or a theme file has no base
                                                counterpart
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
    "#0B1030": P["surface"],   # unlit slider block fill
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
LOGO_IMAGE = "GeneralsLogo"
LOGO_RETAIL_INI = "Data/INI/MappedImages/TextureSize_512/SCSmShellUserInterface512.INI"
# base GO copy of the retail logo with KEEP_ASPECT; sorts after the retail file and before the theme logo INI
BASE_LOGO_INI = "Data/INI/MappedImages/TextureSize_512/ZZ_GeneralsOnline_Logo.INI"
KEEP_ASPECT = "KEEP_ASPECT"  # image status (Core Image.h): window images fit their window at the image's own aspect
# load screen credit logo (ShellGameLoadScreen): GO core data outside Themes and the Retail style's exclusions, so it shows
# in every style and palette; its own texture name keeps the Radar GeneralsLogo override independent
LOAD_LOGO_TEXTURE = "GeneralsOnline_LoadLogo.tga"
LOAD_LOGO_TGA = f"Art/Textures/{LOAD_LOGO_TEXTURE}"
LOAD_LOGO_INI = "Data/INI/MappedImages/TextureSize_512/ZZ_GeneralsOnline_LoadLogo.INI"
LOAD_LOGO_IMAGE = "GOLoadLogo"

# ruler: the retail MainMenuRuler (the grid frame with tick marks behind the menus) recoloured into its own texture
RETAIL_DIR = Path("C:/Program Files (x86)/Steam/steamapps/common/Command & Conquer Generals - Zero Hour")
RETAIL_INI_BIG = "INIZH.big"
RETAIL_TEXTURE_BIG = "TexturesZH.big"
RULER_IMAGE = "MainMenuRuler"
RULER_RETAIL_INI = "Data/INI/MappedImages/HandCreated/HandCreatedMappedImages.INI"
RULER_TEXTURE = "GeneralsOnline_RadarRuler.tga"
RULER_TGA = f"Art/Textures/{RULER_TEXTURE}"
# HandCreated loads after TextureSize_<n>, so the redefinition must sit there and sort after the retail file
RULER_INI = "Data/INI/MappedImages/HandCreated/ZZ_GeneralsOnline_ThemeRuler.INI"
# retail brightness (max channel) -> launcher token; each stop is a tone of the retail art: transparent and fill black,
# fill mid, brightest grid fill, frame line (70 79 191), tick marks (53 67 231) and the inner highlight (177 183 255)
RULER_RAMP = ((0, P["ground"]), (40, P["panel"]), (77, P["line"]), (191, P["bronze"]), (231, P["tan"]), (255, P["sand"]))
ADDITIONS = (LOGO_TGA, LOGO_INI, RULER_TGA, RULER_INI)  # theme files with no base counterpart


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
        f"  Status = {KEEP_ASPECT}",
        "End",
        "",
    ]
    return "\r\n".join(lines)


def load_logo_ini_text(texture):
    """The load logo INI: GOLoadLogo over the logo texture bytes, cropped to the texels the logo draws."""
    width, height, rows = read_tga(texture)
    xs = [x for x in range(width) if any(row[x][3] for row in rows)]
    ys = [y for y in range(height) if any(t[3] for t in rows[y])]
    if not xs:
        raise SystemExit("the logo texture is empty")
    s = LOGO_SCALE
    left, top, right, bottom = xs[0] // s, ys[0] // s, -(-(xs[-1] + 1) // s), -(-(ys[-1] + 1) // s)
    tw, th = (v // s for v in LOGO_TEXTURE_SIZE)
    lines = [
        "; Generals Online: the launcher logo the first shell load screen shows as the Generals Online credit.",
        f"; Generated by scripts/go_themes.py from {LOGO_TGA} of the theme; edit the script, not this file.",
        "",
        f"MappedImage {LOAD_LOGO_IMAGE}",
        f"  Texture = {LOAD_LOGO_TEXTURE}",
        f"  TextureWidth = {tw}",
        f"  TextureHeight = {th}",
        f"  Coords = Left:{left} Top:{top} Right:{right} Bottom:{bottom}",
        "  Status = NONE",
        "End",
        "",
    ]
    return "\r\n".join(lines)


def load_logo_outputs():
    """[(path, bytes)] of the load logo, from the committed theme logo texture."""
    texture = (THEME_DIR / LOGO_TGA).read_bytes()
    return [(DATA / LOAD_LOGO_TGA, texture), (DATA / LOAD_LOGO_INI, load_logo_ini_text(texture).encode("ascii"))]


def base_logo_ini_text(retail):
    """The base GO logo INI: every field of the retail GeneralsLogo, with KEEP_ASPECT as its status."""
    lines = [
        f"; Generals Online: the retail {LOGO_IMAGE} keeping its aspect when the menus stretch to a wide screen.",
        "; Generated by scripts/go_themes.py from the retail INI; edit the script, not this file.",
        "",
        f"MappedImage {LOGO_IMAGE}",
    ]
    lines += [f"  {key} = {KEEP_ASPECT if key == 'Status' else value}" for key, value in retail.items()]
    lines += ["End", ""]
    return "\r\n".join(lines)


def build_base_logo(retail_dir=RETAIL_DIR):
    """Return the base GO logo INI text, read from the retail INI archive."""
    ini = read_big_entry(Path(retail_dir) / RETAIL_INI_BIG, LOGO_RETAIL_INI)
    if ini is None:
        raise SystemExit(f"no {LOGO_RETAIL_INI} in {RETAIL_INI_BIG}")
    retail = mapped_image(ini.decode("latin-1"), LOGO_IMAGE)
    if "Status" not in retail:
        raise SystemExit(f"the retail {LOGO_IMAGE} has no Status line to carry {KEEP_ASPECT}")
    return base_logo_ini_text(retail)


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


def read_big_entry(path, name):
    """Return the bytes of one entry of a BIG archive, matched case-insensitively, or None."""
    data = Path(path).read_bytes()
    if data[:4] != b"BIGF":
        raise SystemExit(f"{path} is not a BIG archive")
    want, pos = name.replace("/", "\\").lower(), 16
    for _ in range(struct.unpack(">I", data[8:12])[0]):
        offset, size = struct.unpack(">II", data[pos:pos + 8])
        end = data.index(b"\0", pos + 8)
        if data[pos + 8:end].decode("latin-1").lower() == want:
            return data[offset:offset + size]
        pos = end + 1
    return None


def read_tga(data):
    """Return (width, height, rows of (r, g, b, a), top row first) of a 32 bit type 2 or type 10 TGA."""
    id_len, kind = data[0], data[2]
    width, height, bits, descriptor = struct.unpack("<HHBB", data[12:18])
    if kind not in (2, 10) or bits != 32:
        raise SystemExit("only 32 bit uncompressed or RLE TGAs are supported")
    texels, pos, total = [], 18 + id_len, width * height
    while len(texels) < total:
        if kind == 2:
            texels.append(data[pos:pos + 4])
            pos += 4
            continue
        head = data[pos]
        count = (head & 0x7F) + 1
        if head & 0x80:
            texels += [data[pos + 1:pos + 5]] * count
            pos += 5
        else:
            texels += [data[pos + 1 + 4 * i:pos + 5 + 4 * i] for i in range(count)]
            pos += 1 + 4 * count
    rows = [[(t[2], t[1], t[0], t[3]) for t in texels[y * width:(y + 1) * width]] for y in range(height)]
    # descriptor bit 5 set means the first stored row is the top one
    return width, height, rows if descriptor & 0x20 else rows[::-1]


def mapped_image(text, name):
    """Return the fields of one MappedImage block of an INI text as {key: value}, in file order."""
    fields, inside = {}, False
    for line in text.splitlines():
        line = line.split(";")[0].strip()
        if line.lower() == f"mappedimage {name}".lower():
            inside = True
        elif inside and line == "End":
            return fields
        elif inside and "=" in line:
            key, value = (part.strip() for part in line.split("=", 1))
            fields[key] = value
    raise SystemExit(f"no MappedImage {name} in the retail INI")


def ruler_lut():
    """Return the theme colour of each retail brightness 0..255 along RULER_RAMP."""
    lut = []
    for v in range(256):
        for (v0, c0), (v1, c1) in zip(RULER_RAMP, RULER_RAMP[1:]):
            if v <= v1:
                t = (v - v0) / (v1 - v0)
                lut.append(tuple(round(a + (b - a) * t) for a, b in zip(_rgb(c0), _rgb(c1))))
                break
    return lut


def recolor_ruler(rows):
    """Map each texel's brightness onto RULER_RAMP; the art is a single blue hue, so brightness carries its shape."""
    lut = ruler_lut()
    return [[lut[max(r, g, b)] + (a,) for r, g, b, a in row] for row in rows]


def ruler_ini_text(retail):
    lines = [
        f"; Generals Online theme: the retail {RULER_IMAGE} recoloured to the launcher palette.",
        "; Generated by scripts/go_themes.py from the retail art; edit the script, not this file.",
        "",
        f"MappedImage {RULER_IMAGE}",
    ]
    lines += [f"  {key} = {RULER_TEXTURE if key == 'Texture' else value}" for key, value in retail.items()]
    lines += ["End", ""]
    return "\r\n".join(lines)


def build_ruler(retail_dir=RETAIL_DIR):
    """Return (texture TGA bytes, INI text) of the theme ruler, read from the retail archives."""
    import go_ui_kit  # imported here: go_ui_kit imports this module for the kit palette

    ini = read_big_entry(Path(retail_dir) / RETAIL_INI_BIG, RULER_RETAIL_INI)
    if ini is None:
        raise SystemExit(f"no {RULER_RETAIL_INI} in {RETAIL_INI_BIG}")
    retail = mapped_image(ini.decode("latin-1"), RULER_IMAGE)
    texture = read_big_entry(Path(retail_dir) / RETAIL_TEXTURE_BIG, f"Art/Textures/{retail['Texture']}")
    if texture is None:
        raise SystemExit(f"no Art/Textures/{retail['Texture']} in {RETAIL_TEXTURE_BIG}")
    width, height, rows = read_tga(texture)
    if (width, height) != (int(retail["TextureWidth"]), int(retail["TextureHeight"])):
        raise SystemExit(f"the retail {RULER_IMAGE} texture is {width}x{height}, not the size its INI names")
    atlas = [[(r / 255.0, g / 255.0, b / 255.0, a / 255.0) for r, g, b, a in row] for row in recolor_ruler(rows)]
    return go_ui_kit.tga_bytes(atlas), ruler_ini_text(retail)


def missing_retail(retail_dir):
    return [big for big in (RETAIL_INI_BIG, RETAIL_TEXTURE_BIG) if not (Path(retail_dir) / big).is_file()]


def check(retail_dir=RETAIL_DIR):
    """Return the problems of the committed theme files the other generators do not cover."""
    problems = []
    missing = missing_retail(retail_dir)
    if missing:
        problems.append(f"retail archives not found in {retail_dir}: {', '.join(missing)}; "
                        "cannot verify the ruler and the base logo INI (pass --retail)")
    else:
        texture, ini = build_ruler(retail_dir)
        expected = ((THEME_DIR / RULER_TGA, texture), (THEME_DIR / RULER_INI, ini.encode("ascii")),
                    (DATA / BASE_LOGO_INI, build_base_logo(retail_dir).encode("ascii")))
        for path, data in expected:
            if not path.exists() or path.read_bytes() != data:
                problems.append(f"out of date: {path.relative_to(ROOT).as_posix()}")
    ini = THEME_DIR / LOGO_INI
    if not ini.exists() or ini.read_bytes() != logo_ini_text().encode("ascii"):
        problems.append(f"out of date: {ini.relative_to(ROOT).as_posix()}")
    tga = THEME_DIR / LOGO_TGA
    data = tga.read_bytes() if tga.exists() else b""
    if len(data) < 18 or data[2] not in (2, 10) or struct.unpack("<HHB", data[12:17]) != (*LOGO_TEXTURE_SIZE, 32):
        problems.append(f"not a {LOGO_TEXTURE_SIZE[0]}x{LOGO_TEXTURE_SIZE[1]} 32 bit TGA: {tga.relative_to(ROOT).as_posix()}")
    else:
        for path, expected in load_logo_outputs():
            if not path.exists() or path.read_bytes() != expected:
                problems.append(f"out of date: {path.relative_to(ROOT).as_posix()}")
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
    parser.add_argument("--retail", type=Path, default=RETAIL_DIR, help="the Zero Hour folder holding INIZH.big and TexturesZH.big")
    parser.add_argument("--no-inkscape", "--ruler", action="store_true", dest="no_inkscape",
                        help="write every file but the logo texture, without Inkscape")
    args = parser.parse_args()
    if args.check:
        problems = check(args.retail)
        for problem in problems:
            print(problem)
        return 1 if problems else 0
    missing = missing_retail(args.retail)
    if missing:
        raise SystemExit(f"retail archives not found in {args.retail}: {', '.join(missing)} (pass --retail)")
    ruler, ruler_ini = build_ruler(args.retail)
    outputs = [(THEME_DIR / LOGO_INI, logo_ini_text().encode("ascii")), (THEME_DIR / RULER_TGA, ruler),
               (THEME_DIR / RULER_INI, ruler_ini.encode("ascii")), (DATA / BASE_LOGO_INI, build_base_logo(args.retail).encode("ascii"))]
    if not args.no_inkscape:
        outputs.insert(0, (THEME_DIR / LOGO_TGA, build_logo(args.svg, args.inkscape)))
    for path, data in outputs:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote {path.relative_to(ROOT).as_posix()}")
    # after the logo texture, which the load logo copies
    for path, data in load_logo_outputs():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote {path.relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
