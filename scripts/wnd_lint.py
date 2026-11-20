"""Lint the GO window layouts for structure bugs the engine does not report.

Rules:
    outside-parent  a visible clickable window reaches outside its parent, so the part outside never gets clicks
    overlap         two visible clickable sibling windows overlap, so one of them hides part of the other
    missing-name    the game code looks up "<file>.wnd:<name>" but the GO copy of that layout has no such window while the
                    retail copy has one (names retail never had are the game code's own optional lookups)

Known exceptions live in scripts/wnd_lint_allow.txt, one per line: "<rule> <file>.wnd:<name>  # reason"; the name may use
fnmatch wildcards.

Usage:
    python scripts/wnd_lint.py          print new findings and unused allow entries, exit 1 when there are any
"""

import fnmatch
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_wnd_layouts as wl  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
LAYOUTS = ROOT / "GenOnline" / "GameData" / "Window"
CODE = ROOT / "GeneralsMD" / "Code" / "GameEngine" / "Source"
ALLOW = Path(__file__).resolve().parent / "wnd_lint_allow.txt"

CLICKABLE = {"PUSHBUTTON", "CHECKBOX", "RADIOBUTTON", "ENTRYFIELD", "COMBOBOX", "SCROLLLISTBOX", "HORZSLIDER", "VERTSLIDER", "TABCONTROL"}
CODE_NAME_RE = re.compile(r'"(?P<file>\w+)\.wnd:(?P<name>\w+)"')


class Finding:
    def __init__(self, rule, file, name, detail):
        self.rule = rule
        self.file = file
        self.name = name
        self.detail = detail

    def __str__(self):
        return f"{self.rule} {self.name}: {self.detail}"


def _visible(win):
    while win is not None:
        if "HIDDEN" in win.status:
            return False
        win = win.parent
    return True


def _inside(inner, outer):
    return outer[0] <= inner[0] and outer[1] <= inner[1] and inner[2] <= outer[2] and inner[3] <= outer[3]


def _overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def _structure(path, root):
    findings = []
    for win in root.walk():
        if not win.rect or not _visible(win):
            continue
        parent = win.parent
        if win.kind in CLICKABLE and parent is not None and parent.kind != "SCREEN" and parent.rect and not _inside(win.rect, parent.rect):
            findings.append(Finding("outside-parent", path, win.name, f"{win.rect} outside {parent.name} {parent.rect}"))
        clickable = [c for c in win.children if c.kind in CLICKABLE and c.rect and "HIDDEN" not in c.status]
        for i, a in enumerate(clickable):
            for b in clickable[i + 1:]:
                if _overlap(a.rect, b.rect):
                    first, second = sorted((a, b), key=lambda w: w.name)
                    findings.append(Finding("overlap", path, first.name, f"{first.rect} overlaps {second.name} {second.rect}"))
    return findings


def _code_names(layouts, code, retail):
    by_file = {}
    for path, root in layouts.items():
        by_file[Path(path).name.lower()] = (path, {w.name for w in root.walk()})
    findings = []
    seen = set()
    for m in CODE_NAME_RE.finditer(code):
        name = f"{m.group('file')}.wnd:{m.group('name')}"
        shipped = by_file.get(f"{m.group('file')}.wnd".lower())
        if shipped is None or name in seen:
            continue
        seen.add(name)
        retail_names = retail.get(f"{m.group('file')}.wnd".lower()) if retail is not None else None
        if name not in shipped[1] and (retail_names is None or name in retail_names):
            findings.append(Finding("missing-name", shipped[0], name, "looked up by the game code"))
    return findings


def lint_layouts(texts, code="", retail=None):
    """Lint {archive path: layout text} against the game code text; returns [Finding].

    retail is {lower case layout file name: window names} of the retail layouts, or None to report every missing name.
    """
    layouts = {path: wl.parse_windows(text) for path, text in texts.items()}
    findings = []
    for path, root in sorted(layouts.items()):
        findings += _structure(path, root)
    findings += _code_names(layouts, code, retail)
    return findings


def parse_allow(text):
    """Return {(rule, name)} from allow list text; every entry needs a reason after #."""
    allow = set()
    for number, line in enumerate(text.splitlines(), 1):
        entry, _, reason = line.partition("#")
        if not entry.strip():
            continue
        if not reason.strip():
            raise ValueError(f"allow list line {number} has no reason: {line.strip()}")
        rule, name = entry.split()
        allow.add((rule, name))
    return allow


def apply_allow(findings, allow):
    """Return (findings not in the allow list, sorted allow entries that matched nothing); names may use wildcards."""
    used = set()
    new = []
    for f in findings:
        hits = {(rule, name) for rule, name in allow if rule == f.rule and fnmatch.fnmatchcase(f.name, name)}
        used |= hits
        if not hits:
            new.append(f)
    return new, sorted(allow - used)


def load():
    texts = {p.relative_to(LAYOUTS.parent).as_posix(): p.read_bytes().decode("latin-1") for p in sorted(LAYOUTS.rglob("*.wnd"))}
    code = "\n".join(p.read_bytes().decode("latin-1") for p in sorted(CODE.rglob("*.cpp")))
    return texts, code


def load_retail(path=wl.RETAIL_BIG):
    """Return {lower case layout file name: window names} of the retail layouts."""
    retail = {}
    for name, raw in wl.read_big(path).items():
        names = {w.name for w in wl.parse_windows(raw.decode("latin-1")).walk()}
        retail[name.replace("\\", "/").rsplit("/", 1)[-1].lower()] = names
    return retail


def run():
    texts, code = load()
    allow = parse_allow(ALLOW.read_text("utf-8")) if ALLOW.exists() else set()
    return apply_allow(lint_layouts(texts, code, load_retail()), allow)


def main():
    new, unused = run()
    for finding in new:
        print(finding)
    for rule, name in unused:
        print(f"unused allow entry: {rule} {name}")
    print(f"{len(new)} new findings, {len(unused)} unused allow entries")
    return 1 if new or unused else 0


if __name__ == "__main__":
    sys.exit(main())
