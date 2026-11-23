#!/usr/bin/env python3
"""Generate the Generals Online UI kit: one texture atlas and the MappedImage INI that points
the retail shell gadget image names at it, plus a second atlas and INI for the GO map preview
icons and the map list favourite stars.

The pieces draw a clean take on the retail shell gadgets: the retail blues, one even line on every
edge and flat fills, at SCALE texels per pixel. Coords and
TextureWidth/Height stay in retail pixels, the engine takes the image size from Coords and the
UVs from Coords / TextureWidth, so every piece keeps the size of the retail image it replaces
and only gets sharper. Each piece is padded and its edge texels are extruded so stretched and
tiled pieces do not sample their neighbours.

A fourth atlas holds the radar van that drives on the GO bar art (LoadScreen.cpp drawGoLoadBarVan), from the logo
session's layered art in GenOnline/tools/art/van-side: the body, the dish, four wheel frames and a dust puff. The Retail style
leaves it out.

Usage:
    python GenOnline/tools/go_ui_kit.py          write the INIs and the theme independent atlases into GenOnline/data/core and the theme
                                                 atlases (the UI kit, the map icons and the load road) into the folder of each theme
    python GenOnline/tools/go_ui_kit.py --check  exit 1 when the committed files differ from a fresh run
    python GenOnline/tools/go_ui_kit.py --theme generals          write only the theme atlases of one theme
    python GenOnline/tools/go_ui_kit.py --theme generals --check  exit 1 when those files are out of date

The themes are zerohour, the blue of the Zero Hour interface, and generals, the orange of the Generals interface. A game folder of
GenOnline/data holds the atlases and the Data/INI/GOUIColors.ini of its theme, core holds what both share.
"""

import argparse
import math
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_themes  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "GenOnline" / "data" / "core"
TEXTURE_NAME = "GeneralsOnline_UIKit.tga"
TEXTURE_PATH = DATA / "Art" / "Textures" / TEXTURE_NAME
INI_PATH = DATA / "Data" / "INI" / "MappedImages" / "TextureSize_512" / "ZZ_GeneralsOnline_UIKit.INI"

ATLAS_WIDTH = 256  # in retail pixels
SCALE = 4          # texels per retail pixel
EXTRUDE = 1        # retail pixels of padding around each piece (SCALE texels)
SUPERSAMPLE = 2    # samples per texel and axis
# edge line width in layout pixels; three-piece bars carry it as Border, so the engine keeps it fixed at any bar height
LINE_LAYOUT = 1.5
# images whose INI entry gets Border = LINE_LAYOUT: the engine draws their LINE_LAYOUT edge rows and columns at the kit
# line weight, ceil(1.5 * H / 600) px, whatever size they are drawn at (three-piece bars, single images as a nine-slice)
BORDER_PREFIXES = ("Buttons-", "RadioButton", "TextEntry", "VSliderUpButton", "VSliderDownButton",
                   "ReturnButton", "Xbutton", "LobbyRefresh", "Active-", "Disabled-")
# single images whose Border tells the engine the kit line thickness, and the load screen bar frame (a three-piece bar)
BORDER_NAMES = ("GOScrollLine", "LoadingBar_L", "LoadingBar_C", "LoadingBar_R", "GORoadL", "GORoadC", "GORoadR")
ARROW_LINE = LINE_LAYOUT  # combo arrows carry the kit line like every bordered piece
BOX_LINE = (1.25 * 1.2, 1.45 * 1.2)  # slider blocks: 11x13 art drawn as squares, so the line differs per axis; small, so a thinner line


# --theme recolours the blues of the kit, and the colour table of the theme (Data/INI/GOUIColors.ini) carries the same colours to the engine
THEME = sys.argv[sys.argv.index("--theme") + 1] if "--theme" in sys.argv else ""
THEMES = {
    "zerohour": {},  # the colours of the kit as drawn
    "generals": {
        "#2F4BFF": "#E8931A",  # kit line
        "#6A7BFF": "#FFB54A",  # hilite line
        "#8C9BFF": "#FFD08A",  # pushed line
        "#3446FF": "#D9841A",  # selected and slider fill
        "#C9CEFF": "#FFE6BF",  # glyph
        "#5D6080": "#806A4E",  # disabled glyph
        "#2B34B8": "#A85E0C",  # pushed fill
        "#152078": "#6B3A06",  # hilite fill
        "#05070F": "#0F0A05",  # field
        "#4A4E6E": "#6E5A44",  # disabled line
        "#404159": "#5A4A38",  # disabled scroll thumb
        "#3A4FE0": "#C97A14",  # scroll thumb
        "#3A3C55": "#4D3F30",  # disabled scroll pill
        "#283489": "#6A410E",  # slider track
        "#24308C": "#7A4A10",  # slider off line
        "#0B1030": "#1A1005",  # slider off fill
        "#070A18": "#140C04",  # button fill
        "#0A0A10": "#100C08",  # disabled fill
    },
}
if THEME and THEME not in THEMES:
    sys.exit("unknown theme " + THEME)


def rgba(hex_color, alpha=1.0):
    hex_color = THEMES.get(THEME, {}).get(hex_color.upper(), hex_color)
    h = hex_color.lstrip("#")
    return (int(h[0:2], 16) / 255.0, int(h[2:4], 16) / 255.0, int(h[4:6], 16) / 255.0, alpha)


TRANSPARENT = (0.0, 0.0, 0.0, 0.0)
FIELD = rgba("#05070F", 0.92)
GLYPH = rgba("#C9CEFF")
GLYPH_ACTIVE = rgba("#FFFFFF")
GLYPH_DISABLED = rgba("#5D6080")
DISABLED_LINE = rgba("#4A4E6E")  # lighter than the old #404159 so a disabled outline keeps the same visible weight

# state -> (edge line, fill)
BUTTON_STATES = {
    "": (rgba("#2F4BFF"), rgba("#070A18", 0.82)),
    "HiLite-": (rgba("#6A7BFF"), rgba("#152078", 0.88)),
    "Pushed-": (rgba("#8C9BFF"), rgba("#2B34B8", 0.92)),
    "Disabled-": (DISABLED_LINE, rgba("#0A0A10", 0.70)),
}
# a selected radio (a chosen tab or option) draws the Hilighted art, difficulty select the Pushed art: the pushed line over
# a translucent slider fill, so the chosen one reads at a glance
RADIO_SELECTED = (rgba("#8C9BFF"), rgba("#3446FF", 0.55))
RADIO_STATES = {
    "Enabled": BUTTON_STATES[""],
    "Hilighted": RADIO_SELECTED,
    "Pushed": RADIO_SELECTED,
    "Disabled": BUTTON_STATES["Disabled-"],
}
# state -> edge line; the field fill is the same in every state
ENTRY_STATES = {
    "Enabled": rgba("#2F4BFF"),
    "Hilite": rgba("#6A7BFF"),
    "Disabled": DISABLED_LINE,
}
# combo arrows are the right-hand square of one field box: no left edge, the field fill and the field line of each
# state, so hover lights the whole field (the engine lights field and arrow together) and pushed looks like hover
ARROW_STATES = {
    "Enabled": (ENTRY_STATES["Enabled"], FIELD, GLYPH),
    "Hilite": (ENTRY_STATES["Hilite"], FIELD, GLYPH_ACTIVE),
    "HiliteSelected": (ENTRY_STATES["Hilite"], FIELD, GLYPH_ACTIVE),
    "Disabled": (ENTRY_STATES["Disabled"], FIELD, GLYPH_DISABLED),
}
# horizontal slider blocks: (edge line, fill)
SLIDER_ON = (rgba("#6A7BFF"), rgba("#3446FF"))
SLIDER_OFF = (rgba("#24308C"), rgba("#0B1030", 0.92))
SLIDER_HOVER = rgba("#2F4BFF", 0.22)
# track slider (drawn only by the GO track slider draw): plain flat bars with square ends, no outline
SLIDER_TRACK = rgba("#283489")
SLIDER_FILL = rgba("#3446FF")
# the thumb is a plain bar at the value, 4 units wide and 1.75 track heights tall (W3DHorizontalSlider)
SLIDER_MARKER = {
    "": GLYPH,
    "Hilite": GLYPH_ACTIVE,
    "Disabled": GLYPH_DISABLED,
}
SLIDER_TRACK_HEIGHT = 8
SLIDER_MARKER_SIZE = (4, 14)
SYMBOL_LINE = LINE_LAYOUT  # check boxes and symbol buttons draw at about their own size
# every glyph: a 16 unit design grid fitted to its piece, one 2 unit stroke with round caps and joins, 2 units of padding
GLYPH_GRID = 16.0
GLYPH_STROKE_UNITS = 2.0
# symbol buttons and check boxes: state -> (edge line, fill, glyph)
SYMBOL_STATES = {
    "Enabled": BUTTON_STATES[""] + (GLYPH,),
    "Hilite": BUTTON_STATES["HiLite-"] + (GLYPH_ACTIVE,),
    "Pushed": BUTTON_STATES["Pushed-"] + (GLYPH_ACTIVE,),
    "Disabled": BUTTON_STATES["Disabled-"] + (GLYPH_DISABLED,),
}
CHECK_BOXES = {
    "Active-Unchecked": ("Enabled", False),
    "Active-HiLighted": ("Hilite", False),
    "Active-HiLighted-Checked": ("Hilite", True),  # hover over a checked box; the layouts point HILITEDRAWDATA at it
    "Active-Checked": ("Enabled", True),
    "Disabled-Unchecked": ("Disabled", False),
    "Disabled-Checked": ("Disabled", True),
}
RETURN_BUTTONS = {"ReturnButton": "Enabled", "ReturnButtonHilite": "Hilite", "ReturnButtonHilitePushed": "Pushed", "ReturnButtonDisabled": "Disabled"}
CLOSE_BUTTONS = {"XbuttonEnabled": "Enabled", "XbuttonHilite": "Hilite", "XbuttonPushed": "Pushed", "XbuttonDisabled": "Disabled"}
REFRESH_BUTTONS = {"LobbyRefresh": "Enabled", "LobbyRefreshMouseOver": "Hilite", "LobbyRefreshMouseDown": "Pushed"}
# map start positions: a ring, the chosen one filled
PLAYER_STARTS = {
    "PlayerStart": (rgba("#2F4BFF"), rgba("#05070F", 0.85)),
    "PlayerStartHilite": (rgba("#8C9BFF"), rgba("#152078", 0.92)),
    "PlayerStartHilitePushed": (rgba("#C9CEFF"), rgba("#3446FF")),
    "PlayerStartDisabled": (DISABLED_LINE, rgba("#0A0A10", 0.70)),
}
# scroll bar thumb: a slim plain bar in the middle of the thumb rect
SCROLL_THUMBS = {
    "Enabled": rgba("#3A4FE0"),
    "Hilite": rgba("#6A7BFF"),
    "HiliteSelected": rgba("#8C9BFF"),
    "Disabled": rgba("#404159"),
}
SCROLL_THUMB_WIDTH = 7.0
# kit list scroll bar (drawn only by the GO list scroll bar draw): a flat channel, a flat thumb bar and arrow cells
SCROLL_TRACK = SLIDER_TRACK
SCROLL_PILLS = {
    "": SLIDER_FILL,
    "Hilite": rgba("#6A7BFF"),
    "Disabled": rgba("#3A3C55"),
}
# arrow chevrons in the combo arrow states: state -> chevron; hover and pushed light only the chevron, no cell behind it
SCROLL_ARROWS = {
    "": GLYPH,
    "Hilite": GLYPH_ACTIVE,
    "Pushed": GLYPH_ACTIVE,
    "Disabled": GLYPH_DISABLED,
}
# the kit scroll column is 14 layout px wide with a 6 px channel the thumb fills (GadgetListBoxGetKitScrollSpan)
# the chevron alone, its ink filling the piece: the engine draws it centred on the column line the thumb shares
SCROLL_CHEVRON_ART = (12, 8)
# flat track and thumb pieces with square ends: any size draws the same plain bar
SCROLL_PILL_ART = (4, 2)
SCROLL_LINE = ARROW_STATES["Enabled"][0]  # kit list frames, cell lines and the column separator the engine draws, Border = LINE_LAYOUT
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
        return line if edge(x, y, w, h, left, right) < LINE_LAYOUT else FIELD

    return paint(w, h, sample)


def triangle_inside(x, y, a, b, c):
    def cross(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    d1, d2, d3 = cross(a, b, (x, y)), cross(b, c, (x, y)), cross(c, a, (x, y))
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))


def arrow_piece(w, h, up, line, fill, glyph):
    """The combo drop-down arrow: no left edge, the stroke chevron centred in the space left of the right line.
    The field box beside it draws no right edge either, so the two read as one box with no divider."""
    hit = chevron_glyph(w, h, up, (0.0, 0.0, w - ARROW_LINE, h))

    def sample(x, y):
        if edge(x, y, w, h, False, True) < ARROW_LINE:
            return line
        return glyph if hit(x, y) else fill

    return paint(w, h, sample)


def box_piece(w, h, line, fill):
    lx, ly = BOX_LINE

    def sample(x, y):
        return line if min(x, w - x) < lx or min(y, h - y) < ly else fill

    return paint(w, h, sample)


def flat_piece(w, h, color):
    return paint(w, h, lambda x, y: color)


def glyph_arrow(w, h):
    def sample(x, y):
        return GLYPH if triangle_inside(x, y, (0.0, h), (w, h), (w / 2.0, 0.0)) else TRANSPARENT

    return paint(w, h, sample)


def snap_stroke(v, half_texels):
    """Move a stroke centre, in pixels, so the edges of a stroke half_texels wide land on whole texels."""
    return (round(v * SCALE - half_texels) + half_texels) / SCALE


def snap_point(v):
    """Move a solid shape corner, in pixels, onto a texel edge."""
    return round(v * SCALE) / SCALE


def quad_inside(x, y, a, b, c, d):
    return triangle_inside(x, y, a, b, c) or triangle_inside(x, y, a, c, d)


def line_intersection(p, d, q, e):
    """Point where p + t*d meets q + u*e, None when they are parallel."""
    den = d[0] * e[1] - d[1] * e[0]
    if abs(den) < 1e-9:
        return None
    t = ((q[0] - p[0]) * e[1] - (q[1] - p[1]) * e[0]) / den
    return (p[0] + t * d[0], p[1] + t * d[1])


def stroke_shapes(points, radius):
    """Convex quads covering a polyline stroke with butt caps and mitred joins."""
    quads = []
    dirs = []
    for a, b in zip(points, points[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        d = (dx / length, dy / length)
        n = (-d[1] * radius, d[0] * radius)
        dirs.append(d)
        quads.append(((a[0] + n[0], a[1] + n[1]), (b[0] + n[0], b[1] + n[1]),
                      (b[0] - n[0], b[1] - n[1]), (a[0] - n[0], a[1] - n[1])))
    for i in range(1, len(points) - 1):
        p, d1, d2 = points[i], dirs[i - 1], dirs[i]
        for side in (1.0, -1.0):
            a = (p[0] - d1[1] * radius * side, p[1] + d1[0] * radius * side)
            b = (p[0] - d2[1] * radius * side, p[1] + d2[0] * radius * side)
            m = line_intersection(a, d1, b, d2)
            if m is not None:
                quads.append((p, a, m, b))
    return quads


def stroke_glyph(w, h, polylines=(), arcs=(), solids=(), box=None):
    """Hit test for a glyph on the 16 unit design grid, fitted to the largest square centred in box (default the piece).
    polylines: lists of grid points stroked with butt caps and mitred joins; arcs: (cx, cy, r, start, sweep) in grid
    units and degrees, y down, with flat radial ends; solids: filled triangles of grid points.
    One stroke of GLYPH_STROKE_UNITS; stroke centres and solid corners snapped to whole texels."""
    bx, by, bw, bh = box if box else (0.0, 0.0, w, h)
    side = min(bw, bh)
    unit = side / GLYPH_GRID
    stroke_texels = max(1, round(GLYPH_STROKE_UNITS * unit * SCALE))
    half = stroke_texels / 2.0
    radius = half / SCALE
    ox = round((bx + (bw - side) / 2.0) * SCALE) / SCALE
    oy = round((by + (bh - side) / 2.0) * SCALE) / SCALE

    def px(p):
        return (snap_stroke(ox + p[0] * unit, half), snap_stroke(oy + p[1] * unit, half))

    def corner(p):
        return (snap_point(ox + p[0] * unit), snap_point(oy + p[1] * unit))

    quads = []
    for line in polylines:
        quads += stroke_shapes([px(p) for p in line], radius)
    rings = [(px((cx, cy)), r * unit, start % 360.0, sweep) for cx, cy, r, start, sweep in arcs]
    triangles = [tuple(corner(p) for p in tri) for tri in solids]

    def hit(x, y):
        for q in quads:
            if quad_inside(x, y, *q):
                return True
        for c, rr, start, sweep in rings:
            if abs(math.hypot(x - c[0], y - c[1]) - rr) < radius:
                if (math.degrees(math.atan2(y - c[1], x - c[0])) - start) % 360.0 <= sweep:
                    return True
        for t in triangles:
            if triangle_inside(x, y, *t):
                return True
        return False

    return hit


def chevron_glyph(w, h, up, box=None):
    """The shared arrow: a sharp 90 degree stroke chevron pointing up or down."""
    if up:
        points = [(4.5, 9.75), (8.0, 6.25), (11.5, 9.75)]
    else:
        points = [(4.5, 6.25), (8.0, 9.75), (11.5, 6.25)]
    return stroke_glyph(w, h, [points], box=box)


def symbol_piece(w, h, state, glyph_hit):
    """A button box in the button colours with a glyph; glyph_hit(x, y) says where the glyph is."""
    line, fill, glyph = SYMBOL_STATES[state]

    def sample(x, y):
        if edge(x, y, w, h, True, True) < SYMBOL_LINE:
            return line
        return glyph if glyph_hit(x, y) else fill

    return paint(w, h, sample)


def check_glyph(w, h):
    """A sharp two segment check, centred between its ink box centre and its heavier lower vertex."""
    return stroke_glyph(w, h, [[(3.5, 7.5), (6.5, 10.5), (12.5, 4.5)]])


def no_glyph(x, y):
    return False


def return_glyph(w, h):
    """An angular enter arrow: a stem down from the top right, then left into a solid triangular head."""
    return stroke_glyph(w, h, [[(12.0, 3.5), (12.0, 10.0), (7.0, 10.0)]],
                        solids=[((3.0, 10.0), (7.0, 6.5), (7.0, 13.5))])


def close_glyph(w, h):
    """An X with square ends."""
    return stroke_glyph(w, h, [[(4.5, 4.5), (11.5, 11.5)], [(11.5, 4.5), (4.5, 11.5)]])


def refresh_glyph(w, h):
    """A 270 degree arc with flat ends, open at the top right, and a solid triangular head on its top end."""
    return stroke_glyph(w, h, arcs=[(8.0, 9.0, 4.2, 0.0, 270.0)],
                        solids=[((11.2, 4.8), (8.0, 2.3), (8.0, 7.3))])


def ring_piece(w, h, line, fill):
    """A map start marker: a ring with the even 2 unit glyph stroke, filled inside."""
    cx, cy, r = w / 2.0, h / 2.0, min(w, h) / 2.0 - 1.0
    stroke = GLYPH_STROKE_UNITS * min(w, h) / GLYPH_GRID

    def sample(x, y):
        d = r - math.hypot(x - cx, y - cy)
        if d < 0.0:
            return TRANSPARENT
        return line if d < stroke else fill

    return paint(w, h, sample)


def bar_piece(w, h, color, width):
    """A vertical plain bar of the given width, centred, with a pixel of room above and below."""
    left, right = (w - width) / 2.0, (w + width) / 2.0
    return paint(w, h, lambda x, y: color if left <= x < right and 1.0 <= y < h - 1.0 else TRANSPARENT)


def chevron_ink_glyph(w, h):
    """The up chevron_glyph stroke chevron, fitted by its ink rather than its design grid to the piece and centred in it."""
    radius = GLYPH_STROKE_UNITS / 2.0
    corner = radius * math.sqrt(0.5)  # a butt cap end reaches this far past its point on both axes
    ink_w = 7.0 + 2.0 * corner
    ink_h = 3.5 + math.sqrt(2.0) * radius + corner
    unit = min(w / ink_w, h / ink_h)
    cx, top = w / 2.0, (h - ink_h * unit) / 2.0
    apex = top + math.sqrt(2.0) * radius * unit
    feet = apex + 3.5 * unit
    points = [(cx - 3.5 * unit, feet), (cx, apex), (cx + 3.5 * unit, feet)]
    quads = stroke_shapes(points, radius * unit)
    return lambda x, y: any(quad_inside(x, y, *q) for q in quads)


def scroll_chevron_piece(w, h, up, glyph):
    """A scroll arrow chevron filling its piece, no cell: the engine sizes it and centres it on the scroll column line."""
    hit = chevron_ink_glyph(w, h)
    texels = paint(w, h, lambda x, y: glyph if hit(x, y) else TRANSPARENT)
    return texels if up else texels[::-1]


def pieces():
    """(image name, width, height, texels). Sizes are the retail Coords sizes of each name."""
    out = []
    for state, (line, fill) in BUTTON_STATES.items():
        out.append((f"Buttons-{state}Left", 45, 40, button_piece(45, 40, "left", line, fill, LINE_LAYOUT)))
        out.append((f"Buttons-{state}Middle", 10, 40, button_piece(10, 40, "middle", line, fill, LINE_LAYOUT)))
        out.append((f"Buttons-{state}Right", 45, 40, button_piece(45, 40, "right", line, fill, LINE_LAYOUT)))
    for state, (line, fill) in RADIO_STATES.items():
        out.append((f"RadioButton{state}Left", 34, 24, button_piece(34, 24, "left", line, fill, LINE_LAYOUT)))
        out.append((f"RadioButton{state}Middle", 10, 24, button_piece(10, 24, "middle", line, fill, LINE_LAYOUT)))
        out.append((f"RadioButton{state}Right", 34, 24, button_piece(34, 24, "right", line, fill, LINE_LAYOUT)))
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
    for state, color in SCROLL_THUMBS.items():
        out.append((f"ScrollBarThumb{state}", 21, 22, bar_piece(21, 22, color, SCROLL_THUMB_WIDTH)))
    for name, (state, checked) in CHECK_BOXES.items():
        out.append((name, 22, 22, symbol_piece(22, 22, state, check_glyph(22, 22) if checked else no_glyph)))
    for name, state in RETURN_BUTTONS.items():
        out.append((name, 32, 22, symbol_piece(32, 22, state, return_glyph(32, 22))))
    for name, state in CLOSE_BUTTONS.items():
        out.append((name, 21, 21, symbol_piece(21, 21, state, close_glyph(21, 21))))
    for name, state in REFRESH_BUTTONS.items():
        out.append((name, 24, 24, symbol_piece(24, 24, state, refresh_glyph(24, 24))))
    for name, (line, fill) in PLAYER_STARTS.items():
        out.append((name, 25, 25, ring_piece(25, 25, line, fill)))
    out.append(("arrow", 13, 16, glyph_arrow(13, 16)))
    out.append(("hilightedbox", 11, 13, box_piece(11, 13, *SLIDER_ON)))
    out.append(("dehilightedbox", 11, 13, box_piece(11, 13, *SLIDER_OFF)))
    out.append(("linebox", 13, 15, flat_piece(13, 15, SLIDER_HOVER)))
    # track and fill: one height, flat, square ends
    th = SLIDER_TRACK_HEIGHT
    for part in ("Left", "Center", "Right"):
        out.append((f"GOSliderTrack{part}", th // 2, th, flat_piece(th // 2, th, SLIDER_TRACK)))
        out.append((f"GOSliderFill{part}", th // 2, th, flat_piece(th // 2, th, SLIDER_FILL)))
    mw, mh = SLIDER_MARKER_SIZE
    for state, color in SLIDER_MARKER.items():
        out.append((f"GOSliderThumb{state}", mw, mh, flat_piece(mw, mh, color)))
    # list scroll bar: a channel the thumb fills, like the slider track, then the arrow chevrons and the separator
    pw, ph = SCROLL_PILL_ART
    for part in ("Top", "Center", "Bottom"):
        out.append((f"GOScrollTrack{part}", pw, ph, flat_piece(pw, ph, SCROLL_TRACK)))
        for state, color in SCROLL_PILLS.items():
            out.append((f"GOScrollThumb{state}{part}", pw, ph, flat_piece(pw, ph, color)))
    cw, ch = SCROLL_CHEVRON_ART
    for state, glyph in SCROLL_ARROWS.items():
        out.append((f"GOScrollUp{state}", cw, ch, scroll_chevron_piece(cw, ch, True, glyph)))
        out.append((f"GOScrollDown{state}", cw, ch, scroll_chevron_piece(cw, ch, False, glyph)))
    out.append(("GOScrollLine", 4, 4, flat_piece(4, 4, SCROLL_LINE)))
    return out


# =================================================================================================
# GO map select: map preview icons and the map list favourite stars.
# They live in their own atlas, GeneralsOnline_UIKit2.tga, because the first atlas is full.
# Self-contained: from the kit above this section uses only rgba, TRANSPARENT, SCALE and paint.
# Kit icon style: angular C&C symbols on a 16 unit design grid scaled to the piece, one 2 unit stroke
# with butt caps and mitred joins, sharp corners, even padding and points snapped to whole texels.
# Map icons are lit glyphs: a rounded edge shaded from the top left, a gloss, a thin edge in a dark shade of the icon colour and a soft shadow.
# =================================================================================================

TEXTURE2_NAME = "GeneralsOnline_UIKit2.tga"
TEXTURE2_PATH = DATA / "Art" / "Textures" / TEXTURE2_NAME
INI2_PATH = DATA / "Data" / "INI" / "MappedImages" / "TextureSize_512" / "ZZ_GeneralsOnline_UIKit2.INI"
ATLAS2_WIDTH = 128  # in retail pixels
TITLE2 = "the map preview icons and the favourite stars"

MAP_GRID = 16.0      # design grid units across a piece
MAP_STROKE = 2.0     # stroke weight in grid units
MAP_OUTLINE = 0.6    # thin dark edge around each glyph, in grid units
MAP_GLYPH_SCALE = 1.25  # glyphs are drawn about the grid centre at this scale to fill the icon with even padding
MAP_ICON_SIZE = 15   # retail px of the art; the engine draws the preview markers at a scaled layout size
STAR_SIZE = 12

MAP_GOLD = rgba("#FFD34D")
MAP_BEVEL = 1.5      # grid units over which a glyph rounds off towards its edge
MAP_BEVEL_STRENGTH = 1.8  # how steep the rounded edge reads in the lighting
MAP_SHADOW = (0.45, 0.65)  # drop shadow offset in grid units, the light comes from the top left
MAP_SHADOW_ALPHA = 0.32
MAP_LIGHT = (-0.45, -0.65, 0.62)  # towards the light, z points at the viewer
MAP_SPECULAR = 0.6


def map_ramp(light, mid, dark, edge):
    """A glyph colour ramp: the light and dark ends of the vertical gradient, the base colour and the thin edge colour."""
    return (rgba(light), rgba(mid), rgba(dark), rgba(edge, 0.95))


# the ramps follow the retail map icons: the glossy green Cash dollar and the bevelled gold TecBuilding star
MAP_GREEN = map_ramp("#D9F26B", "#5FBF2A", "#0B6E1C", "#052F0F")
MAP_GOLD_RAMP = map_ramp("#FFF2A8", "#E5BE3A", "#8A6410", "#463105")
MAP_STEEL = map_ramp("#F6F8FB", "#B2BDCB", "#586677", "#1E252E")
MAP_AMBER = map_ramp("#FFD978", "#EE8E16", "#823F05", "#3D1E08")
MAP_WOOD = map_ramp("#EDBE72", "#B97A31", "#5F3611", "#2E1806")
MAP_WOOD_LIGHT = map_ramp("#F7D69A", "#D4994A", "#7A4A1A", "#2E1806")
# image name -> the parts drawn over each other from the bottom, each a (glyph pieces, colour ramp)
MAP_ICONS = {
    "GOMapIconSupplyDock": (("dollar", MAP_GREEN),),
    "GOMapIconSupplyCrates": (("crate", MAP_WOOD), ("brace", MAP_WOOD_LIGHT)),
    "GOMapIconRepairBay": (("wrench", MAP_STEEL),),
    "GOMapIconArtillery": (("cannon", MAP_STEEL),),
    "GOMapIconOilDerrick": (("droplet", MAP_AMBER),),
    "GOMapIconTech": (("star", MAP_GOLD_RAMP),),
}
STAR_ON = (MAP_GOLD, rgba("#B8901A"))  # fill, edge
STAR_OFF = rgba("#5D6080")


def map_snap(px):
    """Snaps a retail pixel coordinate to a whole texel."""
    return round(px * SCALE) / SCALE


def map_point(size, gx, gy):
    """A grid point in retail pixels of a size px piece, snapped to a whole texel."""
    unit = size / MAP_GRID
    return (map_snap(gx * unit), map_snap(gy * unit))


def map_gp(size, gx, gy):
    """A glyph grid point scaled about the grid centre by MAP_GLYPH_SCALE, snapped to a whole texel."""
    return map_point(size, 8.0 + (gx - 8.0) * MAP_GLYPH_SCALE, 8.0 + (gy - 8.0) * MAP_GLYPH_SCALE)


def map_inside(x, y, pts):
    """Nonzero winding point in polygon test, so mitred offsets with small self loops still fill."""
    winding = 0
    for (ax, ay), (bx, by) in zip(pts, pts[1:] + pts[:1]):
        cross = (bx - ax) * (y - ay) - (x - ax) * (by - ay)
        if ay <= y < by and cross > 0.0:
            winding += 1
        elif by <= y < ay and cross < 0.0:
            winding -= 1
    return winding != 0


def map_normal(a, b):
    """Unit normal on the right of a -> b in y down coordinates."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    return (-dy / length, dx / length)


def map_mitre(p, n1, n2, d, limit):
    """The corner at p of two edges offset by d along their normals, joined with a mitre clamped to limit * d."""
    mx, my = n1[0] + n2[0], n1[1] + n2[1]
    length = math.hypot(mx, my)
    if length < 1e-9:
        return (p[0] + n1[0] * d, p[1] + n1[1] * d)
    mx, my = mx / length, my / length
    reach = d / max(mx * n1[0] + my * n1[1], 1e-9)
    if abs(reach) > limit * abs(d):
        reach = math.copysign(limit * abs(d), reach)
    return (p[0] + mx * reach, p[1] + my * reach)


def map_offset(pts, d, limit=2.0):
    """A closed polygon grown by d (shrunk when d < 0) with mitred corners."""
    area = sum(ax * by - bx * ay for (ax, ay), (bx, by) in zip(pts, pts[1:] + pts[:1]))
    sign = -1.0 if area > 0.0 else 1.0  # in y down coordinates the right hand normal points inward on a positive area polygon
    n = len(pts)
    normals = [map_normal(pts[i], pts[(i + 1) % n]) for i in range(n)]
    return [map_mitre(pts[i], normals[i - 1], normals[i], sign * d, limit) for i in range(n)]


def map_stroke_poly(points, width, limit=2.0):
    """An open polyline stroked as one polygon with butt caps and mitred joins."""
    half = width / 2.0
    normals = [map_normal(a, b) for a, b in zip(points, points[1:])]
    left, right = [], []
    for i, p in enumerate(points):
        n1 = normals[max(i - 1, 0)]
        n2 = normals[min(i, len(normals) - 1)]
        left.append(map_mitre(p, n1, n2, half, limit))
        right.append(map_mitre(p, n1, n2, -half, limit))
    return left + right[::-1]


def map_frame(size, centre, angle, local):
    """Grid polygon given in a frame rotated by angle degrees about a grid centre, as glyph points."""
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    return [map_gp(size, centre[0] + u * c - v * s, centre[1] + u * s + v * c) for u, v in local]


def map_regular(size, centre, radius, sides, angle=0.0):
    """A regular polygon in glyph grid units."""
    return [map_gp(size, centre[0] + radius * math.cos(math.radians(angle + 360.0 * i / sides)),
                   centre[1] + radius * math.sin(math.radians(angle + 360.0 * i / sides))) for i in range(sides)]


# A glyph is a list of shapes: (outer polygon, hole polygons, cut polygons). Holes are part of the
# shape and get the outline; cuts only knock the glyph colour out, so the outline shows through them.

def map_wrench(size):
    """A straight handle and a squared head with a square jaw facing the top right."""
    unit = size / MAP_GRID
    handle = map_stroke_poly([map_gp(size, 4.6, 11.4), map_gp(size, 9.2, 6.8)], MAP_STROKE * unit)  # runs into the head, no seam
    head = map_frame(size, (9.9, 6.1), -45.0, [(-1.6, -1.9), (1.9, -1.9), (1.9, -0.8), (0.3, -0.8),
                                               (0.3, 0.8), (1.9, 0.8), (1.9, 1.9), (-1.6, 1.9)])
    return [(handle, [], []), (head, [], [])]


def map_cannon(size):
    """A rectangular barrel raised to the top right over an octagonal wheel, kept apart by a knockout gap."""
    unit = size / MAP_GRID
    wheel = map_regular(size, (6.6, 10.4), 2.3, 8, 22.5)
    hub = map_regular(size, (6.6, 10.4), 0.9, 8, 22.5)
    barrel = map_stroke_poly([map_gp(size, 5.0, 8.6), map_gp(size, 11.8, 5.0)], MAP_STROKE * unit)
    return [(barrel, [], [map_offset(wheel, 0.6 * unit)]), (wheel, [hub], [])]


def map_bezier(size, p0, c1, c2, p1, steps=14):
    """Points of a cubic Bezier curve given in grid units, in retail pixels and not snapped, so a curve stays smooth."""
    unit = size / MAP_GRID
    pts = []
    for i in range(steps + 1):
        t = i / steps
        u = 1.0 - t
        x = u * u * u * p0[0] + 3.0 * u * u * t * c1[0] + 3.0 * u * t * t * c2[0] + t * t * t * p1[0]
        y = u * u * u * p0[1] + 3.0 * u * u * t * c1[1] + 3.0 * u * t * t * c2[1] + t * t * t * p1[1]
        pts.append((x * unit, y * unit))
    return pts


def map_dollar(size):
    """A curved S stroked over a vertical bar that shows above and below it, like the retail Cash dollar."""
    unit = size / MAP_GRID
    curve = map_bezier(size, (11.2, 5.0), (10.6, 2.2), (5.0, 2.2), (4.8, 5.4))
    curve += map_bezier(size, (4.8, 5.4), (4.6, 8.2), (11.4, 7.8), (11.2, 10.6))[1:]
    curve += map_bezier(size, (11.2, 10.6), (11.0, 13.8), (5.4, 13.8), (4.6, 11.0))[1:]
    bar = [(8.0 * unit, 1.4 * unit), (8.0 * unit, 14.6 * unit)]
    return [(map_stroke_poly(curve, 2.4 * unit), [], []), (map_stroke_poly(bar, 1.5 * unit), [], [])]


def map_crate(size):
    """A wooden crate face with two plank gaps cut across it."""
    unit = size / MAP_GRID

    def p(x, y):
        return (x * unit, y * unit)

    body = [p(3.4, 4.0), p(12.6, 4.0), p(13.4, 4.8), p(13.4, 13.2), p(12.6, 14.0), p(3.4, 14.0), p(2.6, 13.2), p(2.6, 4.8)]
    gaps = [[p(2.9, 7.4), p(13.1, 7.4), p(13.1, 7.9), p(2.9, 7.9)], [p(2.9, 10.3), p(13.1, 10.3), p(13.1, 10.8), p(2.9, 10.8)]]
    return [(body, [], gaps)]


def map_brace(size):
    """The diagonal plank across the crate."""
    unit = size / MAP_GRID
    return [(map_stroke_poly([(3.8 * unit, 13.0 * unit), (12.2 * unit, 5.0 * unit)], 1.6 * unit), [], [])]


def map_droplet(size):
    """A smooth teardrop with a pointed top."""
    unit = size / MAP_GRID
    right = map_bezier(size, (8.0, 1.8), (9.4, 4.6), (12.8, 6.8), (12.8, 10.0))
    right += map_bezier(size, (12.8, 10.0), (12.8, 12.8), (10.6, 14.4), (8.0, 14.4))[1:]
    left = [(16.0 * unit - x, y) for x, y in reversed(right[1:-1])]
    return [(right + left, [], [])]


def map_star_points(size):
    """A five point star filling the piece with even padding; the points stay sharp."""
    unit = size / MAP_GRID
    cx, cy = map_point(size, 8.0, 8.6)
    outer, inner = 7.4 * unit, 3.0 * unit
    pts = []
    for i in range(10):
        a = math.radians(-90.0 + 36.0 * i)
        r = outer if i % 2 == 0 else inner
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def map_edge_distance(x, y, pts):
    """Distance from a point to the nearest edge of a closed polygon."""
    best = None
    for (ax, ay), (bx, by) in zip(pts, pts[1:] + pts[:1]):
        dx, dy = bx - ax, by - ay
        k = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
        d = math.hypot(x - ax - k * dx, y - ay - k * dy)
        best = d if best is None else min(best, d)
    return best


MAP_PIECES = {
    "dollar": map_dollar, "crate": map_crate, "brace": map_brace, "wrench": map_wrench, "cannon": map_cannon,
    "droplet": map_droplet, "star": lambda size: [(map_star_points(size), [], [])],
}


def map_mix(a, b, k):
    """A colour k of the way from a to b, k clamped to 0 .. 1."""
    k = max(0.0, min(1.0, k))
    return tuple(a[i] + (b[i] - a[i]) * k for i in range(4))


def map_ramp_color(ramp, t):
    """The ramp colour at t, 0 at the top of the glyph and 1 at the bottom."""
    light, mid, dark, _ = ramp
    return map_mix(light, mid, t * 2.0) if t < 0.5 else map_mix(mid, dark, (t - 0.5) * 2.0)


def map_depth(x, y, pieces):
    """How far inside a part a point is: the distance to its nearest edge, the deepest piece where pieces overlap, None outside."""
    best = None
    for outer, holes, cuts in pieces:
        if not map_inside(x, y, outer) or any(map_inside(x, y, h) for h in holes) or any(map_inside(x, y, c) for c in cuts):
            continue
        d = min([map_edge_distance(x, y, outer)] + [map_edge_distance(x, y, h) for h in holes + cuts])
        best = d if best is None else max(best, d)
    return best


def map_height(x, y, pieces, bevel):
    """The height of a part at a point: 0 outside, rising smoothly to 1 over the bevel width inside its edge."""
    d = map_depth(x, y, pieces)
    if d is None:
        return 0.0
    t = min(1.0, d / bevel)
    return t * t * (3.0 - 2.0 * t)


def map_lit(x, y, pieces, ramp, v, unit):
    """The colour of a point of a part: the ramp at v, shaded by the surface normal of the rounded edge and glossed where it faces the light."""
    bevel = MAP_BEVEL * unit
    e = 0.15
    hx = (map_height(x + e, y, pieces, bevel) - map_height(x - e, y, pieces, bevel)) / (2.0 * e)
    hy = (map_height(x, y + e, pieces, bevel) - map_height(x, y - e, pieces, bevel)) / (2.0 * e)
    nx, ny, nz = -hx * MAP_BEVEL_STRENGTH, -hy * MAP_BEVEL_STRENGTH, 1.0
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / length, ny / length, nz / length
    lx, ly, lz = MAP_LIGHT
    length = math.sqrt(lx * lx + ly * ly + lz * lz)
    lx, ly, lz = lx / length, ly / length, lz / length
    shade = max(0.4, min(1.7, 1.0 + 1.3 * (nx * lx + ny * ly + nz * lz - lz)))
    gx, gy, gz = lx, ly, lz + 1.0
    length = math.sqrt(gx * gx + gy * gy + gz * gz)
    gloss = max(0.0, (nx * gx + ny * gy + nz * gz) / length) ** 24 * MAP_SPECULAR
    base = map_ramp_color(ramp, v)
    return (min(1.0, base[0] * shade + gloss), min(1.0, base[1] * shade + gloss), min(1.0, base[2] * shade + gloss), 1.0)


def map_icon_piece(size, name):
    """A map icon lit like the retail map icons: rounded and shaded from the top left with a gloss, a thin edge in a dark shade
    of its own colour and a soft drop shadow, which keep it readable on snow, sand and grass."""
    unit = size / MAP_GRID
    parts = [(MAP_PIECES[glyph](size), ramp) for glyph, ramp in MAP_ICONS[name]]
    ys = [y for pieces, _ in parts for outer, _, _ in pieces for _, y in outer]
    top, bottom = min(ys), max(ys)
    grown = [(map_offset(outer, MAP_OUTLINE * unit), [map_offset(h, -MAP_OUTLINE * unit) for h in holes], ramp)
             for pieces, ramp in parts for outer, holes, _ in pieces]
    shadow = (MAP_SHADOW[0] * unit, MAP_SHADOW[1] * unit)

    def surface(x, y):
        for pieces, ramp in reversed(parts):
            if map_depth(x, y, pieces) is not None:
                return pieces, ramp
        return None

    def sample(x, y):
        hit = surface(x, y)
        if hit is not None:
            return map_lit(x, y, hit[0], hit[1], (y - top) / (bottom - top), unit)
        for outer, holes, ramp in reversed(grown):
            if map_inside(x, y, outer) and not any(map_inside(x, y, h) for h in holes):
                return ramp[3]
        if surface(x - shadow[0], y - shadow[1]) is not None:
            return (0.0, 0.0, 0.0, MAP_SHADOW_ALPHA)
        return TRANSPARENT

    return paint(size, size, sample)


def map_star_piece(size, fill, edge, edge_width):
    """A five point star with an edge band inside its silhouette; fill None draws the outline only.
    The silhouette is the star polygon itself, so the points and corners stay sharp and mitred."""
    unit = size / MAP_GRID
    pts = map_star_points(size)
    band = edge_width * unit

    def sample(x, y):
        if not map_inside(x, y, pts):
            return TRANSPARENT
        if map_edge_distance(x, y, pts) < band:
            return edge
        return fill if fill is not None else TRANSPARENT

    return paint(size, size, sample)


def pieces2():
    """The second atlas: map preview icons and the favourite stars."""
    out = []
    for name in MAP_ICONS:
        out.append((name, MAP_ICON_SIZE, MAP_ICON_SIZE, map_icon_piece(MAP_ICON_SIZE, name)))
    out.append(("GOMapFavouriteOn", STAR_SIZE, STAR_SIZE, map_star_piece(STAR_SIZE, STAR_ON[0], STAR_ON[1], 1.0)))
    # the inside band shows the inner half of a centred 2 unit stroke plus a little, so the outline star stays open
    out.append(("GOMapFavouriteOff", STAR_SIZE, STAR_SIZE, map_star_piece(STAR_SIZE, None, STAR_OFF, 1.25)))
    return out

# ============================== end of the GO map select section ==============================


# =================================================================================================
# Load road: the GO shell load bar drawn as a road seen from the side, the ground the radar van drives on. Asphalt between
# two kit lines (the three pieces carry Border = LINE_LAYOUT, so the lines keep the kit weight), a dashed centre line, and
# a bronze tint with lit dashes behind the van. Its own atlas. The Retail style leaves the files out and keeps the retail bar. LoadScreen.cpp drawGoShellLoadBar draws it over the
# window ShellGameLoadScreen::init sets to LOAD_ROAD_RECT; load_road_dashes mirrors where the dashes go.
# =================================================================================================

LOAD_ROAD_TEXTURE_NAME = "GeneralsOnline_LoadRoad.tga"
LOAD_ROAD_TEXTURE_PATH = DATA / "Art" / "Textures" / LOAD_ROAD_TEXTURE_NAME
LOAD_ROAD_INI_PATH = DATA / "Data" / "INI" / "MappedImages" / "TextureSize_512" / "ZZ_GeneralsOnline_LoadRoad.INI"
LOAD_ROAD_ATLAS_WIDTH = 32  # in retail pixels
LOAD_ROAD_TITLE = "the load screen road"
LOAD_ROAD_RECT = (267, 574, 533, 592)  # 800x600 units: a third of the width, centred, near the bottom


def mix(a, b, t):
    """Blend two palette colours, t of b; alpha 1."""
    ca, cb = rgba(a), rgba(b)
    return tuple(round(((1 - t) * x + t * y) * 255) / 255 for x, y in zip(ca[:3], cb[:3])) + (1.0,)


LOAD_ROAD_LINE = BUTTON_STATES[""][0]  # the kit line
LOAD_ROAD_ASPHALT = mix(go_themes.PALETTE["ground"], go_themes.PALETTE["pushed"], 0.4)  # a warm near-black grey
LOAD_ROAD_DASH = rgba(go_themes.PALETTE["empty"], 0.55)  # the road ahead
LOAD_ROAD_DASH_LIT = rgba(go_themes.PALETTE["tan"])      # the road behind the van
LOAD_ROAD_TINT = rgba(go_themes.PALETTE["bronze"], 0.18)
LOAD_ROAD_DASH_UNITS = 6.0  # half a dash period, in 800x600 units


def road_piece(w, h, side, line, fill, width):
    """Asphalt between a top and a bottom edge line; caps round their outer corners by the line width."""
    left, right = ends(side)

    def sample(x, y):
        for on, cx, outside_x in ((left, width, x < width), (right, w - width, x > w - width)):
            if on and outside_x:
                for cy, outside_y in ((width, y < width), (h - width, y > h - width)):
                    if outside_y and math.hypot(x - cx, y - cy) > width:
                        return TRANSPARENT
        return line if min(y, h - y) < width else fill

    return paint(w, h, sample)


def load_road_pieces():
    """The road at its art sizes: two caps and a tiled middle (Border pieces), the dash in two states and the tint."""
    return [
        ("GORoadL", 8, 18, road_piece(8, 18, "left", LOAD_ROAD_LINE, LOAD_ROAD_ASPHALT, LINE_LAYOUT)),
        ("GORoadC", 8, 18, road_piece(8, 18, "middle", LOAD_ROAD_LINE, LOAD_ROAD_ASPHALT, LINE_LAYOUT)),
        ("GORoadR", 8, 18, road_piece(8, 18, "right", LOAD_ROAD_LINE, LOAD_ROAD_ASPHALT, LINE_LAYOUT)),
        ("GORoadDash", 4, 2, flat_piece(4, 2, LOAD_ROAD_DASH)),
        ("GORoadDashLit", 4, 2, flat_piece(4, 2, LOAD_ROAD_DASH_LIT)),
        ("GORoadTint", 4, 4, flat_piece(4, 4, LOAD_ROAD_TINT)),
    ]


def layout_scale(value, numerator, denominator):
    """800x600 units to pixels as parseScreenRect scales a rect."""
    return (value * numerator + denominator // 2) // denominator


def load_road_window(screen_width, screen_height):
    """The road window ShellGameLoadScreen::init sets, in pixels: (x, y, width, height)."""
    left, top, right, bottom = LOAD_ROAD_RECT
    return (layout_scale(left, screen_width, 800), layout_scale(top, screen_height, 600),
            layout_scale(right - left, screen_width, 800), layout_scale(bottom - top, screen_height, 600))


def load_road_dashes(x, y, width, height, screen_height):
    """drawGoShellLoadBar's centre line: (line, [dash rects]). Dashes are whole pixels on an even period, centred in the
    road with the same padding at both ends, and as thick as the kit line (one more pixel when that centres them)."""
    line = max(1, math.ceil(LINE_LAYOUT * screen_height / 600.0 - 0.01))
    thickness = line + ((height - line) & 1)
    half = max(2, math.floor(LOAD_ROAD_DASH_UNITS * screen_height / 600.0 + 0.5))
    period = 2 * half
    length = max(2, 2 * (half // 2))
    span = width - 2 * (height // 2)
    count = (span - length) // period + 1 if span >= length else 0
    total = (count - 1) * period + length if count else 0
    # a road of odd width takes one more pixel per dash, so both ends keep the same whole pixel padding
    if count and (width - total) & 1:
        length, total = length + 1, total + 1
    pad = (width - total) // 2
    top = y + (height - thickness) // 2
    return line, [(x + pad + k * period, top, x + pad + k * period + length, top + thickness) for k in range(count)]

# ============================== end of the load road section ==============================


# =================================================================================================
# Load splash: a Minecraft style line, one per launch, picked from the wall clock (never a game random stream), said by
# the radar van of the GO logo in a speech bubble whose tail points at its dish. LoadScreen.cpp GO_LOAD_SPLASHES holds the
# same lines as FETCH_OR_SUBSTITUTE labels; drawGoLogoBubble draws the bubble from ShellGameLoadScreen::drawCredit and
# bubble_layout mirrors it with an estimated text size. The bubble art is its own atlas, as it pairs
# with the van art; the Retail style leaves it out.
# =================================================================================================

LOAD_SPLASHES = (
    ("GUI:GOLoadSplash1", "Working eyes at your disposal."),
    ("GUI:GOLoadSplash2", "Let us see what they are up to."),
    ("GUI:GOLoadSplash3", "Nothing is private."),
    ("GUI:GOLoadSplash4", "GLA radar service."),
    ("GUI:GOLoadSplash5", "What station you need, general?"),
    ("GUI:GOLoadSplash6", "Better reception there?"),
    ("GUI:GOLoadSplash7", "Keep me out of sight."),
    ("GUI:GOLoadSplash8", "Let's go where it is safe."),
    ("GUI:GOLoadSplash9", "We get signal everywhere."),
)
SPLASH_FONT_SIZE = 16      # Consolas bold
BUBBLE_WRAP_UNITS = 260    # the wrap width, scaled like the font: long lines take two lines, short ones one
URL_FONT_SIZE = 14
CREDIT_URL_TOP = 362       # LoadScreen.cpp CREDIT_URL_TOP
LOGO_ASPECT = (1282, 494)  # GOLoadLogo Coords

BUBBLE_TEXTURE_NAME = "GeneralsOnline_LoadBubble.tga"
BUBBLE_TEXTURE_PATH = DATA / "Art" / "Textures" / BUBBLE_TEXTURE_NAME
BUBBLE_INI_PATH = DATA / "Data" / "INI" / "MappedImages" / "TextureSize_512" / "ZZ_GeneralsOnline_LoadBubble.INI"
BUBBLE_ATLAS_WIDTH = 64  # in retail pixels
BUBBLE_TITLE = "the load screen speech bubble"
BUBBLE_FILL = rgba(go_themes.PALETTE["text"])  # cream
BUBBLE_LINE = rgba(go_themes.PALETTE["ink"])   # the van's outline colour
BUBBLE_TEXT = go_themes.PALETTE["ink"]
BUBBLE_CORNER = 8          # art units of a corner piece; a 9-slice, so corners keep their size at any line count
BUBBLE_EDGE = 4            # art units of the stretched edges and the centre
BUBBLE_RADIUS = 7.0
BUBBLE_LINE_WIDTH = 1.0    # about the van art's outline at the bubble's size
BUBBLE_TAIL = (12, 10)     # width and height, the tip at the bottom centre
BUBBLE_TAIL_OVER = 1.5     # the tail's top rows cover the bubble's bottom line, so the two read as one shape
# the rules by their LoadScreen.cpp names: the logo van's dish rim in GOLoadLogo texels (logo_dish measures it) and the
# fade in, from the credit's start
BUBBLE_CONSTANTS = {"GO_LOGO_DISH_X": 306, "GO_LOGO_DISH_Y": 0, "GO_BUBBLE_SHOW_MS": 600}
BUBBLE_MARGIN_UNITS = 8.0
BUBBLE_TIP_GAP_UNITS = 2.0  # the tip stops this far over the dish rim, in 800x600 units
LOGO_TEXTURE_PATH = DATA / "Art" / "Textures" / "GeneralsOnline_LoadLogo.tga"
LOGO_COORDS = (383, 9, 1665, 503)  # GOLoadLogo Coords


def bubble_slice(w, h, left, top, right, bottom):
    """One slice of the bubble's 9-slice: cream inside an ink line BUBBLE_LINE_WIDTH wide, corners rounded where two
    closed sides meet; left/top/right/bottom say which sides of the piece are the bubble's edge."""
    r = BUBBLE_RADIUS

    def sample(x, y):
        sides = [d for d, on in ((x, left), (y, top), (w - x, right), (h - y, bottom)) if on]
        d = min(sides) if sides else float("inf")
        cx = r if left else (w - r if right else None)
        cy = r if top else (h - r if bottom else None)
        if cx is not None and cy is not None and (x < r if left else x > w - r) and (y < r if top else y > h - r):
            d = r - math.hypot(x - cx, y - cy)
        if d < 0:
            return TRANSPARENT
        return BUBBLE_LINE if d < BUBBLE_LINE_WIDTH else BUBBLE_FILL

    return paint(w, h, sample)


def bubble_tail(w, h):
    """The tail: a triangle down to its tip at the bottom centre, lined on its two sides, its top rows all cream."""
    half = w / 2.0
    drop = h - BUBBLE_TAIL_OVER
    slope = math.hypot(half, drop)

    def sample(x, y):
        if y < BUBBLE_TAIL_OVER:
            return BUBBLE_FILL
        reach = half * (1 - (y - BUBBLE_TAIL_OVER) / drop)
        if abs(x - half) > reach:
            return TRANSPARENT
        d = (reach - abs(x - half)) * drop / slope  # to the nearer slanted side
        return BUBBLE_LINE if d < BUBBLE_LINE_WIDTH else BUBBLE_FILL

    return paint(w, h, sample)


def bubble_pieces():
    c, e = BUBBLE_CORNER, BUBBLE_EDGE
    tw, th = BUBBLE_TAIL
    return [
        ("GOBubbleTL", c, c, bubble_slice(c, c, True, True, False, False)),
        ("GOBubbleT", e, c, bubble_slice(e, c, False, True, False, False)),
        ("GOBubbleTR", c, c, bubble_slice(c, c, False, True, True, False)),
        ("GOBubbleL", c, e, bubble_slice(c, e, True, False, False, False)),
        ("GOBubbleC", e, e, bubble_slice(e, e, False, False, False, False)),
        ("GOBubbleR", c, e, bubble_slice(c, e, False, False, True, False)),
        ("GOBubbleBL", c, c, bubble_slice(c, c, True, False, False, True)),
        ("GOBubbleB", e, c, bubble_slice(e, c, False, False, False, True)),
        ("GOBubbleBR", c, c, bubble_slice(c, c, False, False, True, True)),
        ("GOBubbleTail", tw, th, bubble_tail(tw, th)),
    ]


def text_size(text, point_size):
    """A generous estimate of a Consolas line in pixels: 0.75 of the point size per character, 1.6 tall."""
    return math.ceil(len(text) * 0.75 * point_size), math.ceil(1.6 * point_size)


def font_scale(screen_width, screen_height):
    """GlobalLanguage ResolutionFontSizeMethod_Strict."""
    return min(screen_width / 800.0, screen_height / 600.0)


def splash_text_block(text, screen_width, screen_height):
    """The splash as the wrapped DisplayString lays it out: (lines, width, height, line height). Greedy word wrap at
    BUBBLE_WRAP_UNITS times the font scale, with text_size's per character estimate."""
    scale = font_scale(screen_width, screen_height)
    point = int(SPLASH_FONT_SIZE * scale)
    wrap = int(BUBBLE_WRAP_UNITS * scale)
    lines = []
    for word in text.split(" "):
        candidate = f"{lines[-1]} {word}" if lines else word
        if lines and text_size(candidate, point)[0] < wrap:
            lines[-1] = candidate
        else:
            lines.append(word)
    line_height = text_size("x", point)[1]
    return lines, max(text_size(line, point)[0] for line in lines), line_height * len(lines), line_height


def logo_rect(screen_width, screen_height):
    unit = screen_height / 600.0
    top, height = int(go_themes.LOAD_LOGO_TOP * unit), int(go_themes.LOAD_LOGO_HEIGHT * unit)
    width = height * LOGO_ASPECT[0] // LOGO_ASPECT[1]
    left = screen_width // 2 - width // 2
    return left, top, left + width, top + height


def url_rect(screen_width, screen_height):
    w, h = text_size("www.generals.online", int(URL_FONT_SIZE * font_scale(screen_width, screen_height)))
    top = int(CREDIT_URL_TOP * screen_height / 600.0)
    return screen_width // 2 - w // 2, top, screen_width // 2 - w // 2 + w, top + h


def logo_dish(rows=None):
    """The logo van's radar dish rim in GOLoadLogo texels, measured from the art: the topmost opaque row of the logo is
    the rim of the dish pair on the mast, and the rim point is the middle of that row's opaque run."""
    if rows is None:
        rows = go_themes.read_tga(LOGO_TEXTURE_PATH.read_bytes())[2]
    left, top, right, bottom = LOGO_COORDS
    for y in range(top, bottom):
        xs = [x for x in range(left, right) if rows[y][x][3] > 128]
        if xs:
            return (min(xs) + max(xs)) // 2 - left, y - top
    raise SystemExit("the logo art is empty")


def logo_dish_screen(screen_width, screen_height):
    """drawCredit: the dish rim on screen, through the logo's own placement."""
    left, top, right, bottom = logo_rect(screen_width, screen_height)
    return (left + BUBBLE_CONSTANTS["GO_LOGO_DISH_X"] * (right - left) // LOGO_ASPECT[0],
            top + BUBBLE_CONSTANTS["GO_LOGO_DISH_Y"] * (bottom - top) // LOGO_ASPECT[1])


def bubble_tip(screen_width, screen_height):
    """Where the tail points: BUBBLE_TIP_GAP_UNITS over the dish rim."""
    x, y = logo_dish_screen(screen_width, screen_height)
    return x, y - int(BUBBLE_TIP_GAP_UNITS * screen_height / 600.0)


def bubble_layout(tip, text_width, text_height, line_height, screen_width, screen_height):
    """drawGoLogoBubble: {"bubble", "corner", "tail", "text"}. Every size follows the font's line height: corners of
    3/5 line, padding of a quarter line above and below and half a line at the sides, so the bubble grows upward by a
    line per wrapped line from a bottom edge fixed over the tip. Three quarters of it sit left of the tip, kept on screen
    with a margin; the tail hangs from the bottom to the tip, sliding along the straight part when it is pulled back."""
    tip_x, tip_y = tip
    corner = line_height * 3 // 5
    tail_w, tail_h, over = line_height * 9 // 10, line_height * 3 // 4, line_height * 9 // 80
    height = max(text_height + line_height // 2, 2 * corner)
    width = max(text_width + line_height, 2 * corner + tail_w)
    margin = int(BUBBLE_MARGIN_UNITS * screen_height / 600.0)
    left = max(margin, min(tip_x - width * 3 // 4, screen_width - margin - width))
    bottom = tip_y - tail_h + over
    top = bottom - height
    centre = max(left + corner + tail_w // 2, min(tip_x, left + width - corner - tail_w // 2))
    tail_left, tail_top = centre - tail_w // 2, bottom - over
    text_left, text_top = left + (width - text_width) // 2, top + (height - text_height) // 2
    return {"bubble": (left, top, left + width, bottom), "corner": corner,
            "tail": (tail_left, tail_top, tail_left + tail_w, tail_top + tail_h),
            "text": (text_left, text_top, text_left + text_width, text_top + text_height)}


def bubble_alpha(shown_ms):
    """Hidden until GO_BUBBLE_SHOW_MS after the credit's start, when the logo has faded in, then whole; it stays."""
    return 255 if shown_ms >= BUBBLE_CONSTANTS["GO_BUBBLE_SHOW_MS"] else 0


def splash_pick(now):
    """The splash index, picked once per launch from the clock."""
    return now % len(LOAD_SPLASHES)

# ============================== end of the load splash section ==============================


# =================================================================================================
# Load bar van: the GO radar van in side view drives along the shell load road, wheels turning while progress moves,
# dust puffs behind the rear wheel and the body bobbing on its suspension. Every image is drawn on the same 256x156
# canvas, so every layer shares one origin; Coords are texels. LoadScreen.cpp drawGoLoadBarVan draws it from
# drawGoShellLoadBar only; the other bars stay plain. The van_* functions below mirror its rules.
# =================================================================================================

VAN_SOURCES = Path(__file__).resolve().parent / "art" / "van-side"
# the layers, drawn body, dish, wheels on one canvas, and the puff; the full van frames are the reference they stack to
VAN_LAYER_FILES = {"body": "van-layer-body-nodish.png", "dish": "van-layer-dish.png",
                   "wheels": "van-layer-wheels-frames.png", "puff": "van-dust-puff.png"}
VAN_FRAMES_PNG = VAN_SOURCES / "van-side-frames-256.png"  # four full frames, wheels at 0/22.5/45/67.5 degrees
VAN_TEXTURE_NAME = "GeneralsOnline_Van.tga"
VAN_TEXTURE_PATH = DATA / "Art" / "Textures" / VAN_TEXTURE_NAME
VAN_INI_PATH = DATA / "Data" / "INI" / "MappedImages" / "TextureSize_512" / "ZZ_GeneralsOnline_Van.INI"
VAN_GAP = 2  # clear texels between images
VAN_WHEEL_FRAMES = 4

# the draw rules, by their LoadScreen.cpp names; art values are canvas texels, times milliseconds
VAN_CONSTANTS = {
    "GO_VAN_FRAME_WIDTH": 256, "GO_VAN_FRAME_HEIGHT": 156,
    "GO_VAN_OUTLINE_TOP": 5, "GO_VAN_OUTLINE_BOTTOM": 152, "GO_VAN_OUTLINE_LEFT": 14, "GO_VAN_OUTLINE_RIGHT": 247,
    "GO_VAN_TYRE_BOTTOM": 145, "GO_VAN_REAR_TYRE": 46, "GO_VAN_BUMPER": 240,
    "GO_VAN_MIN_HEIGHT": 20, "GO_VAN_WHEEL_FRAMES": VAN_WHEEL_FRAMES, "GO_VAN_FRAME_MS": 83, "GO_VAN_STALL_MS": 500,
    "GO_VAN_SETTLE_MS": 300, "GO_VAN_FORGET_MS": 1000, "GO_VAN_BOB": 2, "GO_VAN_DISH_LAG_MS": 90, "GO_VAN_WAVE_MS": 10000,
    "GO_VAN_PUFFS": 12, "GO_VAN_PUFF_SPAWN_X": 44, "GO_VAN_PUFF_SPAWN_Y": 138, "GO_VAN_PUFF_JITTER": 4,
    "GO_VAN_PUFF_DRIFT_X": 24, "GO_VAN_PUFF_DRIFT_Y": 40, "GO_VAN_PUFF_START": 16, "GO_VAN_PUFF_END": 40,
    "GO_VAN_PUFF_LIFE_MS": 500, "GO_VAN_PUFF_LIFE_SPREAD_MS": 300, "GO_VAN_PUFF_GAP_MS": 40, "GO_VAN_PUFF_TRAVEL": 50,
    "GO_VAN_PUFF_CREDIT": 3, "GO_VAN_PUFF_ALPHA": 255,
}
VAN_REAL_CONSTANTS = {"GO_VAN_BOB_HZ_A": 3.3, "GO_VAN_BOB_HZ_B": 5.1, "GO_VAN_BOB_PHASE_B": 1.3, "GO_VAN_BOB_WEIGHT_A": 0.6}
VAN = VAN_CONSTANTS
VAN_SPAN = VAN["GO_VAN_OUTLINE_BOTTOM"] - VAN["GO_VAN_OUTLINE_TOP"]  # the van height covers the outline rows


def van_strip_frames(path, frames=VAN_WHEEL_FRAMES):
    """Split a strip of frames side by side into frames of (width, height, rows of byte tuples)."""
    width, height, rows = go_themes.read_png(path.read_bytes())
    cell = width // frames
    if cell * frames != width:
        raise SystemExit(f"{path.name} is not {frames} frames wide")
    return [(cell, height, [row[k * cell:(k + 1) * cell] for row in rows]) for k in range(frames)]


def van_images(sources=VAN_SOURCES):
    """The van images as (name, width, height, rows of (r, g, b, a) bytes): body, dish and wheel frames on the 256x156
    canvas, and the puff."""
    body = go_themes.read_png((sources / VAN_LAYER_FILES["body"]).read_bytes())
    dish = go_themes.read_png((sources / VAN_LAYER_FILES["dish"]).read_bytes())
    puff = go_themes.read_png((sources / VAN_LAYER_FILES["puff"]).read_bytes())
    wheels = van_strip_frames(sources / VAN_LAYER_FILES["wheels"])
    out = [("GOVanBody",) + body, ("GOVanDish",) + dish]
    out += [(f"GOVanWheels{k}",) + frame for k, frame in enumerate(wheels)]
    return out + [("GOVanPuff",) + puff]


def van_dilate(width, height, rows):
    """Give every clear texel the colour of its nearest drawn one, alpha kept, so filtering never pulls in the white the
    art stores under its clear texels."""
    out = [list(row) for row in rows]
    frontier = [(x, y) for y in range(height) for x in range(width) if rows[y][x][3]]
    seen = {p for p in frontier}
    while frontier:
        grown = []
        for x, y in frontier:
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < width and 0 <= ny < height and (nx, ny) not in seen:
                    seen.add((nx, ny))
                    out[ny][nx] = out[y][x][:3] + (0,)
                    grown.append((nx, ny))
        frontier = grown
    if not seen:
        out = [[(0, 0, 0, 0)] * width for _ in range(height)]
    return out


def van_pack(images):
    """Shelf-pack the images with VAN_GAP clear texels between them into the smallest power-of-two texture; returns
    ({name: (left, top)}, (width, height))."""
    best = None
    for width in (256, 512, 1024, 2048):
        placements, x, y, shelf = {}, 0, 0, 0
        for name, w, h, _ in sorted(images, key=lambda it: (-it[2], -it[1], it[0])):
            if w > width:
                break
            if x + w > width:
                x, y, shelf = 0, y + shelf + VAN_GAP, 0
            placements[name] = (x, y)
            x += w + VAN_GAP
            shelf = max(shelf, h)
        else:
            height = 1
            while height < y + shelf:
                height *= 2
            if best is None or width * height < best[1][0] * best[1][1]:
                best = (placements, (width, height))
    return best


def build_van(sources=VAN_SOURCES):
    """Return the van texture TGA, its INI text and the placements {name: (left, top, width, height)}."""
    images = van_images(sources)
    placements, (width, height) = van_pack(images)
    atlas = [[TRANSPARENT] * width for _ in range(height)]
    rects = {}
    for name, w, h, rows in images:
        left, top = placements[name]
        rects[name] = (left, top, w, h)
        for y, row in enumerate(van_dilate(w, h, rows)):
            atlas[top + y][left:left + w] = [tuple(c / 255.0 for c in t) for t in row]
    lines = [
        f"; Generals Online load bar van: points the van images at {VAN_TEXTURE_NAME}.",
        "; Generated by GenOnline/tools/go_ui_kit.py from GenOnline/tools/art/van-side; edit the script, not this file. Coords are texels.",
        "",
    ]
    for name in sorted(rects, key=str.lower):
        left, top, w, h = rects[name]
        lines += [
            f"MappedImage {name}",
            f"  Texture = {VAN_TEXTURE_NAME}",
            f"  TextureWidth = {width}",
            f"  TextureHeight = {height}",
            f"  Coords = Left:{left} Top:{top} Right:{left + w} Bottom:{top + h}",
            "  Status = NONE",
            "End",
            "",
        ]
    return tga_bytes(atlas), "\r\n".join(lines), rects


def van_scale(value, height):
    """Canvas texels to pixels for a van height pixels tall over the outline rows, rounded."""
    return (value * height + VAN_SPAN // 2) // VAN_SPAN


def van_above(road):
    """The van height over a bar (about 1.8 times the road height, at least GO_VAN_MIN_HEIGHT) and the strip above the
    road it can reach at any progress, bob and puffs included: (left, top, right, bottom)."""
    left, top, right, bottom = road
    height = max(VAN["GO_VAN_MIN_HEIGHT"], (bottom - top) * 9 // 5)
    s = lambda v: van_scale(v, height)  # noqa: E731
    lowest = left + s(VAN["GO_VAN_BUMPER"] - VAN["GO_VAN_REAR_TYRE"]) - s(VAN["GO_VAN_BUMPER"])
    return height, (lowest, top - s(VAN["GO_VAN_TYRE_BOTTOM"] - VAN["GO_VAN_OUTLINE_TOP"] + VAN["GO_VAN_BOB"]), right, top)


def van_layout(road, progress, above_clear):
    """Where placeGoVan puts the van on the shell road: (height, canvas rect), or None when above_clear(strip) says
    another window is in the way. Tyres on the road's top edge; its position marks progress: the rear tyre at the road
    start at 0%, the front of the outline at the road end at 100%."""
    height, strip = van_above(road)
    if not above_clear(strip):
        return None
    s = lambda v: van_scale(v, height)  # noqa: E731
    first = road[0] + s(VAN["GO_VAN_BUMPER"] - VAN["GO_VAN_REAR_TYRE"])
    last = road[2] - s(VAN["GO_VAN_OUTLINE_RIGHT"] - VAN["GO_VAN_BUMPER"])
    bumper = first + (last - first) * max(0, min(100, progress)) // 100
    x0, y0 = bumper - s(VAN["GO_VAN_BUMPER"]), road[1] - s(VAN["GO_VAN_TYRE_BOTTOM"])
    return height, (x0, y0, x0 + s(VAN["GO_VAN_FRAME_WIDTH"]), y0 + s(VAN["GO_VAN_FRAME_HEIGHT"]))


def load_road_lit_end(road, progress):
    """Where drawGoShellLoadBar ends the lit road: by progress, so it fills the whole road at 100% under the van."""
    end = road[0] + (road[2] - road[0]) * max(0, min(100, progress)) // 100
    return max(road[0], min(road[2], end))


def van_settle(now, changed_at):
    """(moving, envelope): moving until progress has stood still GO_VAN_STALL_MS, then the wiggle fades out over
    GO_VAN_SETTLE_MS."""
    still = now - changed_at
    if still < VAN["GO_VAN_STALL_MS"]:
        return True, 1.0
    return False, max(0.0, 1.0 - (still - VAN["GO_VAN_STALL_MS"]) / VAN["GO_VAN_SETTLE_MS"])


def van_wheel_frame(now, moving, frame):
    """The wheel frame: from the clock at about 12 per second while moving, the last one while parked."""
    return (now // VAN["GO_VAN_FRAME_MS"]) % VAN["GO_VAN_WHEEL_FRAMES"] if moving else frame


def van_wave(now):
    """The suspension rhythm in -1..1: two sines at unrelated rates, so the bounce never repeats a beat. The clock wraps
    first, on GO_VAN_WAVE_MS that holds whole beats of both, so the phase keeps its precision at any uptime."""
    r = VAN_REAL_CONSTANTS
    t = (now % VAN["GO_VAN_WAVE_MS"]) / 1000.0
    return (r["GO_VAN_BOB_WEIGHT_A"] * math.sin(2 * math.pi * r["GO_VAN_BOB_HZ_A"] * t)
            + (1 - r["GO_VAN_BOB_WEIGHT_A"]) * math.sin(2 * math.pi * r["GO_VAN_BOB_HZ_B"] * t + r["GO_VAN_BOB_PHASE_B"]))


def van_wiggle(now, height, envelope):
    """(body bob, dish sway) in whole pixels: the body bobs GO_VAN_BOB canvas texels at most, the dish follows the same
    rhythm GO_VAN_DISH_LAG_MS later, one pixel sideways; both settle with the envelope."""
    bob = math.floor(VAN["GO_VAN_BOB"] * height / VAN_SPAN * envelope * van_wave(now) + 0.5)
    sway = math.floor(envelope * van_wave(now - VAN["GO_VAN_DISH_LAG_MS"]) + 0.5)
    return bob, sway


def van_hash(value):
    return (value * 2654435761) & 0xFFFFFFFF


def van_puff_life(h):
    """A puff's life from its hash: GO_VAN_PUFF_LIFE_MS plus a slice of the hash wide enough for the whole spread."""
    return VAN["GO_VAN_PUFF_LIFE_MS"] + (h >> 20) % VAN["GO_VAN_PUFF_LIFE_SPREAD_MS"]


class VanBar:
    """One bar's van state as drawGoLoadBarVan keeps it: the progress clock, the wheel frame and a fixed ring of puffs."""

    def __init__(self, now, progress, position):
        self.progress, self.position, self.changed_at = progress, position, now - VAN["GO_VAN_STALL_MS"] - VAN["GO_VAN_SETTLE_MS"]
        self.spawned_at, self.frame, self.credit, self.next = now, 0, 0.0, 0
        self.puffs = [None] * VAN["GO_VAN_PUFFS"]  # (born, life, x, y, height)

    def step(self, now, progress, position, layout):
        """Advance to now and return (moving, envelope); spawns a puff behind the rear wheel of the layout rect as the van
        moves forward (position is its left edge)."""
        if progress != self.progress:
            self.progress, self.changed_at = progress, now
        moving, envelope = van_settle(now, self.changed_at)
        self.frame = van_wheel_frame(now, moving, self.frame)
        travel = max(0, position - self.position)
        self.position = position
        if not moving or layout is None:
            self.credit = 0.0
            return moving, envelope
        height, (x0, y0, _, _) = layout
        self.credit = min(float(VAN["GO_VAN_PUFF_CREDIT"]),
                          self.credit + travel / max(1, van_scale(VAN["GO_VAN_PUFF_TRAVEL"], height)))
        if self.credit >= 1.0 and now - self.spawned_at >= VAN["GO_VAN_PUFF_GAP_MS"]:
            self.credit -= 1.0
            self.spawned_at = now
            h = van_hash(now + self.next)
            jitter = VAN["GO_VAN_PUFF_JITTER"]
            x = x0 + van_scale(VAN["GO_VAN_PUFF_SPAWN_X"] + (h >> 8) % (2 * jitter + 1) - jitter, height)
            y = y0 + van_scale(VAN["GO_VAN_PUFF_SPAWN_Y"] + (h >> 16) % (2 * jitter + 1) - jitter, height)
            life = van_puff_life(h)
            self.puffs[self.next] = (now, life, x, y, height)
            self.next = (self.next + 1) % VAN["GO_VAN_PUFFS"]
        return moving, envelope

    def puff_rects(self, now, aspect):
        """The live puffs as (left, top, right, bottom, alpha): they drift back and up, grow and fade over their life."""
        out = []
        for puff in self.puffs:
            if puff is None or now - puff[0] >= puff[1]:
                continue
            born, life, x, y, height = puff
            t = (now - born) / life
            size = van_scale(VAN["GO_VAN_PUFF_START"], height) + (van_scale(VAN["GO_VAN_PUFF_END"], height)
                                                                  - van_scale(VAN["GO_VAN_PUFF_START"], height)) * t
            cx = x - van_scale(VAN["GO_VAN_PUFF_DRIFT_X"], height) * t
            cy = y - van_scale(VAN["GO_VAN_PUFF_DRIFT_Y"], height) * t
            w, h = int(size), int(size * aspect)
            out.append((int(cx) - w // 2, int(cy) - h // 2, int(cx) - w // 2 + w, int(cy) - h // 2 + h,
                        int(VAN["GO_VAN_PUFF_ALPHA"] * (1 - t))))
        return out

# ============================== end of the load bar van section ==============================


def pack(items, atlas_width=ATLAS_WIDTH):
    """Shelf-pack (name, w, h, texels) into the atlas in retail pixels; returns placements and atlas height."""
    order = sorted(items, key=lambda it: (-it[2], -it[1], it[0]))
    placements = {}
    x = y = shelf = 0
    for name, w, h, _ in order:
        cw, ch = w + 2 * EXTRUDE, h + 2 * EXTRUDE
        if cw > atlas_width:
            raise SystemExit(f"{name} is wider than the atlas")
        if x + cw > atlas_width:
            x, y, shelf = 0, y + shelf, 0
        placements[name] = (x + EXTRUDE, y + EXTRUDE)
        x += cw
        shelf = max(shelf, ch)
    used = y + shelf
    height = 1
    while height < used:
        height *= 2
    return placements, height


def build(items=None, texture_name=TEXTURE_NAME, atlas_width=ATLAS_WIDTH, title="the shell gadget images"):
    """Return the atlas TGA and the INI text."""
    if items is None:
        items = pieces()
    names = [it[0] for it in items]
    if len(names) != len(set(names)):
        raise SystemExit("duplicate image names")
    placements, height = pack(items, atlas_width)
    atlas = [[TRANSPARENT] * (atlas_width * SCALE) for _ in range(height * SCALE)]
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

    return tga_bytes(atlas), ini_text(items, placements, height, texture_name, atlas_width, title)


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


def ini_text(items, placements, height, texture_name=TEXTURE_NAME, atlas_width=ATLAS_WIDTH, title="the shell gadget images"):
    lines = [
        f"; Generals Online UI kit: points {title} at {texture_name}.",
        f"; Generated by GenOnline/tools/go_ui_kit.py; edit the script, not this file. The texture holds {SCALE} texels per pixel.",
        "",
    ]
    for name, w, h, _ in sorted(items, key=lambda it: it[0].lower()):
        left, top = placements[name]
        lines += [
            f"MappedImage {name}",
            f"  Texture = {texture_name}",
            f"  TextureWidth = {atlas_width}",
            f"  TextureHeight = {height}",
            f"  Coords = Left:{left} Top:{top} Right:{left + w} Bottom:{top + h}",
            "  Status = NONE",
        ]
        if name.startswith(BORDER_PREFIXES) or name in BORDER_NAMES:
            lines.append(f"  Border = {LINE_LAYOUT}")
        lines += ["End", ""]
    return "\r\n".join(lines)


# the atlases that take the colours of a theme, they go into the folder of the theme
THEME_TEXTURES = (TEXTURE_PATH, TEXTURE2_PATH, LOAD_ROAD_TEXTURE_PATH)


def write_outputs(outputs, check):
    stale = [p for p, data in outputs if not p.exists() or p.read_bytes() != data]
    if check:
        for p in stale:
            print(f"out of date: {p.relative_to(ROOT)}")
        return 1 if stale else 0
    for p, data in outputs:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    print("wrote " + ", ".join(str(p.relative_to(ROOT)) for p, _ in outputs))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when the committed files are out of date")
    parser.add_argument("--theme", default="", help="write only the theme atlases of this theme into GenOnline/data/<theme>")
    args = parser.parse_args()
    if THEME:
        texture, _ = build()
        texture2, _ = build(pieces2(), TEXTURE2_NAME, ATLAS2_WIDTH, TITLE2)
        road_texture, _ = build(load_road_pieces(), LOAD_ROAD_TEXTURE_NAME, LOAD_ROAD_ATLAS_WIDTH, LOAD_ROAD_TITLE)
        themed = ROOT / "GenOnline" / "data" / THEME
        textures = (texture, texture2, road_texture)
        return write_outputs([(themed / path.relative_to(DATA), data) for path, data in zip(THEME_TEXTURES, textures)], args.check)

    _, ini = build()
    _, ini2 = build(pieces2(), TEXTURE2_NAME, ATLAS2_WIDTH, TITLE2)
    van_texture, van_ini, _ = build_van()
    _, road_ini = build(load_road_pieces(), LOAD_ROAD_TEXTURE_NAME, LOAD_ROAD_ATLAS_WIDTH, LOAD_ROAD_TITLE)
    bubble_texture, bubble_ini = build(bubble_pieces(), BUBBLE_TEXTURE_NAME, BUBBLE_ATLAS_WIDTH, BUBBLE_TITLE)
    outputs = [(INI_PATH, ini.encode("ascii")), (INI2_PATH, ini2.encode("ascii")),
               (VAN_TEXTURE_PATH, van_texture), (VAN_INI_PATH, van_ini.encode("ascii")),
               (LOAD_ROAD_INI_PATH, road_ini.encode("ascii")),
               (BUBBLE_TEXTURE_PATH, bubble_texture), (BUBBLE_INI_PATH, bubble_ini.encode("ascii"))]
    result = write_outputs(outputs, args.check)
    # the colours are read when the module loads, so each theme runs in its own process
    for theme in THEMES:
        result |= subprocess.call([sys.executable, __file__, "--theme", theme] + (["--check"] if args.check else []))
    return result


if __name__ == "__main__":
    sys.exit(main())
