#!/usr/bin/env python3
"""Generate the Generals Online copies of the Zero Hour window layouts (.wnd).

Every retail layout in WindowZH.big is copied to GenOnline/GameData at its archive path, with
deterministic fixes. A layout GO already edited by hand (scripts/wnd_sources) replaces the retail
file as input, and the GO-only layouts there get the same fixes. A layout with a YAML source
(scripts/wnd_yaml, compiled by scripts/wnd_yaml.py) is written as compiled: the source already holds the
kit colours, GO header templates and hover images, so none of the fixes below apply to it, and its
scripts/wnd_sources copy, if any, is only kept for reference. All rects are in 800x600 units.

- Kit colours: the retail blue line colours in draw data and TEXTCOLOR border entries become the
  UI kit line and hilite colours; each value keeps its alpha.
- Frame on the ruler art: a window drawing the MainMenuRuler image becomes full screen and its content
  frame (about 41,41-759,559) snaps onto the box drawn in the ruler art, 41,41-759,559. The frame, its
  descendants and the ruler's other children inside the old frame box map linearly; sizes that
  were equal stay equal. Menus only.
- Edges: within one parent, left and top edges within 3 units snap to the most common value
  (smallest on a tie) by moving the window; right and bottom edges of windows sharing a left or
  top edge snap the same way by changing the size.
- Labels: a STATICTEXT is centred vertically on the nearest control (combo box, entry field, check
  box, button or a small childless USER field) to its right on the same row, at most 40 units away
  with nothing in between.
- Equal rows: within one parent, three or more sibling windows of any type that line up and stack
  with heights and gaps within 3 units get the run's most common height and gap, with the first
  row fixed. Horizontal runs get the same treatment.
- Insets: inside a USER frame, content whose left and right insets differ by 1-4 units moves
  sideways to make them even; top and bottom too when both are at most 16 units. Menus only.
- Stack gaps: a vertical run of bordered controls (combo box, entry field, push or radio button)
  whose gap is 0 or 1 opens to a 2 unit gap; each row keeps its top and pitch and loses the
  difference in height, down to 18 units. Menus only.
- Column gaps: a row of three or more bordered controls with the same top and height and gaps of
  0 to 8 gets one gap, the most common (4 when all differ, at least 2); the widest control absorbs
  the difference so the row keeps its outer edges. Every row of the grid and the column headers
  above it move with their columns. Menus only.
- Entry pairs: an entry field with a push button to its right on its row (nothing between, at most 40
  units away, like the chat field and its send button) reaches to the button less the grid gap of
  the parent (its most common column gap, else 4, at least 2). Under a list that spans the pair, the
  field takes the list's left edge and the button its right edge. The button takes the field's top
  and height when both are within 3 units. Menus only.
- Titles: the screen title of a ruler menu (the large or Title template static text above the first
  header line in the frame) moves to TITLE_LEFT, the most common title edge, is centred vertically
  between the frame top and the line, and draws left aligned. Menus only.
- Overrides: scripts/wnd_fixes.py sets absolute rects last; the rules never move those windows.
- Headings: scripts/wnd_fixes.py HEADINGS names column headings that then take their column window's left
  edge, keeping their width, after the overrides; nothing else moves.
- Map previews: a window drawn by W3DDrawMapPreview gets the kit line colour as its first enabled border
  colour, which the engine draws as a kit frame around the preview (menus and load screens alike).
- Theme copies: every menu layout (not the in-game ones, nor the menus shown during a match) also goes to
  GenOnline/GameData/Themes/Radar at the same path, its final output with the colours swapped for the
  Classic (Generals Online) palette (scripts/go_themes.py LAYOUT_COLORS). Only colour numbers differ.

A move never creates an overlap with a sibling and never pushes a window or its children further
out of their parent; such moves are skipped and logged. Children move with their window. The rules
repeat until nothing changes. In-game layouts get only the edge, label and equal row rules;
MainMenu and ControlBar keep their geometry apart from the original 1 unit equal rows rule. Only
values change in place: every other byte, the CRLF line endings and the file layout stay. Names,
callbacks, images, text, fonts and CREATIONRESOLUTION stay; only a title's CENTERED flag changes.

Usage:
    python scripts/go_wnd_layouts.py           write the layouts into GenOnline/GameData/Window
    python scripts/go_wnd_layouts.py --check   exit 1 when the committed layouts differ from a fresh run
    python scripts/go_wnd_layouts.py --report  print the colour inventory and the alignment changes,
                                               and write scripts/wnd_review.md
    python scripts/go_wnd_layouts.py --retail <WindowZH.big>  read the retail layouts from another archive
"""

import argparse
import math
import re
import struct
import sys
from collections import Counter
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from wnd_fixes import FIXES, HEADINGS  # noqa: E402
import go_themes  # noqa: E402
import wnd_yaml  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "GenOnline" / "GameData"
SOURCES = Path(__file__).resolve().parent / "wnd_sources"
REVIEW = Path(__file__).resolve().parent / "wnd_review.md"
RETAIL_BIG = Path("C:/Program Files (x86)/Steam/steamapps/common/Command & Conquer Generals - Zero Hour/WindowZH.big")

SCREEN = (0, 0, 800, 600)  # SCREENRECT values are absolute 800x600 coordinates
CROSS_TOLERANCE = 1        # rows of one run share their cross start and size within this
SNAP = 3                   # edges, sizes and gaps within this count as meant to line up
STRICT = 1                 # row tolerance for layouts that keep their geometry (the original equal rows rule)
MIN_RUN = 3
MAX_PASSES = 20

RULER_IMAGE = "MainMenuRuler"
RULER_FRAME = (41, 41, 759, 559)  # retail content frame inside the ruler
RULER_BOX = (41, 41, 759, 559)    # box drawn in the ruler art; frames sit on it (the gap to the ticks is in the art)
RULER_SLACK = 6                   # a content frame lies within this of RULER_FRAME or RULER_BOX
SIBLING_SLACK = 10                # a frame sibling maps along when it lies within the old box plus this

LABEL_REACH = 40
# HORZSLIDER counts: the engine centres its track and thumb in the slider rect
CONTROL_KINDS = {"COMBOBOX", "ENTRYFIELD", "CHECKBOX", "PUSHBUTTON", "HORZSLIDER"}
VALUE_KINDS = {"STATICTEXT", "ENTRYFIELD"}  # a value box right of a slider centres on the slider
FIELD_HEIGHT = 40          # a childless USER window up to this height counts as a field
INSET_SPREAD = 4
VERTICAL_INSET = 16        # top and bottom insets only even out when both are this tight

BORDERED_KINDS = {"COMBOBOX", "ENTRYFIELD", "PUSHBUTTON", "RADIOBUTTON"}
MIN_GAP = 2                # stacked or side by side bordered controls keep at least this gap
MIN_HEIGHT = 18            # a stack gap never shrinks a control below this height
COLUMN_REACH = 8           # neighbouring columns of one row are at most this far apart
DEFAULT_COLUMN_GAP = 4     # column gap when every gap of a row differs

# shared left edge of the ruler menu titles: the most common one (GameSpy game options and WOL welcome
# at 44, LAN game options and quick match at 45; LAN lobby 47, Skirmish 48, SaveLoad 55)
TITLE_LEFT = 44
TITLE_FONT = 16            # a static text this large, or with the Title template, can be a title
TITLE_TEMPLATE = "Title"

IN_GAME = re.compile(
    r"^Window/(ControlBar|ControlBarPopupDescription|Diplomacy|GenPowersShortcutBar\w*|GeneralsExpPoints"
    r"|InGameChat|IME\w*|MOTD|ReplayControl|InGamePopupMessage)\.wnd$",
    re.I,
)
# menus shown during a match; with IN_GAME they keep the retail header templates so in-game mods stay in charge of them
# (the same list as isGenOnlineShellLayout in Core ArchiveFileSystem.cpp)
IN_MATCH_MENUS = {
    "crcmismatch", "defeat", "disconnectscreen", "localdefeat", "observerquit", "popupsaveload",
    "quitmenu", "quitmessagebox", "quitnosave", "saveload", "victorious",
}
# retail header templates; the menu layouts use the GO copies in Data/GeneralsOnline/HeaderTemplate.ini instead,
# so a mod that replaces Data/<language>/HeaderTemplate.ini cannot resize the GO menus
GO_TEMPLATE_PREFIX = "GO"
RETAIL_TEMPLATES = {
    "Title", "MainButton", "Button", "ButtonSmall", "TextEntry", "ComboBoxEntry", "MinorTitle", "LabelRegular",
    "LabelSmall", "LoadScreenCameos", "LoadScreenMissionLocation", "Arial10BoldTemplate", "Arial12BoldTemplate",
    "Arial14Template", "Arial14BoldTemplate",
}
# a checked box keeps the enabled image on hover in retail; the kit has a hover version
HILITE_DRAW_RE = re.compile(r"HILITEDRAWDATA[^;]*;")
HILITE_IMAGES = {"IMAGE: Active-Checked,": "IMAGE: Active-HiLighted-Checked,"}
HEADER_RE = re.compile(r'(?m)^(?P<lead>[ \t]*HEADERTEMPLATE[ \t]*=[ \t]*")(?P<header>[^"]*)(?=")')
GEOMETRY_KEPT = {
    "window/menus/mainmenu.wnd",  # laid out around the logo and side panels by hand
    "window/controlbar.wnd",      # slots sit on the bar art and ControlBarScheme code places windows too
}
MENU_RULES = ("frame", "edges", "labels", "spacing", "insets", "gaps", "columns", "pairs", "titles")
IN_GAME_RULES = ("edges", "labels", "spacing")
KEPT_RULES = ("rows",)     # equal rows within STRICT only

# retail blue family -> kit line colour #2F4BFF and kit hilite #6A7BFF
COLOR_REMAP = {
    (47, 55, 168): (47, 75, 255),
    (49, 55, 168): (47, 75, 255),
    (46, 54, 165): (47, 75, 255),
    (128, 128, 255): (106, 123, 255),
}
KIT_LINE = (47, 75, 255)

# map preview windows: their first enabled border colour is the frame colour W3DDrawMapPreview draws
MAP_PREVIEW_CALLBACK = "W3DDrawMapPreview"
MAP_PREVIEW_BORDER = "%d %d %d 255" % KIT_LINE
PREVIEW_TOKEN_RE = re.compile(
    r"^[ \t]*(?P<open>WINDOW)[ \t]*\r?$"
    r"|^[ \t]*(?P<close>END)[ \t]*\r?$"
    r"|^[ \t]*DRAWCALLBACK[ \t]*=[ \t]*\"(?P<callback>[^\"]*)\""
    r"|^[ \t]*ENABLEDDRAWDATA[ \t]*=[ \t]*IMAGE:[^,]*,[ \t]*COLOR:[^,]*,[ \t]*BORDERCOLOR:[ \t]*"
    r"(?P<border>\d+[ \t]+\d+[ \t]+\d+[ \t]+\d+)",
    re.M,
)

COLOR_RE = re.compile(
    r"(?<![A-Z])(?P<key>COLOR|BORDERCOLOR|ENABLEDBORDER|DISABLEDBORDER|HILITEBORDER):(?P<sp>[ \t]*)"
    r"(?P<r>\d+)(?P<s1>[ \t]+)(?P<g>\d+)(?P<s2>[ \t]+)(?P<b>\d+)(?=[ \t]+\d+)"
)
TOKEN_RE = re.compile(
    r"^[ \t]*(?P<open>WINDOW)[ \t]*\r?$"
    r"|^[ \t]*(?P<close>END)[ \t]*\r?$"
    r"|^[ \t]*WINDOWTYPE[ \t]*=[ \t]*(?P<kind>\w+)"
    r"|^[ \t]*NAME[ \t]*=[ \t]*\"(?P<name>[^\"]*)\""
    r"|SCREENRECT[ \t]*=[ \t]*UPPERLEFT:[ \t]*(?P<x0>-?\d+)[ \t]+(?P<y0>-?\d+)[ \t]*,\s*"
    r"BOTTOMRIGHT:[ \t]*(?P<x1>-?\d+)[ \t]+(?P<y1>-?\d+)"
    r"|^[ \t]*(?P<prop>\w*DRAWDATA)[ \t]*=(?P<body>[^;]*);"
    r"|^[ \t]*FONT[ \t]*=[ \t]*NAME:[ \t]*\"[^\"]*\"[ \t]*,[ \t]*SIZE:[ \t]*(?P<size>\d+)"
    r"|^[ \t]*HEADERTEMPLATE[ \t]*=[ \t]*\"(?P<header>[^\"]*)\""
    r"|^[ \t]*STATICTEXTDATA[ \t]*=[ \t]*CENTERED:[ \t]*(?P<centered>\d+)"
    r"|^[ \t]*STATUS[ \t]*=[ \t]*(?P<status>[^;]*);",
    re.M,
)
SLOT_RE = re.compile(
    r"IMAGE:\s*(?P<image>[^,\s]+)\s*,\s*COLOR:\s*(?P<color>\d+\s+\d+\s+\d+\s+\d+)\s*,\s*"
    r"BORDERCOLOR:\s*(?P<border>\d+\s+\d+\s+\d+\s+\d+)"
)
# COLOR_RE plus the TEXTCOLOR text colours, for the theme palette
THEME_COLOR_RE = re.compile(
    r"(?<![A-Z])(?P<key>COLOR|BORDERCOLOR|ENABLEDBORDER|DISABLEDBORDER|HILITEBORDER|ENABLED|DISABLED|HILITE):(?P<sp>[ \t]*)"
    r"(?P<r>\d+)(?P<s1>[ \t]+)(?P<g>\d+)(?P<s2>[ \t]+)(?P<b>\d+)(?=[ \t]+\d+)"
)
STRUCTURE_RE = re.compile(r"^[ \t]*(WINDOW|END|CHILD|ENDALLCHILDREN)[ \t]*\r?$", re.M)


class Window:
    def __init__(self, parent=None):
        self.parent = parent
        self.children = []
        self.kind = None
        self.name = ""
        self.rect = None       # (x0, y0, x1, y1), current value
        self.original = None   # rect as parsed
        self.spans = None      # text spans of the four rect numbers
        self.draws = []        # (property, [(image, color, border)])
        self.font_size = 0
        self.header = ""
        self.status = set()    # STATUS flags, for example {"ENABLED", "HIDDEN"}
        self.centered = None   # (value, text span) of STATICTEXTDATA CENTERED

    def walk(self):
        for child in self.children:
            yield child
            yield from child.walk()


def parse_windows(text):
    """Return a root node whose children are the layout's top-level windows."""
    root = Window()
    root.kind, root.rect = "SCREEN", SCREEN
    stack = [root]
    for m in TOKEN_RE.finditer(text):
        top = stack[-1]
        if m.group("open"):
            win = Window(top)
            top.children.append(win)
            stack.append(win)
        elif m.group("close"):
            if len(stack) == 1:
                raise ValueError(f"END without WINDOW at offset {m.start()}")
            stack.pop()
        elif m.group("kind"):
            top.kind = m.group("kind")
        elif m.group("name") is not None:
            top.name = m.group("name")
        elif m.group("status") is not None:
            top.status = {flag.strip() for flag in m.group("status").split("+") if flag.strip()}
        elif m.group("x0") is not None:
            top.rect = top.original = tuple(int(m.group(k)) for k in ("x0", "y0", "x1", "y1"))
            top.spans = [m.span(k) for k in ("x0", "y0", "x1", "y1")]
        elif m.group("prop"):
            slots = [(s["image"], s["color"].split(), s["border"].split()) for s in SLOT_RE.finditer(m.group("body"))]
            top.draws.append((m.group("prop"), slots))
        elif m.group("size"):
            top.font_size = int(m.group("size"))
        elif m.group("header") is not None:
            top.header = m.group("header")
        elif m.group("centered"):
            top.centered = (int(m.group("centered")), m.span("centered"))
    if len(stack) != 1:
        raise ValueError("WINDOW without END")
    return root


def structure_counts(text):
    """Count the WINDOW, END, CHILD and ENDALLCHILDREN lines."""
    return dict(Counter(m.group(1) for m in STRUCTURE_RE.finditer(text)))


def remap_colors(text, table=COLOR_REMAP, pattern=COLOR_RE):
    """Swap the retail blue line colours for the kit colours, or the colours in table, keeping alpha and spacing."""
    def swap(m):
        rgb = (int(m["r"]), int(m["g"]), int(m["b"]))
        if rgb not in table:
            return m.group(0)
        r, g, b = table[rgb]
        return f"{m['key']}:{m['sp']}{r}{m['s1']}{g}{m['s2']}{b}"
    return pattern.sub(swap, text)


def themed(name):
    """True for the layouts the theme recolours: the menus, not the in-game layouts nor the menus shown in a match."""
    return _uses_go_templates(name)


def theme_layout(text):
    """The Classic (Generals Online) copy of a final layout: draw data and text colours in the theme palette."""
    return remap_colors(text, go_themes.LAYOUT_COLORS, THEME_COLOR_RE)


# ---------------------------------------------------------------------------------------------
# geometry helpers


def _overlaps(a, b):
    return min(a[2], b[2]) > max(a[0], b[0]) and min(a[3], b[3]) > max(a[1], b[1])


def _overflow(outer, rect):
    return (outer[0] - rect[0], outer[1] - rect[1], rect[2] - outer[2], rect[3] - outer[3])


def _fits(old_outer, new_outer, old, new):
    """True when no side of new sticks out of new_outer further than old stuck out of old_outer."""
    return all(n <= max(0, o) for n, o in zip(_overflow(new_outer, new), _overflow(old_outer, old)))


def _moved(rect, dx, dy):
    x0, y0, x1, y1 = rect
    return (x0 + dx, y0 + dy, x1 + dx, y1 + dy)


def _round(value):
    return math.floor(value + Fraction(1, 2))


def _snap_target(values):
    """Most common value, the smallest on a tie."""
    counts = Counter(values)
    best = max(counts.values())
    return min(v for v, n in counts.items() if n == best)


def _short(win):
    return win.name.split(":", 1)[-1] or win.kind or "?"


def _items(parent):
    """Group the parent's children by rect; windows sharing a rect (alternates) move as one."""
    groups = {}
    for win in parent.children:
        if win.rect is not None:
            groups.setdefault(win.rect, []).append(win)
    return list(groups.values())


class _Context:
    def __init__(self, locked):
        self.locked = locked     # ids of windows the overrides own
        self.labels = set()      # ids of labels the label rule owns vertically
        self.titles = set()      # ids of screen titles the title rule owns
        self.headers = set()     # ids of column headers kept out of the equal rows rule
        self.uncentred = []      # titles that draw left aligned from now on
        self.notes = []
        self.touched = {}        # rule -> ids of the windows it moved or sized

    def touch(self, rule, wins):
        self.touched.setdefault(rule, set()).update(id(w) for w in wins)


def _try(ctx, parent, moves, rule, quiet=False):
    """Apply [(windows, new rect)] at once when it keeps every safety rule; return the rows changed."""
    moves = [(wins, new) for wins, new in moves if wins[0].rect != new]
    if not moves:
        return 0
    first, new0 = moves[0][0][0], moves[0][1]
    what = f"{rule}: skipped {first.kind} {_short(first)} {first.rect} -> {new0}"
    if quiet:
        notes, ctx.notes = ctx.notes, []
        try:
            return _try_checked(ctx, parent, moves, rule, what)
        finally:
            ctx.notes = notes
    return _try_checked(ctx, parent, moves, rule, what)


def _try_checked(ctx, parent, moves, rule, what):
    if any(id(w) in ctx.locked for wins, _ in moves for w in wins):
        ctx.notes.append(f"{what}: fixed by an override")
        return 0
    moving = {id(w) for wins, _ in moves for w in wins}
    others = [w for w in parent.children if w.rect is not None and id(w) not in moving]
    for i, (wins, new) in enumerate(moves):
        old = wins[0].rect
        if new[2] <= new[0] or new[3] <= new[1]:
            ctx.notes.append(f"{what}: empty rect")
            return 0
        if not _fits(parent.rect, parent.rect, old, new):
            ctx.notes.append(f"{what}: would leave {_short(parent)}")
            return 0
        for other in others:
            if _overlaps(new, other.rect) and not _overlaps(old, other.rect):
                ctx.notes.append(f"{what}: would overlap {other.kind} {_short(other)}")
                return 0
        for wins2, new2 in moves[i + 1:]:
            if _overlaps(new, new2) and not _overlaps(old, wins2[0].rect):
                ctx.notes.append(f"{what}: would overlap {wins2[0].kind} {_short(wins2[0])}")
                return 0
        dx, dy = new[0] - old[0], new[1] - old[1]
        for win in wins:
            for child in win.children:
                if child.rect is not None and not _fits(old, new, child.rect, _moved(child.rect, dx, dy)):
                    ctx.notes.append(f"{what}: child {_short(child)} would stick out")
                    return 0
    for wins, new in moves:
        dx, dy = new[0] - wins[0].rect[0], new[1] - wins[0].rect[1]
        for win in wins:
            win.rect = new
            for child in win.walk():
                if child.rect is not None:
                    child.rect = _moved(child.rect, dx, dy)
    ctx.touch(rule, [w for wins, _ in moves for w in wins])
    return len(moves)


# ---------------------------------------------------------------------------------------------
# frame on the ruler


def _has_image(win, image):
    return any(slot[0] == image for _, slots in win.draws for slot in slots)


def _near(rect, box, slack):
    return all(abs(a - b) <= slack for a, b in zip(rect, box))


def _within(rect, box, slack):
    return rect[0] >= box[0] - slack and rect[1] >= box[1] - slack and rect[2] <= box[2] + slack and rect[3] <= box[3] + slack


def _ruler_frames(root):
    """Yield (ruler, content frame) for each window drawing the ruler image around a content frame."""
    for ruler in root.walk():
        if ruler.rect is None or not _has_image(ruler, RULER_IMAGE):
            continue
        frames = [c for c in ruler.children if c.rect is not None and c.kind == "USER"
                  and (_near(c.rect, RULER_FRAME, RULER_SLACK) or _near(c.rect, RULER_BOX, RULER_SLACK))]
        if frames:
            yield ruler, max(frames, key=lambda c: (c.rect[2] - c.rect[0]) * (c.rect[3] - c.rect[1]))


def _has_ruler_frame(root):
    return any(True for _ in _ruler_frames(root))


def map_frames(ctx, root):
    """Move each ruler's content frame onto the ruler's inner box."""
    for ruler, frame in list(_ruler_frames(root)):
        old = frame.rect
        if ruler.rect != SCREEN:
            ctx.notes.append(f"frame: ruler {_short(ruler)} {ruler.rect} -> {SCREEN}")
            ruler.rect = SCREEN
            ctx.touch("frame", [ruler])
        if old == RULER_BOX:
            continue
        movers = [frame]
        for sib in ruler.children:
            if sib is frame or sib.rect is None:
                continue
            cx2, cy2 = sib.rect[0] + sib.rect[2], sib.rect[1] + sib.rect[3]
            if 2 * old[0] < cx2 < 2 * old[2] and 2 * old[1] < cy2 < 2 * old[3]:
                if _within(sib.rect, old, SIBLING_SLACK):
                    movers.append(sib)
                else:
                    ctx.notes.append(f"frame: kept {sib.kind} {_short(sib)} {sib.rect}, larger than the frame")
        sx = Fraction(RULER_BOX[2] - RULER_BOX[0], old[2] - old[0])
        sy = Fraction(RULER_BOX[3] - RULER_BOX[1], old[3] - old[1])

        def fx(v):
            return _round(RULER_BOX[0] + (v - old[0]) * sx)

        def fy(v):
            return _round(RULER_BOX[1] + (v - old[1]) * sy)

        before = {}
        for mover in movers:
            for win in (mover, *mover.walk()):
                if win.rect is not None:
                    before[id(win)] = win.rect
                    x0, y0, x1, y1 = win.rect
                    win.rect = (fx(x0), fy(y0), fx(x1), fy(y1))
                    if win.rect != before[id(win)]:
                        ctx.touch("frame", [win])
        # mapping both edges can make equal sizes differ by one; give them their scaled size again,
        # where that fits (touching stacks are left to the equal rows rule, so these skips stay quiet)
        kept_notes = ctx.notes[:]
        for parent in (ruler, *(w for m in movers for w in (m, *m.walk()))):
            kids = [c for c in parent.children if id(c) in before]
            for axis, scale in ((0, sx), (1, sy)):
                sizes = {}
                for kid in kids:
                    r = before[id(kid)]
                    sizes.setdefault(r[axis + 2] - r[axis], []).append(kid)
                for size, group in sorted(sizes.items()):
                    if len(group) < 2:
                        continue
                    target = _round(size * scale)
                    for kid in group:
                        r = list(kid.rect)
                        if r[axis + 2] - r[axis] == target:
                            continue
                        grow = r[:]
                        grow[axis + 2] = r[axis] + target
                        if not _try(ctx, parent, [([kid], tuple(grow))], "frame"):
                            shift = r[:]
                            shift[axis] = r[axis + 2] - target
                            _try(ctx, parent, [([kid], tuple(shift))], "frame")
        ctx.notes = kept_notes


# ---------------------------------------------------------------------------------------------
# edges


def _clusters_by(items, key):
    """Greedy clusters of items whose key lies within SNAP of the cluster's smallest key."""
    clusters = []
    for item in sorted(items, key=lambda it: (key(it), it[0].rect)):
        if clusters and key(item) - key(clusters[-1][0]) <= SNAP:
            clusters[-1].append(item)
        else:
            clusters.append([item])
    return [c for c in clusters if len(c) > 1]


def snap_edges(ctx, parent):
    """Snap near left and top edges by moving, and near right and bottom edges of shared columns by sizing."""
    changed = 0
    pr = parent.rect
    items = _items(parent)
    # a sibling drawn over or around another (a frame around a bar, overlapping table cells) keeps its inset
    stacked = {id(it[0]) for it in items for o in items if o is not it and _overlaps(it[0].rect, o[0].rect)}

    def free(item, axis):
        # labels own their vertical place; windows flush with a parent edge stay anchored to it
        r = item[0].rect
        return (id(item[0]) not in stacked and id(item[0]) not in ctx.titles and not (axis == 1 and _is_label(ctx, item))
                and r[axis] != pr[axis] and r[axis + 2] != pr[axis + 2])

    for axis in (0, 1):
        items = [it for it in _items(parent) if free(it, axis)]
        for cluster in _clusters_by(items, lambda it: it[0].rect[axis]):
            target = _snap_target([it[0].rect[axis] for it in cluster])
            for item in cluster:
                d = target - item[0].rect[axis]
                if d:
                    changed += _try(ctx, parent, [(item, _moved(item[0].rect, d if axis == 0 else 0, d if axis == 1 else 0))], "edges")
    for axis in (0, 1):
        columns = {}
        for item in _items(parent):
            if free(item, axis):
                columns.setdefault(item[0].rect[axis], []).append(item)
        for start in sorted(columns):
            for cluster in _clusters_by(columns[start], lambda it: it[0].rect[axis + 2]):
                target = _snap_target([it[0].rect[axis + 2] for it in cluster])
                for item in cluster:
                    r = list(item[0].rect)
                    if r[axis + 2] != target:
                        r[axis + 2] = target
                        changed += _try(ctx, parent, [(item, tuple(r))], "edges")
    return changed


# ---------------------------------------------------------------------------------------------
# labels


def _is_label(ctx, item):
    return all(id(w) in ctx.labels for w in item)


def _is_control(win):
    if win.kind in CONTROL_KINDS:
        return True
    return win.kind == "USER" and not win.children and win.rect[3] - win.rect[1] <= FIELD_HEIGHT


def label_pairs(parent):
    """Return [(label item, control item)]: each label with the nearest control to its right on its row."""
    items = _items(parent)
    labels = [it for it in items if all(w.kind == "STATICTEXT" for w in it)]
    controls = [it for it in items if any(_is_control(w) for w in it)]
    pairs = []
    for label in labels:
        lr = label[0].rect
        lh = lr[3] - lr[1]
        best = None
        for control in controls:
            cr = control[0].rect
            ch = cr[3] - cr[1]
            gap = cr[0] - lr[2]
            if cr[0] <= lr[0] or not -SNAP <= gap <= LABEL_REACH or lh <= 0 or ch <= 0 or lh > 2 * ch:
                continue
            band = (lr[2], max(lr[1], cr[1]), cr[0], min(lr[3], cr[3]))
            if 2 * (band[3] - band[1]) <= min(lh, ch):
                continue
            if gap > 0 and any(o is not label and o is not control and _overlaps(o[0].rect, band) for o in items):
                continue
            key = (gap, abs(lr[1] + lr[3] - cr[1] - cr[3]), cr)
            if best is None or key < best[0]:
                best = (key, control)
        if best:
            pairs.append((label, best[1]))
    return pairs


def slider_values(parent):
    """Return [(value item, slider item)]: each slider with the nearest value box to its right on its row."""
    items = _items(parent)
    sliders = [it for it in items if any(w.kind == "HORZSLIDER" for w in it)]
    values = [it for it in items if all(w.kind in VALUE_KINDS for w in it)]
    pairs = []
    for slider in sliders:
        sr = slider[0].rect
        sh = sr[3] - sr[1]
        best = None
        for value in values:
            vr = value[0].rect
            vh = vr[3] - vr[1]
            gap = vr[0] - sr[2]
            if not -SNAP <= gap <= LABEL_REACH or vh <= 0 or sh <= 0 or vh > 2 * sh:
                continue
            if 2 * (min(vr[3], sr[3]) - max(vr[1], sr[1])) <= min(vh, sh):
                continue
            key = (gap, abs(vr[1] + vr[3] - sr[1] - sr[3]), vr)
            if best is None or key < best[0]:
                best = (key, value)
        if best:
            pairs.append((best[1], slider))
    return pairs


def centre_labels(ctx, parent):
    changed = 0
    for label, control in label_pairs(parent) + slider_values(parent):
        if id(label[0]) in ctx.titles:
            continue
        lr, cr = label[0].rect, control[0].rect
        h = lr[3] - lr[1]
        y0 = (cr[1] + cr[3] - h + 1) // 2  # centre on the control, halves round up
        changed += _try(ctx, parent, [(label, (lr[0], y0, lr[2], y0 + h))], "labels")
    return changed


# ---------------------------------------------------------------------------------------------
# equal rows


def _axis(rect, vertical):
    """Return (cross start, cross size, main start, main size) for a stack along y or x."""
    x0, y0, x1, y1 = rect
    return (x0, x1 - x0, y0, y1 - y0) if vertical else (y0, y1 - y0, x0, x1 - x0)


def _span(values):
    return max(values) - min(values)


def _clusters(rows, vertical):
    """Group rows that share the cross start and size within CROSS_TOLERANCE."""
    groups = []
    for row in sorted(rows, key=lambda r: _axis(r[0].rect, vertical)):
        cs, sz = _axis(row[0].rect, vertical)[:2]
        for group in groups:
            ref = [_axis(r[0].rect, vertical)[:2] for r in group]
            if all(abs(cs - c) <= CROSS_TOLERANCE and abs(sz - s) <= CROSS_TOLERANCE for c, s in ref):
                group.append(row)
                break
        else:
            groups.append([row])
    return groups


def _segments(cluster, vertical, notes):
    """Sort a cluster along the stack and split it where rows overlap by half or more."""
    rows = sorted(cluster, key=lambda r: _axis(r[0].rect, vertical)[2])
    segments, current, skip = [], [], set()
    for prev, nxt in zip(rows, rows[1:]):
        ps, psz = _axis(prev[0].rect, vertical)[2:]
        ns, nsz = _axis(nxt[0].rect, vertical)[2:]
        if ns - ps < min(psz, nsz) / 2:
            skip.update((id(prev), id(nxt)))
            if abs(psz - nsz) <= SNAP:
                notes.append(f"spacing: overlapping {prev[0].kind} rows skipped: {_short(prev[0])} / {_short(nxt[0])}")
    for row in rows:
        if id(row) in skip:
            if current:
                segments.append(current)
            current = []
        else:
            current.append(row)
    if current:
        segments.append(current)
    return segments


def _runs(segment, vertical, tolerance):
    """Split a segment into maximal runs with sizes and gaps within tolerance."""
    runs, i = [], 0
    while i < len(segment):
        j = i + 1
        while j < len(segment):
            spans = [_axis(r[0].rect, vertical)[2:] for r in segment[i:j + 1]]
            sizes = [size for _, size in spans]
            gaps = [b - (a + size) for (a, size), (b, _) in zip(spans, spans[1:])]
            if _span(sizes) > tolerance or _span(gaps) > tolerance:
                break
            j += 1
        if j - i >= MIN_RUN:
            runs.append(segment[i:j])
            i = j
        else:
            i += 1
    return runs


def _equalize(ctx, parent, vertical, tolerance=SNAP):
    """Equalize every run among the parent's direct children; return the number of rows changed."""
    changed = 0
    rows = {}
    for win in parent.children:
        if win.rect is None or id(win) in ctx.locked or id(win) in ctx.titles:
            continue
        if vertical and (id(win) in ctx.labels or id(win) in ctx.headers):
            continue
        rows.setdefault(win.rect, []).append(win)  # same-rect windows (e.g. Mute/UnMute) are one row
    # any window type: a column mixing an entry field and combo boxes is still one stack of rows
    runs = []
    for cluster in _clusters(list(rows.values()), vertical):
        if len(cluster) < MIN_RUN:
            continue
        for segment in _segments(cluster, vertical, ctx.notes):
            runs.extend(_run_options(run, vertical) for run in _runs(segment, vertical, tolerance))
    if not runs:
        return 0
    # all runs at once first, so the columns of a grid whose cells overlap can move together
    for k in range(max(len(options) for options in runs)):
        batch = [m for options in runs for m in options[min(k, len(options) - 1)]]
        changed = _try(ctx, parent, batch, "spacing", quiet=True)
        if changed:
            return changed
    for options in runs:
        for k, moves in enumerate(options):
            done = _try(ctx, parent, moves, "spacing", quiet=k > 0)
            changed += done
            if done:
                break
    return changed


def _ranked(values):
    """Distinct values, most common first, ties in the order seen."""
    counts = Counter(values)
    return sorted(dict.fromkeys(values), key=lambda v: -counts[v])


def _run_options(run, vertical):
    """Move lists for a run, best first: the most common size and gap, then the other sizes and gaps
    seen in the run (a grid that cannot grow inside its parent may still even out at a smaller size)."""
    starts = [_axis(r[0].rect, vertical)[2] for r in run]
    sizes = [_axis(r[0].rect, vertical)[3] for r in run]
    gaps = [b - (a + s) for a, s, b in zip(starts, sizes, starts[1:])]
    options = []
    for size in _ranked(sizes):
        for gap in _ranked(gaps):
            moves = []
            for k, row in enumerate(run):
                start = starts[0] + k * (size + gap)
                x0, y0, x1, y1 = row[0].rect
                moves.append((row, (x0, start, x1, start + size) if vertical else (start, y0, start + size, y1)))
            options.append(moves)
    return options


# ---------------------------------------------------------------------------------------------
# insets


def even_insets(ctx, parent):
    """Make the content insets of a USER frame symmetric when they differ by 1 to INSET_SPREAD."""
    fr = parent.rect
    if parent.kind != "USER" or fr is None or (fr[2] - fr[0] >= SCREEN[2] - 10 and fr[3] - fr[1] >= SCREEN[3] - 10):
        return 0
    changed = 0
    for axis in (0, 1):
        cross = 1 - axis
        group = [w for w in parent.children if w.rect is not None and id(w) not in ctx.titles
                 and fr[axis] < w.rect[axis] and w.rect[axis + 2] < fr[axis + 2]
                 and fr[cross] <= w.rect[cross] and w.rect[cross + 2] <= fr[cross + 2]]
        if not group:
            continue
        lo = min(w.rect[axis] for w in group) - fr[axis]
        hi = fr[axis + 2] - max(w.rect[axis + 2] for w in group)
        if axis == 1 and max(lo, hi) > VERTICAL_INSET:
            continue
        if not 1 <= abs(hi - lo) <= INSET_SPREAD:
            continue
        d = int((hi - lo) / 2)  # an odd spread keeps one unit, toward zero so a rerun does not flip it
        if d:
            moves = [([w], _moved(w.rect, d if axis == 0 else 0, d if axis == 1 else 0)) for w in group]
            changed += _try(ctx, parent, moves, "insets")
    return changed


# ---------------------------------------------------------------------------------------------
# stack gaps and column gaps


def _bordered_items(ctx, parent):
    """The parent's items holding a bordered control that no override or title owns."""
    return [it for it in _items(parent) if any(w.kind in BORDERED_KINDS for w in it)
            and not any(id(w) in ctx.locked or id(w) in ctx.titles for w in it)]


def _chains(cluster, vertical, max_gap, min_len):
    """Split a cluster, sorted along the stack, into chains whose neighbouring gaps lie in 0..max_gap."""
    rows = sorted(cluster, key=lambda r: _axis(r[0].rect, vertical)[2])
    chains, current = [], rows[:1]
    for prev, nxt in zip(rows, rows[1:]):
        start, size = _axis(prev[0].rect, vertical)[2:]
        if 0 <= _axis(nxt[0].rect, vertical)[2] - (start + size) <= max_gap:
            current.append(nxt)
            continue
        if len(current) >= min_len:
            chains.append(current)
        current = [nxt]
    if len(current) >= min_len:
        chains.append(current)
    return chains


def _header_above(items, x0, x1, top):
    """The static text just above a column at x0-x1 whose first row starts at top, if any."""
    best = None
    for item in items:
        r = item[0].rect
        if (all(w.kind == "STATICTEXT" for w in item) and top - COLUMN_REACH <= r[3] <= top
                and 2 * x0 <= r[0] + r[2] <= 2 * x1 and (best is None or r[3] > best[0].rect[3])):
            best = item
    return best


def column_headers(ctx, parent):
    """Ids of the static texts heading a stack of bordered controls (kept out of the equal rows rule)."""
    items = _items(parent)
    ids = set()
    for cluster in _clusters(_bordered_items(ctx, parent), True):
        for chain in _chains(cluster, True, MIN_GAP, 2):
            r = chain[0][0].rect
            header = _header_above(items, r[0], r[2], r[1])
            if header:
                ids.update(id(w) for w in header)
    return ids


def open_gaps(ctx, parent):
    """Open vertical runs of touching bordered controls to MIN_GAP; rows keep their top and pitch."""
    batches = {}
    for cluster in _clusters(_bordered_items(ctx, parent), True):
        for chain in _chains(cluster, True, MIN_GAP - 1, 2):
            rects = [item[0].rect for item in chain]
            cuts = [MIN_GAP - (b[1] - a[3]) for a, b in zip(rects, rects[1:])]
            cuts.append(cuts[-1])
            moves = [(item, (r[0], r[1], r[2], r[3] - cut)) for item, r, cut in zip(chain, rects, cuts)]
            low = min(new[3] - new[1] for _, new in moves)
            if low < MIN_HEIGHT:
                first = chain[0][0]
                ctx.notes.append(f"gaps: skipped {first.kind} {_short(first)} run of {len(chain)} at {first.rect}: "
                                 f"height {low} below {MIN_HEIGHT}")
                continue
            # columns of one grid share their row tops and change together so the rows stay aligned
            batches.setdefault(tuple(r[1] for r in rects), []).extend(moves)
    return sum(_try(ctx, parent, batches[key], "gaps") for key in sorted(batches))


def even_columns(ctx, parent):
    """Give rows of side by side bordered controls one gap, keeping their outer edges; every row of
    the grid and the headers above its columns move along."""
    grids = {}
    for cluster in _clusters(_bordered_items(ctx, parent), False):
        for chain in _chains(cluster, False, COLUMN_REACH, MIN_RUN):
            grids.setdefault(tuple((item[0].rect[0], item[0].rect[2]) for item in chain), []).append(chain)
    items = _items(parent)
    changed = 0
    owners = Counter(span for spans in grids for span in spans)
    for spans in sorted(grids):
        gaps = [b[0] - a[1] for a, b in zip(spans, spans[1:])]
        target = DEFAULT_COLUMN_GAP if max(Counter(gaps).values()) == 1 else _snap_target(gaps)
        target = max(target, MIN_GAP)
        if all(g == target for g in gaps):
            continue
        if any(owners[span] > 1 for span in spans):
            # another row shares a column but not the others (an extra control at the end of one row)
            ctx.notes.append(f"columns: skipped row at {spans[0][0]}-{spans[-1][1]}, y {grids[spans][0][0][0].rect[1]}: "
                             f"its columns differ from another row of the grid")
            continue
        widths = [x1 - x0 for x0, x1 in spans]
        wide = widths.index(max(widths))
        widths[wide] -= target * len(gaps) - sum(gaps)
        new, x = [], spans[0][0]
        for w in widths:
            new.append((x, x + w))
            x += w + target
        moves = [(item, (nx0, item[0].rect[1], nx1, item[0].rect[3]))
                 for chain in grids[spans] for item, (nx0, nx1) in zip(chain, new)]
        top = min(chain[0][0].rect[1] for chain in grids[spans])
        for (x0, x1), (nx0, nx1) in zip(spans, new):
            header = _header_above(items, x0, x1, top)
            if header:
                r = header[0].rect
                moves.append((header, (r[0] + nx0 - x0, r[1], nx1 if r[2] == x1 else r[2] + nx0 - x0, r[3])))
        changed += _try(ctx, parent, moves, "columns")
    return changed


# ---------------------------------------------------------------------------------------------
# entry fields and their buttons


def _has_kind(item, kind):
    return any(w.kind == kind for w in item)


def entry_pairs(ctx, parent):
    """Return [(field item, button item)]: each entry field with the nearest push button to its right on its row."""
    items = _items(parent)
    free = [it for it in items if not any(id(w) in ctx.locked or id(w) in ctx.titles for w in it)]
    pairs = []
    for field in (it for it in free if _has_kind(it, "ENTRYFIELD")):
        fr = field[0].rect
        best = None
        for button in (it for it in free if _has_kind(it, "PUSHBUTTON") and it is not field):
            br = button[0].rect
            gap = br[0] - fr[2]
            if br[0] <= fr[0] or not -SNAP <= gap <= LABEL_REACH:
                continue
            if 2 * (min(fr[3], br[3]) - max(fr[1], br[1])) <= min(fr[3] - fr[1], br[3] - br[1]):
                continue
            band = (fr[2], max(fr[1], br[1]), br[0], min(fr[3], br[3]))
            if gap > 0 and any(o is not field and o is not button and _overlaps(o[0].rect, band) for o in items):
                continue
            if best is None or gap < best[0]:
                best = (gap, button)
        if best:
            pairs.append((field, best[1]))
    return pairs


def _list_above(items, fr, br):
    """The list box ending just above a field and button, spanning them within SNAP, with nothing between."""
    for item in items:
        r = item[0].rect
        if (_has_kind(item, "SCROLLLISTBOX") and fr[1] - COLUMN_REACH <= r[3] <= fr[1]
                and abs(r[0] - fr[0]) <= SNAP and abs(r[2] - br[2]) <= SNAP):
            band = (r[0], r[3], r[2], fr[1])
            if r[3] == fr[1] or not any(o is not item and _overlaps(o[0].rect, band) for o in items):
                return item
    return None


def _grid_gap(ctx, parent):
    """The parent's most common column gap among rows of bordered controls, else the default; at least MIN_GAP."""
    gaps = []
    for cluster in _clusters(_bordered_items(ctx, parent), False):
        for chain in _chains(cluster, False, COLUMN_REACH, MIN_RUN):
            gaps += [b[0].rect[0] - a[0].rect[2] for a, b in zip(chain, chain[1:])]
    return max(_snap_target(gaps) if gaps else DEFAULT_COLUMN_GAP, MIN_GAP)


def fit_entry_fields(ctx, parent):
    """Stretch each entry field to its button, lining the pair up with a list box right above it."""
    changed = 0
    items = _items(parent)
    for field, button in entry_pairs(ctx, parent):
        fr, br = field[0].rect, button[0].rect
        above = _list_above(items, fr, br)
        x1 = above[0].rect[2] if above else br[2]
        x0 = above[0].rect[0] if above else fr[0]
        y0, y1 = br[1], br[3]
        if abs(br[1] - fr[1]) <= SNAP and abs((br[3] - br[1]) - (fr[3] - fr[1])) <= SNAP:
            y0, y1 = fr[1], fr[3]
        new_button = (x1 - (br[2] - br[0]), y0, x1, y1)
        new_field = (x0, fr[1], new_button[0] - _grid_gap(ctx, parent), fr[3])
        changed += _try(ctx, parent, [(button, new_button), (field, new_field)], "pairs")
    return changed


# ---------------------------------------------------------------------------------------------
# screen titles


def _is_title(win):
    return win.kind == "STATICTEXT" and (win.header == TITLE_TEMPLATE or win.font_size >= TITLE_FONT)


def screen_titles(root):
    """Return [(frame, title or None, band top, band bottom)] for each ruler frame with a header line:
    the title is the largest title text whose centre lies above the frame's first header line."""
    found = []
    for _, frame in _ruler_frames(root):
        fr = frame.rect
        lines = [c.rect[1] for c in frame.children if c.rect is not None and c.kind == "USER"
                 and c.rect[3] - c.rect[1] <= MIN_GAP and 2 * (c.rect[2] - c.rect[0]) >= fr[2] - fr[0]
                 and fr[1] < c.rect[1] <= fr[1] + (fr[3] - fr[1]) // 4]
        if not lines:
            found.append((frame, None, fr[1], fr[1]))
            continue
        bottom = min(lines)
        titles = [c for c in frame.children if c.rect is not None and _is_title(c)
                  and c.rect[1] >= fr[1] - SNAP and c.rect[1] + c.rect[3] < 2 * bottom]
        title = max(titles, key=lambda c: (c.header == TITLE_TEMPLATE, c.font_size, c.rect[2] - c.rect[0]), default=None)
        found.append((frame, title, fr[1], bottom))
    return found


def _title_item(frame, title):
    return [w for w in frame.children if w.rect == title.rect and _is_title(w)]


def align_titles(ctx, root):
    """Move each screen title to TITLE_LEFT, centre it in its header band and draw it left aligned."""
    changed = 0
    ctx.uncentred = []
    for frame, title, top, bottom in screen_titles(root):
        if title is None:
            ctx.notes.append("titles: no title found")
            continue
        item = _title_item(frame, title)
        r = title.rect
        h = min(r[3] - r[1], bottom - top)  # text centres in its rect, so a taller title fills the band
        y0 = (top + bottom - h + 1) // 2
        changed += _try(ctx, frame, [(item, (TITLE_LEFT, y0, TITLE_LEFT + r[2] - r[0], y0 + h))], "titles")
        ctx.notes.append(f"titles: {_short(title)} {title.original} -> {title.rect}")
        if title.rect[0] == TITLE_LEFT:
            ctx.uncentred += [w for w in item if w.centered and w.centered[0]]
    return changed


# ---------------------------------------------------------------------------------------------
# overrides and the pass loop


def _named(root, fixes):
    by_name = {}
    for win in root.walk():
        by_name.setdefault(win.name, win)
    missing = sorted(set(fixes) - set(by_name))
    if missing:
        raise ValueError(f"wnd_fixes names not in the layout: {', '.join(missing)}")
    return by_name


def apply_overrides(ctx, root, fixes):
    by_name = _named(root, fixes)
    changed = 0
    for name in sorted(fixes):
        win, rect = by_name[name], tuple(fixes[name])
        if win.rect != rect:
            dx, dy = rect[0] - win.rect[0], rect[1] - win.rect[1]
            win.rect = rect
            for child in win.walk():
                if child.rect is not None:
                    child.rect = _moved(child.rect, dx, dy)
            ctx.touch("override", [win])
            changed += 1
    return changed


def align_headings(ctx, root, headings):
    """Move each named heading sideways onto its column's left edge, keeping its width; fail on a new overlap."""
    by_name = _named(root, {**{h: None for h in headings}, **{c: None for c in headings.values()}})
    for name in sorted(headings):
        win, col = by_name[name], by_name[headings[name]]
        dx = col.rect[0] - win.rect[0]
        if dx == 0:
            continue
        new = _moved(win.rect, dx, 0)
        siblings = [w for w in win.parent.children if w is not win and w.rect is not None]
        if any(_overlaps(new, w.rect) and not _overlaps(win.rect, w.rect) for w in siblings):
            raise ValueError(f"heading {name} would overlap a sibling at {new}")
        win.rect = new
        for child in win.walk():
            if child.rect is not None:
                child.rect = _moved(child.rect, dx, 0)
        ctx.touch("override", [win])


def _settle(ctx, root, rules, fixes, name, late=True):
    """Repeat the rules until nothing changes; without late, the gap, column and title rules only
    keep their windows (titles, column headers) out of the other rules."""
    parents = [p for p in (root, *root.walk()) if p.rect is not None]
    for _ in range(MAX_PASSES):
        ctx.notes = []
        before = [w.rect for w in root.walk()]
        ctx.labels, ctx.titles, ctx.headers = set(), set(), set()
        if "titles" in rules:
            ctx.titles = {id(w) for frame, title, _, _ in screen_titles(root) if title for w in _title_item(frame, title)}
        if "labels" in rules:
            ctx.labels = {id(w) for p in parents for label, _ in label_pairs(p) + slider_values(p) for w in label} - ctx.titles
        if "gaps" in rules or "columns" in rules:
            ctx.headers = {i for p in parents for i in column_headers(ctx, p)}
        for p in parents:
            if "edges" in rules:
                snap_edges(ctx, p)
        for p in parents:
            if "labels" in rules:
                centre_labels(ctx, p)
        for p in parents:
            if "spacing" in rules or "rows" in rules:
                tolerance = SNAP if "spacing" in rules else STRICT
                _equalize(ctx, p, True, tolerance)
                _equalize(ctx, p, False, tolerance)
        for p in parents:
            if "insets" in rules:
                even_insets(ctx, p)
        for p in parents:
            if late and "gaps" in rules:
                open_gaps(ctx, p)
            if late and "columns" in rules:
                even_columns(ctx, p)
            if late and "pairs" in rules:
                fit_entry_fields(ctx, p)
        if late and "titles" in rules:
            align_titles(ctx, root)
        apply_overrides(ctx, root, fixes)
        if [w.rect for w in root.walk()] == before:
            return
    raise RuntimeError(f"{name or 'layout'}: alignment did not converge")


def _rules(name):
    if name is None:
        return MENU_RULES
    if name.lower() in GEOMETRY_KEPT:
        return KEPT_RULES
    return IN_GAME_RULES if IN_GAME.match(name) else MENU_RULES


def align(text, name=None, rules=None):
    """Return (text, info) with the alignment rules applied.

    info holds "frame" (a ruler frame was found), "stats" (windows each rule moved), "notes" (skips and
    remarks from the settled pass), "changes" ([(window name, input rect, output rect)]) and
    "uncentred" (names of titles that now draw left aligned).
    """
    root = parse_windows(text)
    rules = _rules(name) if rules is None else rules
    fixes = FIXES.get(name, {}) if name else {}
    locked = {id(w) for n, w in _named(root, fixes).items() if n in fixes}
    ctx = _Context(locked)
    frame_notes = []
    if "frame" in rules and _has_ruler_frame(root):
        # settle first so runs and columns within the snap distance are exact before scaling stretches them;
        # gaps, columns and titles work on the final geometry only
        _settle(ctx, root, rules, fixes, name, late=False)
        ctx.notes = []
        map_frames(ctx, root)
        frame_notes, ctx.notes = ctx.notes, []
    frame = "frame" in rules and _has_ruler_frame(root)
    _settle(ctx, root, rules, fixes, name)
    align_headings(ctx, root, HEADINGS.get(name, {}) if name else {})
    edits, changes = [], []
    for win in root.walk():
        if win.rect is None or win.rect == win.original:
            continue
        changes.append((win.name, win.original, win.rect))
        edits.extend(zip(win.spans, win.rect))
    edits.extend((win.centered[1], 0) for win in ctx.uncentred)
    for (start, end), value in sorted(edits, reverse=True):
        text = text[:start] + str(value) + text[end:]
    if ctx.uncentred:
        ctx.touch("titles", ctx.uncentred)
    stats = Counter({rule: len(ids) for rule, ids in ctx.touched.items()})
    info = {"frame": frame, "stats": stats, "notes": frame_notes + list(dict.fromkeys(ctx.notes)), "changes": changes,
            "uncentred": [win.name for win in ctx.uncentred]}
    return text, info


def equalize_rows(text, notes=None):
    """Return (text, changes) with only the equal rows rule applied."""
    out, info = align(text, rules=("spacing",))
    if notes is not None:
        notes.extend(info["notes"])
    return out, info["changes"]


def _uses_go_templates(name):
    if not name:
        return False
    path = name.replace("\\", "/")
    if not path.lower().startswith("window/menus/"):
        return False
    return path.rsplit("/", 1)[-1].rsplit(".", 1)[0].lower() not in IN_MATCH_MENUS


def go_templates(text, name):
    """Point the header templates of a menu layout at their GO copies; in-game layouts are returned unchanged."""
    if not _uses_go_templates(name):
        return text

    def swap(m):
        header = m.group("header")
        return m.group("lead") + (GO_TEMPLATE_PREFIX + header if header in RETAIL_TEMPLATES else header)

    return HEADER_RE.sub(swap, text)


def hilite_images(text):
    """Give hovered kit pieces their hover image where retail reuses the enabled one."""

    def swap(m):
        block = m.group(0)
        for old, new in HILITE_IMAGES.items():
            block = block.replace(old, new)
        return block

    return HILITE_DRAW_RE.sub(swap, text)


def map_preview_borders(text):
    """Give every W3DDrawMapPreview window the kit line as its first enabled border colour."""
    stack, spans = [], []
    for m in PREVIEW_TOKEN_RE.finditer(text):
        if m.group("open"):
            stack.append({"preview": False, "border": None})
        elif m.group("close"):
            if not stack:
                raise ValueError(f"END without WINDOW at offset {m.start()}")
            win = stack.pop()
            if win["preview"] and win["border"] is not None:
                spans.append(win["border"])
        elif stack and m.group("callback") is not None:
            stack[-1]["preview"] = m.group("callback") == MAP_PREVIEW_CALLBACK
        elif stack and m.group("border") is not None:
            stack[-1]["border"] = m.span("border")
    for start, end in sorted(spans, reverse=True):
        text = text[:start] + MAP_PREVIEW_BORDER + text[end:]
    return text


def finish(text, name):
    """The colour and template fixes after alignment: GO header templates, hover images and map preview frames."""
    return map_preview_borders(hilite_images(go_templates(text, name)))


def transform(text, notes=None, name=None):
    """Apply the kit colours, the alignment rules and the GO header templates for the layout at archive path name."""
    out, info = align(remap_colors(text), name)
    out = finish(out, name)
    if notes is not None:
        notes.extend(info["notes"])
    return out


def color_inventory(text):
    """Count visible draw-data colours outside the kit remap on NoImage slots.

    Keys are (window type, draw property, slot index, field, "r g b a"). Slots left at the editor
    default (white colour and white border) are counted under the field "default".
    """
    known = {*COLOR_REMAP, *COLOR_REMAP.values()}
    counts = Counter()
    for win in parse_windows(text).walk():
        for prop, slots in win.draws:
            for index, (image, color, border) in enumerate(slots):
                if image != "NoImage":
                    continue
                if color == border == ["255", "255", "255", "255"]:
                    counts[(win.kind, prop, index, "default", "255 255 255 255")] += 1
                    continue
                for field, value in (("COLOR", color), ("BORDERCOLOR", border)):
                    rgba = tuple(int(v) for v in value)
                    if rgba[3] > 0 and rgba[:3] not in known:
                        counts[(win.kind, prop, index, field, " ".join(value))] += 1
    return counts


def read_big(path):
    """Return {relative path: bytes} for the .wnd entries of a BIG archive."""
    data = Path(path).read_bytes()
    if data[:4] != b"BIGF":
        raise ValueError(f"{path} is not a BIG archive")
    count = struct.unpack(">I", data[8:12])[0]
    files, pos = {}, 16
    for _ in range(count):
        offset, size = struct.unpack(">II", data[pos:pos + 8])
        end = data.index(b"\0", pos + 8)
        name = data[pos + 8:end].decode("latin-1").replace("\\", "/")
        pos = end + 1
        if name.lower().endswith(".wnd"):
            files[name] = data[offset:offset + size]
    return files


def build(retail):
    """Return [(relative path, input text, output text, info)] for every layout."""
    inputs = {name: raw for name, raw in read_big(retail).items()}
    by_lower = {name.lower(): name for name in inputs}
    for src in sorted(SOURCES.rglob("*.wnd")):
        rel = src.relative_to(SOURCES).as_posix()
        inputs[by_lower.get(rel.lower(), rel)] = src.read_bytes()
    compiled = {}
    for rel, src in wnd_yaml.sources().items():
        name = by_lower.get(rel.lower(), rel)
        compiled[name] = wnd_yaml.compile_file(src)
        inputs[name] = compiled[name].encode("latin-1")
    results = []
    for name in sorted(inputs, key=str.lower):
        text = inputs[name].decode("latin-1")
        if name in compiled:
            info = {"frame": False, "stats": Counter(), "notes": [], "changes": [], "uncentred": []}
            results.append((name, text, text, info))
            continue
        out, info = align(remap_colors(text), name)
        results.append((name, text, finish(out, name), info))
    return results


RULE_NAMES = ("frame", "edges", "labels", "spacing", "insets", "gaps", "columns", "pairs", "titles", "override")


def _move_size(change):
    _, old, new = change
    return max(abs(a - b) for a, b in zip(old, new))


def _skips(info):
    return [n for n in info["notes"] if ": skipped " in n or "rows skipped" in n]


def _layout_kind(name):
    if name.lower() in GEOMETRY_KEPT:
        return "geometry kept"
    return "in-game" if IN_GAME.match(name) else "menu"


def write_review(results, path=REVIEW):
    """Write the screenshot review list: most changed layouts first, then the ones the rules could not settle."""
    ranked = sorted(results, key=lambda r: (-len(r[3]["changes"]), r[0].lower()))
    lines = [
        "# GO layout alignment review",
        "",
        "Generated by `python scripts/go_wnd_layouts.py --report`. Rects are in 800x600 units.",
        "Check these layouts with screenshots first; fix what the rules got wrong in `scripts/wnd_fixes.py`.",
        "",
        "## Notes",
        "",
        "- ScoreScreen: the ruler parent was at 6,12-792,614 (bottom off screen); it is now full screen like every other ruler menu, so its ruler moves up 12 units. Its buttons, portrait and remarks sit next to the frame, inside its old box, and map with it.",
        "- Ruler parents at 799x599, 799x600 and 798x599 (LanGameOptionsMenu, LanLobbyMenu, ReplayMenu, SaveLoad) are now 800x600.",
        "- MainMenu keeps its geometry: no single content frame, panels stacked by hand around the logo; only the original equal rows rule (1 unit) runs, as before.",
        "- ControlBar keeps its geometry the same way (no change): the new rules would shift the whole bar 3 units and resize the queue buttons over the bar art, and ControlBarScheme code places windows too.",
        "- ChallengeLoadScreen draws the ruler without a content frame: no frame mapping.",
        "- Other in-game layouts get only the edge, label and equal row rules.",
        "- Labels skip horizontal sliders: the track draws from the top of the slider rect at a fixed pixel size.",
        "- WOLCustomLobby ButtonRefresh sat 30 units left of its parent in retail; scaling with the frame makes that 31.",
        f"- Titles: the ruler menu titles (GameSpy and LAN game options, LAN lobby, SaveLoad, Skirmish, WOL quick match and welcome) share left edge {TITLE_LEFT} and centre between the frame top and the first header line; the Skirmish title draws left aligned now (it was centred). No title found in GameSpyLoginProfile, NetworkDirectConnect and ReplayMenu (no header line, the title sits in a panel), ScoreScreen (its header band holds column labels) and WOLCustomLobby.",
        "- The title no longer counts as frame content for the inset rule, which gives SaveLoad and WOL quick match back their retail insets (content 2 units up and 2 units left).",
        f"- Stack gaps: the touching player rows of the Skirmish, LAN and GameSpy game options grids and the Options detail radios get a {MIN_GAP} unit gap at the same pitch; each control is 2 units shorter.",
        f"- Column gaps: LAN game options columns are {MIN_GAP} units apart (were 1, 3, 1), Skirmish 3 (were 3, 3, 2); GameSpy is already even, and its last row, which also holds Select Map, is skipped.",
        "- Entry pairs: the chat fields of the LAN lobby, LAN and GameSpy game options and the score screen reach their send button less the grid gap (2 next to a player grid, else the button row gap or 4), and line up with the chat list above. WOLCustomLobby already sits at its button row gap of 6. The LAN lobby name field and Clear are no pair: the Games header overlaps the space between them.",
        "- Map lists: with Open Sans a list row is 36 px at 1080p (font height plus 1), so the 152-153 unit map lists show 7 full rows and part of an 8th. List heights are left alone; the row height is the engine's.",
        "",
        "## Most changed",
        "",
        "| Layout | Kind | Rects changed | Frame | " + " | ".join(RULE_NAMES[1:]) + " | Skips | Largest move |",
        "|---|---|---|---|" + "---|" * (len(RULE_NAMES) - 1) + "---|---|",
    ]
    for name, text, out, info in ranked:
        if not info["changes"]:
            continue
        stats = info["stats"]
        big = max(info["changes"], key=_move_size)
        lines.append(
            f"| {name} | {_layout_kind(name)} | {len(info['changes'])} | {'yes' if info['frame'] else 'no'} | "
            + " | ".join(str(stats[r]) for r in RULE_NAMES[1:])
            + f" | {len(_skips(info))} | {big[0].split(':', 1)[-1]} {_move_size(big)} |"
        )
    lines += ["", "## Rules could not settle", "", "Skipped moves (overlap, parent or override) per layout.", ""]
    for name, text, out, info in sorted(results, key=lambda r: r[0].lower()):
        skips = _skips(info)
        if skips:
            lines.append(f"### {name}")
            lines.append("")
            lines += [f"- {s}" for s in skips]
            lines.append("")
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when the committed layouts are out of date")
    parser.add_argument("--report", action="store_true", help="print the colour inventory and every alignment change")
    parser.add_argument("--retail", type=Path, default=RETAIL_BIG, help="path to the retail WindowZH.big")
    args = parser.parse_args()
    if not args.retail.is_file():
        print(f"retail archive not found: {args.retail}", file=sys.stderr)
        return 2
    results = build(args.retail)

    if args.report:
        inventory = Counter()
        for name, text, out, info in results:
            inventory.update(color_inventory(text))
        print("colour inventory (visible NoImage draw slots outside the kit remap):")
        for (kind, prop, index, field, value), n in inventory.most_common():
            print(f"  {n:5}  {kind:13} {prop:38} slot {index}  {field:11} {value}")
        totals = Counter()
        print("alignment changes:")
        for name, text, out, info in results:
            stats = info["stats"]
            totals.update(stats)
            totals["rects"] += len(info["changes"])
            totals["skips"] += len(_skips(info))
            totals["frames"] += info["frame"]
            if not info["changes"] and not info["notes"]:
                continue
            counts = ", ".join(f"{r} {stats[r]}" for r in RULE_NAMES[1:])
            print(f"  {name}: frame mapped {'yes' if info['frame'] else 'no'}; {counts}; "
                  f"{len(info['changes'])} rects; {len(_skips(info))} skipped")
            for win, old, new in sorted(info["changes"], key=lambda c: -_move_size(c))[:10]:
                print(f"    {win} {old} -> {new}")
            for note in info["notes"]:
                print(f"    note: {note}")
        print("totals: " + ", ".join(f"{k} {totals[k]}" for k in ("frames", *RULE_NAMES, "rects", "skips")))
        write_review(results)
        print(f"wrote {REVIEW.relative_to(ROOT).as_posix()}")
        return 0

    theme = {go_themes.THEME_DIR / name: theme_layout(out).encode("latin-1") for name, text, out, info in results if themed(name)}
    if args.check:
        expected = {DATA / name: out.encode("latin-1") for name, text, out, info in results}
        expected.update(theme)
        stale = [p for p, data in expected.items() if not p.exists() or p.read_bytes() != data]
        extra = [p for folder in (DATA / "Window", go_themes.THEME_DIR / "Window") if folder.exists()
                 for p in folder.rglob("*.wnd") if p not in expected]
        for p in stale:
            print(f"out of date: {p.relative_to(ROOT).as_posix()}")
        for p in extra:
            print(f"not generated: {p.relative_to(ROOT).as_posix()}")
        return 1 if stale or extra else 0

    total = 0
    for name, text, out, info in results:
        path = DATA / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(out.encode("latin-1"))
        total += len(info["changes"])
        if info["changes"]:
            print(f"{name}: {len(info['changes'])} rects changed")
    for path, data in theme.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print(f"wrote {len(results)} layouts into {(DATA / 'Window').relative_to(ROOT).as_posix()}, {total} rects changed")
    print(f"wrote {len(theme)} theme layouts into {(go_themes.THEME_DIR / 'Window').relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
