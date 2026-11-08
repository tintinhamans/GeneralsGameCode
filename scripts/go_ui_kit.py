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
# the thumb is a slim marker at the value: (colour, width in thumb pixels)
SLIDER_MARKER = {
    "": (rgba("#C9CEFF"), 1.15),
    "Hilite": (rgba("#FFFFFF"), 1.6),
    "Disabled": (rgba("#5D6080"), 1.15),
}
TRACK_LINE = 0.55  # 8 px art, drawn about 14 px tall at 1080p
SYMBOL_LINE = LINE_LAYOUT  # check boxes and symbol buttons draw at about their own size
GLYPH_STROKE = 2.2
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
# arrow cells in the combo arrow states: state -> (divider line, fill, glyph)
SCROLL_ARROWS = {
    "": ARROW_STATES["Enabled"],
    "Hilite": ARROW_STATES["Hilite"],
    "Pushed": ARROW_STATES["HiliteSelected"],
    "Disabled": ARROW_STATES["Disabled"],
}
# the kit scroll column is 14 layout px wide with a 6 px channel the pill fills (W3DVerticalSlider, GadgetListBox)
SCROLL_ARROW_SIZE = 14  # the square arrow cell, drawn at its own layout size
SCROLL_PILL_WIDTH = 6
SCROLL_LINE = ARROW_STATES["Enabled"][0]  # the column separator the engine draws, Border = LINE_LAYOUT
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
    cx, cy = (w - ARROW_LINE) / 2.0, h / 2.0
    tri = ((cx - 4.5, cy - 2.2), (cx + 4.5, cy - 2.2), (cx, cy + 2.8))

    def sample(x, y):
        if up:
            y = h - y
        if edge(x, y, w, h, False, True) < ARROW_LINE:
            return line
        return glyph if triangle_inside(x, y, *tri) else fill

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


def segment_distance(px, py, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    k = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - k * dx, py - ay - k * dy)


def on_stroke(x, y, polylines, width):
    for line in polylines:
        for a, b in zip(line, line[1:]):
            if segment_distance(x, y, a, b) < width / 2.0:
                return True
    return False


def symbol_piece(w, h, state, glyph_hit):
    """A button box in the button colours with a glyph; glyph_hit(x, y) says where the glyph is."""
    line, fill, glyph = SYMBOL_STATES[state]

    def sample(x, y):
        if edge(x, y, w, h, True, True) < SYMBOL_LINE:
            return line
        return glyph if glyph_hit(x, y) else fill

    return paint(w, h, sample)


def check_glyph(w, h):
    mark = [((0.27 * w, 0.52 * h), (0.43 * w, 0.69 * h), (0.75 * w, 0.31 * h))]
    return lambda x, y: on_stroke(x, y, mark, GLYPH_STROKE)


def no_glyph(x, y):
    return False


def return_glyph(w, h):
    """The enter key arrow: down from the top right, then left into an arrow head."""
    bend = [((0.74 * w, 0.26 * h), (0.74 * w, 0.62 * h), (0.36 * w, 0.62 * h))]
    head = ((0.24 * w, 0.62 * h), (0.40 * w, 0.40 * h), (0.40 * w, 0.84 * h))
    return lambda x, y: on_stroke(x, y, bend, GLYPH_STROKE) or triangle_inside(x, y, *head)


def close_glyph(w, h):
    cross = [((0.32 * w, 0.32 * h), (0.68 * w, 0.68 * h)), ((0.68 * w, 0.32 * h), (0.32 * w, 0.68 * h))]
    return lambda x, y: on_stroke(x, y, cross, GLYPH_STROKE)


def refresh_glyph(w, h):
    """Three quarters of a circle with an arrow head at its open end."""
    cx, cy, r = w / 2.0, h / 2.0, 0.27 * min(w, h)
    head_angle = math.radians(-60.0)
    tip = (cx + r * math.cos(head_angle), cy + r * math.sin(head_angle))
    out = (math.cos(head_angle), math.sin(head_angle))
    along = (-math.sin(head_angle), math.cos(head_angle))
    head = ((tip[0] - 2.8 * along[0], tip[1] - 2.8 * along[1]),
            (tip[0] + 2.6 * out[0] + 1.6 * along[0], tip[1] + 2.6 * out[1] + 1.6 * along[1]),
            (tip[0] - 2.6 * out[0] + 1.6 * along[0], tip[1] - 2.6 * out[1] + 1.6 * along[1]))

    def hit(x, y):
        angle = math.degrees(math.atan2(y - cy, x - cx))
        if abs(math.hypot(x - cx, y - cy) - r) < GLYPH_STROKE / 2.0 and not -60.0 < angle < 10.0:
            return True
        return triangle_inside(x, y, *head)

    return hit


def ring_piece(w, h, line, fill):
    cx, cy, r = w / 2.0, h / 2.0, min(w, h) / 2.0 - 1.0

    def sample(x, y):
        d = r - math.hypot(x - cx, y - cy)
        if d < 0.0:
            return TRANSPARENT
        return line if d < 2.2 else fill

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


def scroll_arrow_piece(w, h, up, line, fill, glyph):
    """An arrow cell of the scroll column: the combo arrow triangle scaled to the cell, top and bottom divider lines.
    The engine draws the left separator and the list frame closes the right."""
    k = w / 21.0
    cx, cy = (w + LINE_LAYOUT) / 2.0, h / 2.0
    tri = ((cx - 4.5 * k, cy - 2.2 * k), (cx + 4.5 * k, cy - 2.2 * k), (cx, cy + 2.8 * k))

    def sample(x, y):
        if up:
            y = h - y
        if edge(x, y, w, h, False, False) < LINE_LAYOUT:
            return line
        return glyph if triangle_inside(x, y, *tri) else fill

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
    # rounded track ends; the fill keeps a square end where it meets the marker
    out.append(("GOSliderTrackLeft", 4, 8, round_end(4, 8, "left", *SLIDER_TRACK)))
    out.append(("GOSliderTrackCenter", 4, 8, button_piece(4, 8, "middle", *SLIDER_TRACK, TRACK_LINE)))
    out.append(("GOSliderTrackRight", 4, 8, round_end(4, 8, "right", *SLIDER_TRACK)))
    out.append(("GOSliderFillLeft", 4, 8, round_end(4, 8, "left", *SLIDER_FILL)))
    out.append(("GOSliderFillCenter", 4, 8, button_piece(4, 8, "middle", *SLIDER_FILL, TRACK_LINE)))
    out.append(("GOSliderFillRight", 4, 8, button_piece(4, 8, "middle", *SLIDER_FILL, TRACK_LINE)))
    for state, (color, width) in SLIDER_MARKER.items():
        out.append((f"GOSliderThumb{state}", 8, 16, marker_piece(8, 16, color, width)))
    # list scroll bar: a channel the pill fills, like the slider track, then the arrow cells and the separator
    pw, ph = SCROLL_PILL_WIDTH, SCROLL_PILL_WIDTH // 2
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
        f"; Generated by scripts/go_ui_kit.py; edit the script, not this file. The texture holds {SCALE} texels per pixel.",
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
