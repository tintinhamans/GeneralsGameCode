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
EXTRUDE = 1        # retail pixels of padding around each piece (SCALE texels)
SUPERSAMPLE = 2    # samples per texel and axis
# edge line width in layout pixels; three-piece bars carry it as Border, so the engine keeps it fixed at any bar height
LINE_LAYOUT = 1.5
BORDER_PREFIXES = ("Buttons-", "RadioButton", "TextEntry")  # three-piece bars whose INI entry gets Border = LINE_LAYOUT
BORDER_NAMES = ("GOScrollLine",)  # single images whose Border tells the engine the kit line thickness
ARROW_LINE = 0.95 * LINE_LAYOUT  # combo arrows are single images: 22 px art, usually about 23 layout px tall
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
# horizontal slider blocks: (edge line, fill)
SLIDER_ON = (rgba("#6A7BFF"), rgba("#3446FF"))
SLIDER_OFF = (rgba("#24308C"), rgba("#0B1030", 0.92))
SLIDER_HOVER = rgba("#2F4BFF", 0.22)
# track slider (drawn only by the GO track slider draw): (edge line, fill)
# flat, no outline: an outline only shows on the rounded ends once the small art is scaled, as a light rim
SLIDER_TRACK = (rgba("#283489"), rgba("#283489"))
SLIDER_FILL = (rgba("#3446FF"), rgba("#3446FF"))
# the thumb is a rounded pill at the value, 4 units wide and 1.75 track heights tall (W3DHorizontalSlider)
SLIDER_MARKER = {
    "": GLYPH,
    "Hilite": GLYPH_ACTIVE,
    "Disabled": GLYPH_DISABLED,
}
SLIDER_TRACK_HEIGHT = 8
SLIDER_MARKER_SIZE = (4, 14)
TRACK_LINE = 0.55  # 8 px art, drawn about 14 px tall at 1080p
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
    "PlayerStartDisabled": (rgba("#404159"), rgba("#0A0A10", 0.70)),
}
# scroll bar thumb: a slim pill in the middle of the thumb rect
SCROLL_THUMBS = {
    "Enabled": rgba("#3A4FE0"),
    "Hilite": rgba("#6A7BFF"),
    "HiliteSelected": rgba("#8C9BFF"),
    "Disabled": rgba("#404159"),
}
SCROLL_THUMB_WIDTH = 7.0
# kit list scroll bar (drawn only by the GO list scroll bar draw): a rounded channel, a pill thumb and arrow cells
SCROLL_TRACK = SLIDER_TRACK
SCROLL_PILLS = {
    "": SLIDER_FILL,
    "Hilite": (rgba("#6A7BFF"), rgba("#6A7BFF")),
    "Disabled": (rgba("#3A3C55"), rgba("#3A3C55")),
}
# arrow cells in the combo arrow states: state -> (line, fill, glyph); the engine draws the lines with GOScrollLine
SCROLL_ARROWS = {
    "": (TRANSPARENT, GLYPH),
    "Hilite": (SLIDER_HOVER, GLYPH_ACTIVE),
    "Pushed": (rgba("#2F4BFF", 0.40), GLYPH_ACTIVE),
    "Disabled": (TRANSPARENT, GLYPH_DISABLED),
}
SCROLL_HIGHLIGHT_INSET = 2.0  # cell units: clear of the separator and the list frame
SCROLL_HIGHLIGHT_RADIUS = 2.0
# the kit scroll column is 14 layout px wide with a 6 px channel the pill fills (W3DVerticalSlider, GadgetListBox)
SCROLL_ARROW_SIZE = 14  # the square arrow cell, drawn at its own layout size
SCROLL_PILL_WIDTH = 6
# pill and track pieces: twice the pill width so the semicircle ends stay smooth when drawn larger (4K draws them 22 px wide);
# the engine draws each end as tall as half the drawn width, so they stay true semicircles
SCROLL_PILL_ART = (12, 6)
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
    """The combo drop-down arrow: no left edge, the stroke chevron centred in the space left of the right line."""
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


def marker_piece(w, h, color):
    """A fully rounded vertical pill filling the piece."""
    r = w / 2.0

    def sample(x, y):
        cy = min(max(y, r), h - r)
        return color if math.hypot(x - r, y - cy) < r else TRANSPARENT

    return paint(w, h, sample)


def flat_piece(w, h, color):
    return paint(w, h, lambda x, y: color)


def glyph_arrow(w, h):
    def sample(x, y):
        return GLYPH if triangle_inside(x, y, (0.0, h), (w, h), (w / 2.0, 0.0)) else TRANSPARENT

    return paint(w, h, sample)


def segment_distance(px, py, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    k = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - k * dx, py - ay - k * dy)


def snap_stroke(v, half_texels):
    """Move a stroke centre, in pixels, so the edges of a stroke half_texels wide land on whole texels."""
    return (round(v * SCALE - half_texels) + half_texels) / SCALE


def stroke_glyph(w, h, polylines=(), arcs=(), box=None):
    """Hit test for a glyph on the 16 unit design grid, fitted to the largest square centred in box (default the piece).
    polylines: lists of grid points; arcs: (cx, cy, r, start, sweep) in grid units and degrees, y down.
    One stroke of GLYPH_STROKE_UNITS with round caps and joins; centres snapped to whole texel edges."""
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

    lines = [[px(p) for p in line] for line in polylines]
    rings = []
    for cx, cy, r, start, sweep in arcs:
        c = px((cx, cy))
        rr = r * unit
        a0, a1 = math.radians(start), math.radians(start + sweep)
        ends = ((c[0] + rr * math.cos(a0), c[1] + rr * math.sin(a0)), (c[0] + rr * math.cos(a1), c[1] + rr * math.sin(a1)))
        rings.append((c, rr, start % 360.0, sweep, ends))

    def hit(x, y):
        for line in lines:
            for a, b in zip(line, line[1:]):
                if segment_distance(x, y, a, b) < radius:
                    return True
        for c, rr, start, sweep, ends in rings:
            if abs(math.hypot(x - c[0], y - c[1]) - rr) < radius:
                if (math.degrees(math.atan2(y - c[1], x - c[0])) - start) % 360.0 <= sweep:
                    return True
            for e in ends:
                if math.hypot(x - e[0], y - e[1]) < radius:
                    return True
        return False

    return hit


def chevron_glyph(w, h, up, box=None):
    """The shared arrow: a stroke chevron pointing up or down."""
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
    """A two segment check."""
    return stroke_glyph(w, h, [[(3.5, 8.5), (6.5, 11.5), (12.5, 5.0)]])


def no_glyph(x, y):
    return False


def return_glyph(w, h):
    """The enter key arrow: a stem down from the top right, then left, ending in a chevron head."""
    return stroke_glyph(w, h, [[(12.0, 3.5), (12.0, 10.0), (4.0, 10.0)], [(7.0, 7.0), (4.0, 10.0), (7.0, 13.0)]])


def close_glyph(w, h):
    """An X with round caps."""
    return stroke_glyph(w, h, [[(4.5, 4.5), (11.5, 11.5)], [(11.5, 4.5), (4.5, 11.5)]])


def refresh_glyph(w, h):
    """A 270 degree arc, open at the top right, with a chevron head on its tangent at the top end."""
    return stroke_glyph(w, h, [[(6.0, 3.0), (8.0, 5.0), (6.0, 7.0)]], arcs=[(8.0, 9.0, 4.0, 0.0, 270.0)])


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


def pill_piece(w, h, color, width):
    """A vertical pill of the given width, centred, with a pixel of room above and below."""
    r = width / 2.0
    cx = w / 2.0

    def sample(x, y):
        top, bottom = 1.0 + r, h - 1.0 - r
        cy = min(max(y, top), bottom)
        return color if math.hypot(x - cx, y - cy) < r else TRANSPARENT

    return paint(w, h, sample)


def round_cap(w, h, side, line, fill):
    """Half of a vertical pill: a semicircle of radius w/2 closing the channel at the top or bottom."""
    r = w / 2.0

    def sample(x, y):
        cy = r if side == "top" else h - r
        if (side == "top" and y >= cy) or (side == "bottom" and y <= cy):
            d = min(x, w - x)
        else:
            d = r - math.hypot(x - r, y - cy)
        if d < 0.0:
            return TRANSPARENT
        return line if d < TRACK_LINE else fill

    return paint(w, h, sample)


def channel_piece(w, h, line, fill):
    """The middle of a vertical pill: open at the top and bottom, a line down both sides."""
    return paint(w, h, lambda x, y: line if min(x, w - x) < TRACK_LINE else fill)


def scroll_arrow_piece(w, h, up, highlight, glyph):
    """An arrow cell of the scroll column: no box, the combo arrow chevron over an optional rounded highlight.
    The engine draws the column separator with GOScrollLine; the list frame closes the other sides."""
    hit = chevron_glyph(w, h, up)
    inset, radius = SCROLL_HIGHLIGHT_INSET, SCROLL_HIGHLIGHT_RADIUS

    def sample(x, y):
        if hit(x, y):
            return glyph
        dx = max(inset + radius - x, x - (w - inset - radius), 0.0)
        dy = max(inset + radius - y, y - (h - inset - radius), 0.0)
        return highlight if math.hypot(dx, dy) < radius else TRANSPARENT

    return paint(w, h, sample)


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
        out.append((f"ScrollBarThumb{state}", 21, 22, pill_piece(21, 22, color, SCROLL_THUMB_WIDTH)))
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
    # track and fill: one height, flat, ends rounded with radius half the height
    th = SLIDER_TRACK_HEIGHT
    out.append(("GOSliderTrackLeft", th // 2, th, round_end(th // 2, th, "left", *SLIDER_TRACK)))
    out.append(("GOSliderTrackCenter", th // 2, th, button_piece(th // 2, th, "middle", *SLIDER_TRACK, TRACK_LINE)))
    out.append(("GOSliderTrackRight", th // 2, th, round_end(th // 2, th, "right", *SLIDER_TRACK)))
    out.append(("GOSliderFillLeft", th // 2, th, round_end(th // 2, th, "left", *SLIDER_FILL)))
    out.append(("GOSliderFillCenter", th // 2, th, button_piece(th // 2, th, "middle", *SLIDER_FILL, TRACK_LINE)))
    out.append(("GOSliderFillRight", th // 2, th, round_end(th // 2, th, "right", *SLIDER_FILL)))
    mw, mh = SLIDER_MARKER_SIZE
    for state, color in SLIDER_MARKER.items():
        out.append((f"GOSliderThumb{state}", mw, mh, marker_piece(mw, mh, color)))
    # list scroll bar: a channel the pill fills, like the slider track, then the arrow cells and the separator
    pw, ph = SCROLL_PILL_ART
    out.append(("GOScrollTrackTop", pw, ph, round_cap(pw, ph, "top", *SCROLL_TRACK)))
    out.append(("GOScrollTrackCenter", pw, ph, channel_piece(pw, ph, *SCROLL_TRACK)))
    out.append(("GOScrollTrackBottom", pw, ph, round_cap(pw, ph, "bottom", *SCROLL_TRACK)))
    for state, style in SCROLL_PILLS.items():
        out.append((f"GOScrollThumb{state}Top", pw, ph, round_cap(pw, ph, "top", *style)))
        out.append((f"GOScrollThumb{state}Center", pw, ph, channel_piece(pw, ph, *style)))
        out.append((f"GOScrollThumb{state}Bottom", pw, ph, round_cap(pw, ph, "bottom", *style)))
    for state, style in SCROLL_ARROWS.items():
        size = SCROLL_ARROW_SIZE
        out.append((f"GOScrollUp{state}", size, size, scroll_arrow_piece(size, size, True, *style)))
        out.append((f"GOScrollDown{state}", size, size, scroll_arrow_piece(size, size, False, *style)))
    out.append(("GOScrollLine", 4, 4, flat_piece(4, 4, SCROLL_LINE)))
    return out


# =================================================================================================
# GO map select: map preview icons (TheSuperHackers issue 1684) and the map list favourite stars.
# They live in their own atlas, GeneralsOnline_UIKit2.tga, because the first atlas is full.
# Self-contained: from the kit above this section uses only rgba, TRANSPARENT, SCALE and paint.
# Kit icon style: a 16 unit design grid scaled to the piece, one 2 unit stroke with round caps and
# joins, even padding and centres snapped to whole texels. Map icons are bare glyphs with a dark outline.
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


def map_segment_distance(px, py, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    length = dx * dx + dy * dy
    k = 0.0 if length == 0.0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length))
    return math.hypot(px - ax - k * dx, py - ay - k * dy)


def map_polyline_distance(px, py, polylines):
    return min(map_segment_distance(px, py, a, b) for line in polylines for a, b in zip(line, line[1:]))


def map_arc(size, gcx, gcy, gr, start, end, steps=16):
    """Grid arc from start to end degrees (y down) as a polyline in retail pixels; the centre is snapped."""
    unit = size / MAP_GRID
    cx, cy = map_point(size, gcx, gcy)
    return [(cx + gr * unit * math.cos(math.radians(start + (end - start) * i / steps)),
             cy + gr * unit * math.sin(math.radians(start + (end - start) * i / steps))) for i in range(steps + 1)]


def map_stroke(size, polylines):
    """Hit test for polylines drawn with the 2 unit stroke; distance tests give round caps and joins."""
    half = MAP_STROKE * size / MAP_GRID / 2.0
    return lambda x, y: map_polyline_distance(x, y, polylines) < half


def map_gp(size, gx, gy):
    """A glyph grid point scaled about the grid centre by MAP_GLYPH_SCALE, snapped to a whole texel."""
    return map_point(size, 8.0 + (gx - 8.0) * MAP_GLYPH_SCALE, 8.0 + (gy - 8.0) * MAP_GLYPH_SCALE)


def map_garc(size, gcx, gcy, gr, start, end, steps=16):
    """A glyph arc scaled like map_gp."""
    return map_arc(size, 8.0 + (gcx - 8.0) * MAP_GLYPH_SCALE, 8.0 + (gcy - 8.0) * MAP_GLYPH_SCALE, gr * MAP_GLYPH_SCALE, start, end, steps)


def map_dollar(size):
    """An S of two arcs with short bar ends above and below; a bar through the S would close its gaps."""
    curve = map_garc(size, 8.0, 6.25, 1.75, -20.0, -270.0) + map_garc(size, 8.0, 9.75, 1.75, -90.0, 160.0)[1:]
    top = [map_gp(size, 8.0, 3.6), map_gp(size, 8.0, 4.5)]
    bottom = [map_gp(size, 8.0, 11.5), map_gp(size, 8.0, 12.4)]
    return map_stroke(size, [curve, top, bottom])


def map_wrench(size):
    """A diagonal handle and an open jaw ring facing the top right."""
    handle = [map_gp(size, 5.2, 10.8), map_gp(size, 8.2, 7.8)]
    jaw = map_garc(size, 9.6, 6.4, 1.9, 5.0, 265.0)
    return map_stroke(size, [handle, jaw])


def map_cannon(size):
    """A barrel raised to the top right over a wheel; a knockout gap keeps the wheel apart from the barrel."""
    unit = size / MAP_GRID
    half = MAP_STROKE * unit / 2.0
    barrel = map_stroke(size, [[map_gp(size, 4.6, 8.0), map_gp(size, 11.1, 5.0)]])
    wheel = [map_garc(size, 6.8, 10.2, 1.9, 0.0, 360.0, 24)]

    def hit(x, y):
        d = map_polyline_distance(x, y, wheel)
        if d < half:
            return True
        if d < half + 0.7 * unit:
            return False
        return barrel(x, y)

    return hit


def map_gear(size):
    """A ring with six teeth."""
    ring = map_garc(size, 8.0, 8.0, 2.4, 0.0, 360.0, 24)
    teeth = []
    for i in range(6):
        a = math.radians(30.0 + 60.0 * i)
        teeth.append([map_gp(size, 8.0 + 3.4 * math.cos(a), 8.0 + 3.4 * math.sin(a)),
                      map_gp(size, 8.0 + 3.9 * math.cos(a), 8.0 + 3.9 * math.sin(a))])
    return map_stroke(size, [ring] + teeth)


def map_droplet(size):
    """A filled teardrop with a rounded tip."""
    unit = size / MAP_GRID
    cx, cy = map_gp(size, 8.0, 9.3)
    r = 2.7 * MAP_GLYPH_SCALE * unit
    tip = map_gp(size, 8.0, 4.2)
    left, right = map_gp(size, 5.6, 8.1), map_gp(size, 10.4, 8.1)
    round_tip = 0.35 * unit

    def hit(x, y):
        if math.hypot(x - cx, y - cy) < r:
            return True
        return map_polyline_distance(x, y, [[tip, left, right, tip]]) < round_tip or map_inside(x, y, [tip, left, right])

    return hit


def map_inside(x, y, pts):
    """Even-odd point in polygon test."""
    inside = False
    for (ax, ay), (bx, by) in zip(pts, pts[1:] + pts[:1]):
        if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
            inside = not inside
    return inside


MAP_GLYPHS = {"dollar": map_dollar, "wrench": map_wrench, "cannon": map_cannon, "gear": map_gear, "droplet": map_droplet}
# directions for the outline test: the glyph shifted by up to MAP_OUTLINE units in 16 directions and at half reach
MAP_OUTLINE_STEPS = [(r * math.cos(math.radians(a)), r * math.sin(math.radians(a))) for r in (0.5, 1.0) for a in [22.5 * i for i in range(16)]]


def map_icon_piece(size, glyph_name, glyph_color):
    """The glyph alone in its category colour over a thin dark outline, for contrast on snow, sand and grass."""
    unit = size / MAP_GRID
    glyph = MAP_GLYPHS[glyph_name](size)
    reach = [(dx * MAP_OUTLINE * unit, dy * MAP_OUTLINE * unit) for dx, dy in MAP_OUTLINE_STEPS]

    def sample(x, y):
        if glyph(x, y):
            return glyph_color
        if any(glyph(x + dx, y + dy) for dx, dy in reach):
            return MAP_GLYPH_OUTLINE
        return TRANSPARENT

    return paint(size, size, sample)


def map_star_points(size):
    unit = size / MAP_GRID
    cx, cy = map_point(size, 8.0, 8.4)
    outer, inner = 6.8 * unit, 2.9 * unit
    pts = []
    for i in range(10):
        a = math.radians(-90.0 + 36.0 * i)
        r = outer if i % 2 == 0 else inner
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def map_star_piece(size, fill, edge):
    """A five point star with the 2 unit stroke on its edge; fill None draws the outline only."""
    pts = map_star_points(size)
    outline = map_stroke(size, [pts + pts[:1]])

    def sample(x, y):
        if outline(x, y):
            return edge
        if fill is not None and map_inside(x, y, pts):
            return fill
        return TRANSPARENT

    return paint(size, size, sample)


def pieces2():
    """The second atlas: map preview icons and the favourite stars."""
    out = []
    for name, (glyph_name, glyph_color) in MAP_ICONS.items():
        out.append((name, MAP_ICON_SIZE, MAP_ICON_SIZE, map_icon_piece(MAP_ICON_SIZE, glyph_name, glyph_color)))
    out.append(("GOMapFavouriteOn", STAR_SIZE, STAR_SIZE, map_star_piece(STAR_SIZE, STAR_ON[0], STAR_ON[1])))
    out.append(("GOMapFavouriteOff", STAR_SIZE, STAR_SIZE, map_star_piece(STAR_SIZE, None, STAR_OFF)))
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


def build(items=None, texture_name=TEXTURE_NAME, atlas_width=ATLAS_WIDTH, title="the shell gadget images"):
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
    outputs = ((TEXTURE_PATH, texture), (INI_PATH, ini.encode("ascii")), (TEXTURE2_PATH, texture2), (INI2_PATH, ini2.encode("ascii")))
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
