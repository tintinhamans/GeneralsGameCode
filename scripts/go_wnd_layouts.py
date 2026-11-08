#!/usr/bin/env python3
"""Generate the Generals Online copies of the Zero Hour window layouts (.wnd).

Every retail layout in WindowZH.big is copied to GenOnline/GameData at its archive path, with
two deterministic fixes. A layout GO already edited by hand (scripts/wnd_sources) replaces the
retail file as input, and the GO-only layouts there get the same fixes.

- Kit colours: the retail blue line colours in draw data and TEXTCOLOR border entries become the
  UI kit line and hilite colours; each value keeps its alpha.
- Equal rows: within one parent, three or more sibling windows of one type that line up and stack
  with heights and gaps within 1 px get the run's most common height and gap, with the first row
  fixed. Horizontal runs get the same treatment. Children move with their window.

Only values change in place: every other byte, the CRLF line endings and the file layout stay.
Fonts and CREATIONRESOLUTION are left alone.

Usage:
    python scripts/go_wnd_layouts.py           write the layouts into GenOnline/GameData/Window
    python scripts/go_wnd_layouts.py --check   exit 1 when the committed layouts differ from a fresh run
    python scripts/go_wnd_layouts.py --report  print the colour inventory and every row change
    python scripts/go_wnd_layouts.py --retail <WindowZH.big>  read the retail layouts from another archive
"""

import argparse
import re
import struct
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "GenOnline" / "GameData"
SOURCES = Path(__file__).resolve().parent / "wnd_sources"
RETAIL_BIG = Path("C:/Program Files (x86)/Steam/steamapps/common/Command & Conquer Generals - Zero Hour/WindowZH.big")

SCREEN = (0, 0, 800, 600)  # SCREENRECT values are absolute 800x600 coordinates
TOLERANCE = 1              # px of slack for windows to count as one run
MIN_RUN = 3
MAX_PASSES = 20

# retail blue family -> kit line colour #2F4BFF and kit hilite #6A7BFF
COLOR_REMAP = {
    (47, 55, 168): (47, 75, 255),
    (49, 55, 168): (47, 75, 255),
    (46, 54, 165): (47, 75, 255),
    (128, 128, 255): (106, 123, 255),
}

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
    r"|^[ \t]*(?P<prop>\w*DRAWDATA)[ \t]*=(?P<body>[^;]*);",
    re.M,
)
SLOT_RE = re.compile(
    r"IMAGE:\s*(?P<image>[^,\s]+)\s*,\s*COLOR:\s*(?P<color>\d+\s+\d+\s+\d+\s+\d+)\s*,\s*"
    r"BORDERCOLOR:\s*(?P<border>\d+\s+\d+\s+\d+\s+\d+)"
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
        elif m.group("x0") is not None:
            top.rect = top.original = tuple(int(m.group(k)) for k in ("x0", "y0", "x1", "y1"))
            top.spans = [m.span(k) for k in ("x0", "y0", "x1", "y1")]
        elif m.group("prop"):
            slots = [(s["image"], s["color"].split(), s["border"].split()) for s in SLOT_RE.finditer(m.group("body"))]
            top.draws.append((m.group("prop"), slots))
    if len(stack) != 1:
        raise ValueError("WINDOW without END")
    return root


def structure_counts(text):
    """Count the WINDOW, END, CHILD and ENDALLCHILDREN lines."""
    return dict(Counter(m.group(1) for m in STRUCTURE_RE.finditer(text)))


def remap_colors(text):
    """Swap the retail blue line colours for the kit colours, keeping alpha and spacing."""
    def swap(m):
        rgb = (int(m["r"]), int(m["g"]), int(m["b"]))
        if rgb not in COLOR_REMAP:
            return m.group(0)
        r, g, b = COLOR_REMAP[rgb]
        return f"{m['key']}:{m['sp']}{r}{m['s1']}{g}{m['s2']}{b}"
    return COLOR_RE.sub(swap, text)


def _axis(rect, vertical):
    """Return (cross start, cross size, main start, main size) for a stack along y or x."""
    x0, y0, x1, y1 = rect
    return (x0, x1 - x0, y0, y1 - y0) if vertical else (y0, y1 - y0, x0, x1 - x0)


def _span(values):
    return max(values) - min(values)


def _mode(values):
    return Counter(values).most_common(1)[0][0]  # ties go to the first value seen


def _clusters(rows, vertical):
    """Group rows that share the cross start and size within TOLERANCE."""
    groups = []
    for row in sorted(rows, key=lambda r: _axis(r[0].rect, vertical)):
        cs, sz = _axis(row[0].rect, vertical)[:2]
        for group in groups:
            ref = [_axis(r[0].rect, vertical)[:2] for r in group]
            if all(abs(cs - c) <= TOLERANCE and abs(sz - s) <= TOLERANCE for c, s in ref):
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
            if abs(psz - nsz) <= TOLERANCE:
                notes.append(f"overlapping {prev[0].kind} rows skipped: {prev[0].name} / {nxt[0].name}")
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


def _runs(segment, vertical):
    """Split a segment into maximal runs with sizes and gaps within TOLERANCE."""
    runs, i = [], 0
    while i < len(segment):
        j = i + 1
        while j < len(segment):
            spans = [_axis(r[0].rect, vertical)[2:] for r in segment[i:j + 1]]
            sizes = [size for _, size in spans]
            gaps = [b - (a + size) for (a, size), (b, _) in zip(spans, spans[1:])]
            if _span(sizes) > TOLERANCE or _span(gaps) > TOLERANCE:
                break
            j += 1
        if j - i >= MIN_RUN:
            runs.append(segment[i:j])
            i = j
        else:
            i += 1
    return runs


def _shift(win, delta, vertical):
    for child in win.walk():
        x0, y0, x1, y1 = child.rect
        child.rect = (x0, y0 + delta, x1, y1 + delta) if vertical else (x0 + delta, y0, x1 + delta, y1)


def _equalize(parent, vertical, notes):
    """Equalize every run among the parent's direct children; return the number of rows changed."""
    changed = 0
    by_kind = {}
    for win in parent.children:
        if win.rect is not None:
            by_kind.setdefault(win.kind, {}).setdefault(win.rect, []).append(win)  # same-rect windows (e.g. Mute/UnMute) are one row
    for kind in sorted(by_kind, key=str):
        for cluster in _clusters(list(by_kind[kind].values()), vertical):
            if len(cluster) < MIN_RUN:
                continue
            for segment in _segments(cluster, vertical, notes):
                for run in _runs(segment, vertical):
                    changed += _apply_run(parent, run, vertical, notes)
    return changed


def _apply_run(parent, run, vertical, notes):
    starts = [_axis(r[0].rect, vertical)[2] for r in run]
    sizes = [_axis(r[0].rect, vertical)[3] for r in run]
    gaps = [b - (a + s) for a, s, b in zip(starts, sizes, starts[1:])]
    size, gap = _mode(sizes), _mode(gaps)
    if all(s == size for s in sizes) and all(g == gap for g in gaps):
        return 0
    lo, hi = _axis(parent.rect, vertical)[2], sum(_axis(parent.rect, vertical)[2:])
    targets = [starts[0] + k * (size + gap) for k in range(len(run))]
    for start, old_start, old_size in zip(targets, starts, sizes):
        if start < min(lo, old_start) or start + size > max(hi, old_start + old_size):
            notes.append(f"{run[0][0].kind} run would leave its parent {parent.name or 'screen'}: skipped {run[0][0].name}")
            return 0
    changed = 0
    for row, start, old_start, old_size in zip(run, targets, starts, sizes):
        if start == old_start and size == old_size:
            continue
        for win in row:
            x0, y0, x1, y1 = win.rect
            win.rect = (x0, start, x1, start + size) if vertical else (start, y0, start + size, y1)
            _shift(win, start - old_start, vertical)
        changed += 1
    return changed


def equalize_rows(text, notes=None):
    """Return (text, changes): runs of equal rows made equal; changes lists (name, old rect, new rect)."""
    notes = [] if notes is None else notes
    root = parse_windows(text)
    for _ in range(MAX_PASSES):
        pass_notes = []
        changed = 0
        for parent in [root, *root.walk()]:
            if parent.rect is None:
                continue
            changed += _equalize(parent, True, pass_notes)
            changed += _equalize(parent, False, pass_notes)
        if not changed:
            notes.extend(dict.fromkeys(pass_notes))
            break
    else:
        raise RuntimeError("row equalization did not converge")
    edits, changes = [], []
    for win in root.walk():
        if win.rect is None or win.rect == win.original:
            continue
        changes.append((win.name, win.original, win.rect))
        edits.extend(zip(win.spans, win.rect))
    for (start, end), value in sorted(edits, reverse=True):
        text = text[:start] + str(value) + text[end:]
    return text, changes


def transform(text, notes=None):
    """Apply the kit colours and the equal rows to one layout."""
    return equalize_rows(remap_colors(text), notes)[0]


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
    """Return [(relative path, input text, output text, changes, notes)] for every layout."""
    inputs = {name: raw for name, raw in read_big(retail).items()}
    by_lower = {name.lower(): name for name in inputs}
    for src in sorted(SOURCES.rglob("*.wnd")):
        rel = src.relative_to(SOURCES).as_posix()
        inputs[by_lower.get(rel.lower(), rel)] = src.read_bytes()
    results = []
    for name in sorted(inputs, key=str.lower):
        text = inputs[name].decode("latin-1")
        notes = []
        out, changes = equalize_rows(remap_colors(text), notes)
        results.append((name, text, out, changes, notes))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when the committed layouts are out of date")
    parser.add_argument("--report", action="store_true", help="print the colour inventory and every row change")
    parser.add_argument("--retail", type=Path, default=RETAIL_BIG, help="path to the retail WindowZH.big")
    args = parser.parse_args()
    if not args.retail.is_file():
        print(f"retail archive not found: {args.retail}", file=sys.stderr)
        return 2
    results = build(args.retail)

    if args.report:
        inventory = Counter()
        for name, text, out, changes, notes in results:
            inventory.update(color_inventory(text))
        print("colour inventory (visible NoImage draw slots outside the kit remap):")
        for (kind, prop, index, field, value), n in inventory.most_common():
            print(f"  {n:5}  {kind:13} {prop:38} slot {index}  {field:11} {value}")
        print("row changes:")
        for name, text, out, changes, notes in results:
            for win, old, new in changes:
                print(f"  {name}: {win} {old} -> {new}")
            for note in notes:
                print(f"  {name}: note: {note}")
        return 0

    if args.check:
        expected = {DATA / name: out.encode("latin-1") for name, text, out, changes, notes in results}
        stale = [p for p, data in expected.items() if not p.exists() or p.read_bytes() != data]
        extra = [p for p in (DATA / "Window").rglob("*.wnd") if p not in expected] if (DATA / "Window").exists() else []
        for p in stale:
            print(f"out of date: {p.relative_to(ROOT).as_posix()}")
        for p in extra:
            print(f"not generated: {p.relative_to(ROOT).as_posix()}")
        return 1 if stale or extra else 0

    total = 0
    for name, text, out, changes, notes in results:
        path = DATA / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(out.encode("latin-1"))
        total += len(changes)
        if changes:
            print(f"{name}: {len(changes)} rects changed")
    print(f"wrote {len(results)} layouts into {(DATA / 'Window').relative_to(ROOT).as_posix()}, {total} rects changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
