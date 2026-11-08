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

The Classic (Generals Online) theme gets the same atlas in its palette (scripts/go_themes.py KIT_COLORS) under
GenOnline/GameData/Themes/Radar; its INI would be byte-identical, so the theme has no copy of it.

Usage:
    python scripts/go_ui_kit.py          write the atlas, the theme atlas and the INI into GenOnline/GameData
    python scripts/go_ui_kit.py --check  exit 1 when the committed files differ from a fresh run
"""

import argparse
import contextlib
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_themes  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "GenOnline" / "GameData"
TEXTURE_NAME = "GeneralsOnline_UIKit.tga"
TEXTURE_PATH = DATA / "Art" / "Textures" / TEXTURE_NAME
INI_PATH = DATA / "Data" / "INI" / "MappedImages" / "TextureSize_512" / "ZZ_GeneralsOnline_UIKit.INI"
THEME_TEXTURE_PATH = go_themes.THEME_DIR / TEXTURE_PATH.relative_to(DATA)

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
BORDER_NAMES = ("GOScrollLine",)  # single images whose Border tells the engine the kit line thickness
ARROW_LINE = LINE_LAYOUT  # combo arrows carry the kit line like every bordered piece
BOX_LINE = (1.25 * 1.2, 1.45 * 1.2)  # slider blocks: 11x13 art drawn as squares, so the line differs per axis; small, so a thinner line


def rgba(hex_color, alpha=1.0):
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
# arrow cells in the combo arrow states: state -> (cell highlight, chevron); the engine draws the lines with GOScrollLine
SCROLL_ARROWS = {
    "": (TRANSPARENT, GLYPH),
    "Hilite": (SLIDER_HOVER, GLYPH_ACTIVE),
    "Pushed": (rgba("#2F4BFF", 0.40), GLYPH_ACTIVE),
    "Disabled": (TRANSPARENT, GLYPH_DISABLED),
}
SCROLL_HIGHLIGHT_INSET = 3.0  # cell units: clear of the list frame
# the kit scroll column is 14 layout px wide with a 6 px channel the thumb fills (GadgetListBoxGetKitScrollSpan)
SCROLL_ARROW_SIZE = 21  # the square highlight cell, drawn over the column left of the list frame line
# the chevron alone, its ink filling the piece: the engine draws it centred on the column line the thumb shares
SCROLL_CHEVRON_ART = (12, 8)
# flat track and thumb pieces with square ends: any size draws the same plain bar
SCROLL_PILL_ART = (4, 2)
SCROLL_LINE = ARROW_STATES["Enabled"][0]  # kit list frames, cell lines and the column separator the engine draws, Border = LINE_LAYOUT
LIST_SELECTION = {
    "ListBoxHiliteItem": rgba("#2F4BFF", 0.25),
    "ListBoxHiliteSelectedItem": rgba("#2B34B8", 0.80),
}


# module colours the pieces read; a theme build swaps them for their theme colours
COLOR_NAMES = (
    "FIELD", "GLYPH", "GLYPH_ACTIVE", "GLYPH_DISABLED", "DISABLED_LINE", "BUTTON_STATES", "RADIO_STATES", "ENTRY_STATES",
    "ARROW_STATES", "SLIDER_ON", "SLIDER_OFF", "SLIDER_HOVER", "SLIDER_TRACK", "SLIDER_FILL", "SLIDER_MARKER",
    "SYMBOL_STATES", "PLAYER_STARTS", "SCROLL_THUMBS", "SCROLL_TRACK", "SCROLL_PILLS", "SCROLL_ARROWS", "SCROLL_LINE",
    "LIST_SELECTION",
)


def recolor(color, palette):
    """Swap a kit colour for its palette colour by RGB, keeping alpha; nested dicts and tuples are swapped through."""
    if isinstance(color, dict):
        return {k: recolor(v, palette) for k, v in color.items()}
    if not all(isinstance(c, float) for c in color):
        return tuple(recolor(c, palette) for c in color)
    if color[3] == 0.0:
        return color
    key = "#%02X%02X%02X" % tuple(round(c * 255) for c in color[:3])
    if key not in palette:
        raise SystemExit(f"kit colour {key} has no theme colour")
    return rgba(palette[key], color[3])


@contextlib.contextmanager
def recolored(palette):
    """Draw the pieces in palette while the block runs; None keeps the kit colours."""
    saved = {name: globals()[name] for name in COLOR_NAMES}
    if palette is not None:
        globals().update({name: recolor(value, palette) for name, value in saved.items()})
    try:
        yield
    finally:
        globals().update(saved)


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


def scroll_cell_piece(w, h, highlight):
    """A hovered or pushed scroll arrow cell: a plain square cornered highlight, inset clear of the list frame."""
    inset = SCROLL_HIGHLIGHT_INSET
    return paint(w, h, lambda x, y: highlight if inset <= x < w - inset and inset <= y < h - inset else TRANSPARENT)


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
    # list scroll bar: a channel the thumb fills, like the slider track, then the arrow chevrons and cells and the separator
    pw, ph = SCROLL_PILL_ART
    for part in ("Top", "Center", "Bottom"):
        out.append((f"GOScrollTrack{part}", pw, ph, flat_piece(pw, ph, SCROLL_TRACK)))
        for state, color in SCROLL_PILLS.items():
            out.append((f"GOScrollThumb{state}{part}", pw, ph, flat_piece(pw, ph, color)))
    cw, ch = SCROLL_CHEVRON_ART
    for state, (highlight, glyph) in SCROLL_ARROWS.items():
        out.append((f"GOScrollUp{state}", cw, ch, scroll_chevron_piece(cw, ch, True, glyph)))
        out.append((f"GOScrollDown{state}", cw, ch, scroll_chevron_piece(cw, ch, False, glyph)))
        if highlight[3] > 0.0:
            size = SCROLL_ARROW_SIZE
            out.append((f"GOScrollCell{state}", size, size, scroll_cell_piece(size, size, highlight)))
    out.append(("GOScrollLine", 4, 4, flat_piece(4, 4, SCROLL_LINE)))
    return out


# =================================================================================================
# GO map select: map preview icons (TheSuperHackers issue 1684) and the map list favourite stars.
# They live in their own atlas, GeneralsOnline_UIKit2.tga, because the first atlas is full.
# Self-contained: from the kit above this section uses only rgba, TRANSPARENT, SCALE and paint.
# Kit icon style: angular C&C symbols on a 16 unit design grid scaled to the piece, one 2 unit stroke
# with butt caps and mitred joins, sharp corners, even padding and points snapped to whole texels.
# Map icons are bare glyphs over a mitred dark outline.
# =================================================================================================

TEXTURE2_NAME = "GeneralsOnline_UIKit2.tga"
TEXTURE2_PATH = DATA / "Art" / "Textures" / TEXTURE2_NAME
INI2_PATH = DATA / "Data" / "INI" / "MappedImages" / "TextureSize_512" / "ZZ_GeneralsOnline_UIKit2.INI"
ATLAS2_WIDTH = 128  # in retail pixels

MAP_GRID = 16.0      # design grid units across a piece
MAP_STROKE = 2.0     # stroke weight in grid units
MAP_OUTLINE = 1.0    # dark contrast outline around each glyph, in grid units
MAP_GLYPH_SCALE = 1.25  # glyphs are drawn about the grid centre at this scale to fill the icon with even padding
MAP_ICON_SIZE = 15   # retail px of the art; the engine draws the preview markers at a scaled layout size
STAR_SIZE = 12

MAP_WHITE = rgba("#FFFFFF")
MAP_GOLD = rgba("#FFD34D")
MAP_GLYPH_OUTLINE = rgba("#05070F", 0.85)
# image name -> (glyph, glyph colour); no token behind the glyph, only the dark outline
MAP_ICONS = {
    "GOMapIconSupplyDock": ("dollar", rgba("#7CFC7C")),
    "GOMapIconSupplyCrates": ("dollar", rgba("#8CC4FF")),
    "GOMapIconRepairBay": ("wrench", MAP_WHITE),
    "GOMapIconArtillery": ("cannon", MAP_WHITE),
    "GOMapIconOilDerrick": ("droplet", MAP_GOLD),
    "GOMapIconTech": ("gear", MAP_WHITE),
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

def map_dollar(size):
    """An angular S of straight runs with chamfered corners and square bar ends above and below."""
    unit = size / MAP_GRID
    s = [(10.6, 5.9), (9.6, 4.9), (6.4, 4.9), (5.4, 5.9), (5.4, 7.0), (6.4, 8.0),
         (9.6, 8.0), (10.6, 9.0), (10.6, 10.1), (9.6, 11.1), (6.4, 11.1), (5.4, 10.1)]
    width = MAP_STROKE * unit
    shapes = [(map_stroke_poly([map_gp(size, x, y) for x, y in s], width), [], [])]
    # the bar ends reach past the top and bottom runs
    shapes.append((map_stroke_poly([map_gp(size, 8.0, 3.2), map_gp(size, 8.0, 5.0)], width), [], []))
    shapes.append((map_stroke_poly([map_gp(size, 8.0, 11.0), map_gp(size, 8.0, 12.8)], width), [], []))
    return shapes


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


def map_gear(size):
    """A six sided body with six square teeth and a hexagonal hole."""
    body, tip, half = 3.0, 4.3, 0.75
    base = math.sqrt(body * body - half * half)
    pts = []
    for i in range(6):
        a = math.radians(60.0 * i - 90.0)
        d, p = (math.cos(a), math.sin(a)), (-math.sin(a), math.cos(a))
        for r, w in ((base, -half), (tip, -half), (tip, half), (base, half)):
            pts.append(map_gp(size, 8.0 + d[0] * r + p[0] * w, 8.0 + d[1] * r + p[1] * w))
    return [(pts, [map_regular(size, (8.0, 8.0), 1.5, 6, 0.0)], [])]


def map_droplet(size):
    """A pointed top over a faceted bottom."""
    right = [(10.6, 7.6), (11.2, 9.6), (10.4, 11.6), (8.0, 12.6)]
    left = [(16.0 - x, y) for x, y in reversed(right[:-1])]
    pts = [map_gp(size, 8.0, 3.6)] + [map_gp(size, x, y) for x, y in right + left]
    return [(pts, [], [])]


MAP_GLYPHS = {"dollar": map_dollar, "wrench": map_wrench, "cannon": map_cannon, "gear": map_gear, "droplet": map_droplet}


def map_icon_piece(size, glyph_name, glyph_color):
    """The glyph alone in its category colour over a thin mitred dark outline, for contrast on snow, sand and grass."""
    unit = size / MAP_GRID
    shapes = MAP_GLYPHS[glyph_name](size)
    grown = [(map_offset(outer, MAP_OUTLINE * unit), [map_offset(h, -MAP_OUTLINE * unit) for h in holes]) for outer, holes, _ in shapes]

    def in_shape(x, y, outer, holes):
        return map_inside(x, y, outer) and not any(map_inside(x, y, h) for h in holes)

    def sample(x, y):
        for outer, holes, cuts in shapes:
            if in_shape(x, y, outer, holes) and not any(map_inside(x, y, c) for c in cuts):
                return glyph_color
        for outer, holes in grown:
            if in_shape(x, y, outer, holes):
                return MAP_GLYPH_OUTLINE
        return TRANSPARENT

    return paint(size, size, sample)


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
    for name, (glyph_name, glyph_color) in MAP_ICONS.items():
        out.append((name, MAP_ICON_SIZE, MAP_ICON_SIZE, map_icon_piece(MAP_ICON_SIZE, glyph_name, glyph_color)))
    out.append(("GOMapFavouriteOn", STAR_SIZE, STAR_SIZE, map_star_piece(STAR_SIZE, STAR_ON[0], STAR_ON[1], 1.0)))
    # the inside band shows the inner half of a centred 2 unit stroke plus a little, so the outline star stays open
    out.append(("GOMapFavouriteOff", STAR_SIZE, STAR_SIZE, map_star_piece(STAR_SIZE, None, STAR_OFF, 1.25)))
    return out

# ============================== end of the GO map select section ==============================


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


def build(items=None, texture_name=TEXTURE_NAME, atlas_width=ATLAS_WIDTH, title="the shell gadget images", palette=None):
    """Return the atlas TGA and the INI text; palette (go_themes.KIT_COLORS) recolours the shell gadget atlas only."""
    if items is None:
        with recolored(palette):
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
        f"; Generated by scripts/go_ui_kit.py; edit the script, not this file. The texture holds {SCALE} texels per pixel.",
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when the committed files are out of date")
    args = parser.parse_args()
    texture, ini = build()
    texture2, ini2 = build(pieces2(), TEXTURE2_NAME, ATLAS2_WIDTH, "the map preview icons and the favourite stars")
    theme_texture, theme_ini = build(palette=go_themes.KIT_COLORS)
    if theme_ini != ini:
        raise SystemExit("the theme atlas moved a piece; it needs its own INI")
    outputs = ((TEXTURE_PATH, texture), (INI_PATH, ini.encode("ascii")), (TEXTURE2_PATH, texture2), (INI2_PATH, ini2.encode("ascii")),
               (THEME_TEXTURE_PATH, theme_texture))
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
