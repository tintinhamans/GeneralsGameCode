"""Tests for scripts/wnd_yaml.py, the YAML layout compiler.

Usage:
    python -m unittest scripts/test_wnd_yaml.py
"""

import re
import subprocess
import sys
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_wnd_layouts as wl  # noqa: E402
import wnd_lint as lint  # noqa: E402
import wnd_yaml  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OPTIONS = "Window/Menus/OptionsMenu.wnd"

# a tiny component so these tests do not depend on the shipped component files
BOX = """
components:
  box:
    params: {kind: USER}
    status: [ENABLED]
    template: |
      WINDOWTYPE = {kind};
      SCREENRECT = UPPERLEFT: {x0} {y0},
                   BOTTOMRIGHT: {x1} {y1},
                   CREATIONRESOLUTION: 800 600;
      NAME = "{name}";
      STATUS = {status};
      SYSTEMCALLBACK = "{system}";
      TOOLTIPTEXT = "{tooltip}";
      TEXT = "{text}";
  cell:
    extends: box
    params: {kind: PUSHBUTTON}
    status: [ENABLED, IMAGE]
    place: {left: inset, right: inset, h: row}
"""
TOKENS = """
tokens:
  inset: 6
  row: 20
  gap: 4
"""


def compile_yaml(body, tokens=TOKENS):
    return wnd_yaml.compile_text(BOX + tokens + textwrap.dedent(body), "Test.wnd", "test.yaml")


def rects(text):
    return {w.name.split(":", 1)[1]: w.rect for w in wl.parse_windows(text).walk()}


def statuses(text):
    return {w.name.split(":", 1)[1]: "+".join(sorted(w.status)) for w in wl.parse_windows(text).walk()}


def error(body, tokens=TOKENS):
    with unittest.TestCase().assertRaises(wnd_yaml.LayoutError) as caught:
        compile_yaml(body, tokens)
    return str(caught.exception)


class ContainerTest(unittest.TestCase):
    def test_column_stacks_items_with_its_gap_and_pad(self):
        out = compile_yaml("""
            windows:
              - component: box
                name: Panel
                rect: [100, 100, 300, 400]
                column:
                  pad: [10, 0, 0, 0]
                  gap: gap
                  items:
                    - {component: cell, name: A}
                    - {component: cell, name: B}
                    - {component: cell, name: C, h: 30}
            """)
        r = rects(out)
        self.assertEqual(r["A"], (106, 110, 294, 130))
        self.assertEqual(r["B"], (106, 134, 294, 154))
        self.assertEqual(r["C"], (106, 158, 294, 188))

    def test_row_gives_fill_items_the_remaining_width(self):
        out = compile_yaml("""
            windows:
              - component: box
                name: Bar
                rect: [0, 0, 400, 40]
                row:
                  pad: [0, 10, 0, 10]
                  gap: 4
                  items:
                    - {component: box, name: Fill}
                    - {component: box, name: Fixed, w: 100}
            """)
        r = rects(out)
        self.assertEqual(r["Fill"], (10, 0, 286, 40))
        self.assertEqual(r["Fixed"], (290, 0, 390, 40))

    def test_grid_lays_label_and_control_rows_in_two_columns(self):
        out = compile_yaml("""
            windows:
              - component: box
                name: Panel
                rect: [0, 0, 200, 300]
                grid:
                  columns: [fill, 90]
                  col_gap: 4
                  row_gap: 2
                  items:
                    - column:
                        items:
                          - {component: box, name: LabelA, h: 10}
                          - {component: cell, name: ControlA}
                    - column:
                        items:
                          - {component: box, name: LabelB, h: 10}
                          - {component: cell, name: ControlB}
                    - {component: cell, name: Alone}
                    - ~
                    - {component: cell, name: Wide, span: 2}
            """)
        r = rects(out)
        self.assertEqual(r["LabelA"], (0, 0, 106, 10))
        self.assertEqual(r["ControlA"], (6, 10, 100, 30))
        self.assertEqual(r["LabelB"], (110, 0, 200, 10))
        self.assertEqual(r["ControlB"], (116, 10, 194, 30))
        self.assertEqual(r["Alone"], (6, 32, 100, 52))
        self.assertEqual(r["Wide"], (6, 54, 194, 74))

    def test_overlay_items_share_one_box(self):
        out = compile_yaml("""
            windows:
              - component: box
                name: Tabs
                rect: [0, 0, 100, 100]
                overlay:
                  items:
                    - {component: box, name: PageA}
                    - {component: box, name: PageB}
            """)
        r = rects(out)
        self.assertEqual(r["PageA"], (0, 0, 100, 100))
        self.assertEqual(r["PageB"], (0, 0, 100, 100))

    def test_children_fill_the_window_unless_pinned(self):
        out = compile_yaml("""
            windows:
              - component: box
                name: Dialog
                rect: [10, 10, 90, 90]
                children:
                  - {component: box, name: Back}
                  - {component: cell, name: Ok, rect: [20, 70, 40, 80]}
            """)
        r = rects(out)
        self.assertEqual(r["Back"], (10, 10, 90, 90))
        self.assertEqual(r["Ok"], (20, 70, 40, 80))

    def test_window_without_height_takes_the_height_of_its_column(self):
        out = compile_yaml("""
            windows:
              - component: box
                name: Outer
                rect: [0, 0, 200, 500]
                column:
                  gap: 10
                  items:
                    - component: box
                      name: Auto
                      column:
                        pad: 5
                        items:
                          - {component: cell, name: One}
                          - {component: cell, name: Two}
                    - {component: cell, name: After}
            """)
        r = rects(out)
        self.assertEqual(r["Auto"], (0, 0, 200, 50))
        self.assertEqual(r["Two"], (11, 25, 189, 45))
        self.assertEqual(r["After"], (6, 60, 194, 80))

    def test_explicit_rect_and_middle_alignment(self):
        out = compile_yaml("""
            windows:
              - component: box
                name: Band
                rect: [0, 0, 100, 24]
                row:
                  items:
                    - {component: box, name: Tab, w: 40, h: 16, valign: middle}
                    - {component: box, name: Pinned, rect: [500, 500, 510, 510]}
            """)
        r = rects(out)
        self.assertEqual(r["Tab"], (0, 4, 40, 20))
        self.assertEqual(r["Pinned"], (500, 500, 510, 510))


class TokenTest(unittest.TestCase):
    def test_tokens_set_gaps_insets_and_sizes(self):
        body = """
            windows:
              - component: box
                name: Panel
                rect: [0, 0, 100, 100]
                column:
                  gap: gap
                  items:
                    - {component: cell, name: A}
                    - {component: cell, name: B}
            """
        self.assertEqual(rects(compile_yaml(body))["B"], (6, 24, 94, 44))
        wider = "\ntokens:\n  inset: 10\n  row: 30\n  gap: 8\n"
        self.assertEqual(rects(compile_yaml(body, wider))["B"], (10, 38, 90, 68))

    def test_unknown_token_names_the_key(self):
        message = error("""
            windows:
              - {component: box, name: A, rect: [0, 0, 10, 10], column: {gap: nope, items: []}}
            """)
        self.assertIn("test.yaml", message)
        self.assertIn("windows[0].column.gap", message)
        self.assertIn("nope", message)


class RepeatTest(unittest.TestCase):
    def test_count_repeat_names_items_from_the_pattern(self):
        out = compile_yaml("""
            windows:
              - component: box
                name: List
                rect: [0, 0, 100, 200]
                column:
                  items:
                    - repeat:
                        count: 3
                        item: {component: cell, name: "Slot{i}", text: "GUI:Slot{i}"}
            """)
        r = rects(out)
        self.assertEqual([n for n in r if n.startswith("Slot")], ["Slot1", "Slot2", "Slot3"])
        self.assertEqual(r["Slot3"], (6, 40, 94, 60))
        self.assertIn('TEXT = "GUI:Slot2";', out)

    def test_each_repeat_takes_fields_from_every_entry(self):
        out = compile_yaml("""
            windows:
              - component: box
                name: List
                rect: [0, 0, 100, 200]
                column:
                  items:
                    - repeat:
                        each: [{key: Shadows}, {key: Water}]
                        item: {component: cell, name: "Check{key}", tooltip: "TOOLTIP:{key}"}
            """)
        r = rects(out)
        self.assertEqual(r["CheckShadows"], (6, 0, 94, 20))
        self.assertEqual(r["CheckWater"], (6, 20, 94, 40))
        self.assertIn('TOOLTIPTEXT = "TOOLTIP:Water";', out)


class HiddenTest(unittest.TestCase):
    def test_hidden_windows_keep_their_rect_and_get_hidden_status(self):
        out = compile_yaml("""
            windows:
              - component: box
                name: Panel
                rect: [0, 0, 100, 100]
                hidden_windows:
                  - {component: cell, name: OldButton, rect: [700, 10, 760, 30], text: "GUI:Old"}
            """)
        self.assertEqual(rects(out)["OldButton"], (700, 10, 760, 30))
        self.assertIn("STATUS = ENABLED+HIDDEN+IMAGE;", out)
        self.assertEqual(statuses(out)["Panel"], "ENABLED")

    def test_hidden_window_needs_a_rect(self):
        message = error("""
            windows:
              - component: box
                name: Panel
                rect: [0, 0, 100, 100]
                hidden_windows:
                  - {component: cell, name: OldButton}
            """)
        self.assertIn("windows[0].hidden_windows[0]", message)
        self.assertIn("rect", message)


class StrictLoadTest(unittest.TestCase):
    def test_duplicate_key_is_rejected_with_its_path(self):
        message = error("""
            windows:
              - component: box
                name: A
                name: B
                rect: [0, 0, 10, 10]
            """)
        self.assertIn("test.yaml", message)
        self.assertIn("windows[0].name", message)
        self.assertIn("duplicate", message)

    def test_unknown_key_is_rejected_with_its_path(self):
        message = error("""
            windows:
              - {component: box, name: A, rect: [0, 0, 10, 10], colour: red}
            """)
        self.assertIn("windows[0].colour", message)
        self.assertIn("unknown key", message)

    def test_yaml_booleans_do_not_pass_as_text(self):
        message = error("""
            windows:
              - {component: box, name: A, rect: [0, 0, 10, 10], text: no}
            """)
        self.assertIn("windows[0].text", message)
        self.assertIn("string", message)

    def test_wrong_types_are_rejected(self):
        cases = {
            "hidden": ('hidden: "yes"', "windows[0].hidden"),
            "rect": ("rect: [0, 0, 10]", "windows[0].rect"),
            "width": ("w: 1.5", "windows[0].w"),
            "param": ("params: {kind: on}", "windows[0].params.kind"),
            "status": ("status: [ENABLED, SHINY]", "windows[0].status"),
        }
        for label, (field, path) in cases.items():
            rect = "" if field.startswith("rect") else "rect: [0, 0, 10, 10], "
            message = error(f"windows:\n  - {{component: box, name: A, {rect}{field}}}\n")
            self.assertIn(path, message, label)

    def test_unknown_component_and_param_are_rejected(self):
        self.assertIn("windows[0].component", error("windows:\n  - {component: nothing, name: A, rect: [0, 0, 1, 1]}\n"))
        self.assertIn("windows[0].params.colour", error("windows:\n  - {component: box, name: A, rect: [0, 0, 1, 1], params: {colour: x}}\n"))

    def test_span_outside_a_grid_is_rejected(self):
        message = error("""
            windows:
              - {component: box, name: A, rect: [0, 0, 10, 10], column: {items: [{component: cell, name: B, span: 2}]}}
            """)
        self.assertIn("windows[0].column.items[0]", message)
        self.assertIn("span", message)

    def test_order_must_list_every_emitted_child(self):
        message = error("""
            windows:
              - component: box
                name: Panel
                rect: [0, 0, 100, 100]
                order: [A]
                column:
                  items:
                    - {component: cell, name: A}
                    - {component: cell, name: B}
            """)
        self.assertIn("windows[0].order", message)


class OutputTest(unittest.TestCase):
    BODY = """
        layout_block: {init: TestInit, update: TestUpdate, shutdown: TestShutdown}
        windows:
          - component: box
            name: Root
            rect: [0, 0, 799, 599]
            callbacks: {system: TestSystem}
            order: [Line, Panel]
            column:
              items:
                - component: box
                  name: Panel
                  h: 50
                  column:
                    items:
                      - {component: cell, name: Inner}
                      - {component: box, name: Line, h: 1, child_of: Root}
        """

    def test_file_layout_matches_the_engine_format(self):
        out = compile_yaml(self.BODY)
        self.assertNotIn("\n", out.replace("\r\n", ""))
        self.assertTrue(out.endswith("END\r\n"))
        lines = out.split("\r\n")
        self.assertEqual(lines[:6], ["FILE_VERSION = 2;", "STARTLAYOUTBLOCK", "  LAYOUTINIT = TestInit;",
                                     "  LAYOUTUPDATE = TestUpdate;", "  LAYOUTSHUTDOWN = TestShutdown;", "ENDLAYOUTBLOCK"])
        self.assertEqual(wl.structure_counts(out), {"WINDOW": 4, "END": 4, "CHILD": 3, "ENDALLCHILDREN": 2})
        self.assertIn('  NAME = "Test.wnd:Root";', lines)
        self.assertIn('  SYSTEMCALLBACK = "TestSystem";', lines)
        self.assertIn('      SYSTEMCALLBACK = "[None]";', lines)

    def test_child_of_and_order_set_the_emitted_tree(self):
        root = wl.parse_windows(compile_yaml(self.BODY))
        tree = [(w.name, [c.name for c in w.children]) for w in root.walk()]
        self.assertEqual(tree, [
            ("Test.wnd:Root", ["Test.wnd:Line", "Test.wnd:Panel"]),
            ("Test.wnd:Line", []),
            ("Test.wnd:Panel", ["Test.wnd:Inner"]),
            ("Test.wnd:Inner", []),
        ])
        self.assertEqual(rects(compile_yaml(self.BODY))["Line"], (0, 20, 799, 21))

    def test_text_and_tooltip_lines_only_when_set(self):
        out = compile_yaml("windows:\n  - {component: box, name: A, rect: [0, 0, 1, 1]}\n")
        self.assertNotIn("TEXT", out.replace("TOOLTIPTEXT", ""))
        self.assertNotIn("TOOLTIPTEXT", out)

    def test_compiling_twice_gives_the_same_bytes(self):
        self.assertEqual(compile_yaml(self.BODY), compile_yaml(self.BODY))
        source = wnd_yaml.sources()[OPTIONS]
        self.assertEqual(wnd_yaml.compile_file(source), wnd_yaml.compile_file(source))


def records(text):
    """Return one record per window in file order: kind, name, status, callbacks, text, tooltip, parent and rect."""
    out, stack = [], []
    current = None
    for line in text.split("\r\n"):
        token = line.strip()
        if token == "WINDOW":
            current = {"parent": stack[-1]["index"] if stack else None, "index": len(out), "status": set(), "text": None,
                       "tooltip": None, "callbacks": {}}
            out.append(current)
            stack.append(current)
        elif token == "END":
            stack.pop()
        elif stack:
            win = stack[-1]
            if m := re.match(r'(SYSTEM|INPUT|TOOLTIP|DRAW)CALLBACK = "([^"]*)";', token):
                win["callbacks"][m.group(1)] = m.group(2)
            elif m := re.match(r"WINDOWTYPE = (\w+);", token):
                win["kind"] = m.group(1)
            elif m := re.match(r'NAME = "([^"]*)";', token):
                win["name"] = m.group(1)
            elif m := re.match(r"STATUS = ([^;]*);", token):
                win["status"] = set(m.group(1).split("+"))
            elif m := re.match(r'TEXT = "([^"]*)";', token):
                win["text"] = m.group(1)
            elif m := re.match(r'TOOLTIPTEXT = "([^"]*)";', token):
                win["tooltip"] = m.group(1)
    for win, parsed in zip(out, wl.parse_windows(text).walk()):
        win["rect"] = parsed.rect
        win["hidden"] = "HIDDEN" in win["status"] or (win["parent"] is not None and out[win["parent"]]["hidden"])
    return out


# the hand-made Options layout the YAML pilot reproduces, before the YAML source existed
PILOT_BASELINE = "bdcd83ab94"


def committed_options():
    try:
        raw = subprocess.run(["git", "show", f"{PILOT_BASELINE}:GeneralsOnline/GameData/{OPTIONS}"], cwd=ROOT, capture_output=True,
                             check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return raw.decode("latin-1")


class OptionsEquivalenceTest(unittest.TestCase):
    """The pilot: the compiled Options layout against the generated file committed at HEAD."""

    @classmethod
    def setUpClass(cls):
        cls.old = committed_options()
        if cls.old is None:
            raise unittest.SkipTest("git or the committed OptionsMenu.wnd is not available")
        cls.new = wnd_yaml.compile_file(wnd_yaml.sources()[OPTIONS])

    def test_same_windows_types_status_callbacks_text_and_parents(self):
        strip = ("rect", "hidden")
        old = [{k: v for k, v in r.items() if k not in strip} for r in records(self.old)]
        new = [{k: v for k, v in r.items() if k not in strip} for r in records(self.new)]
        self.assertEqual(len(new), len(old))
        for a, b in zip(old, new):
            self.assertEqual(b, a, a.get("name"))

    def test_visible_rects_within_two_units(self):
        for a, b in zip(records(self.old), records(self.new)):
            if a["hidden"]:
                continue
            worst = max(abs(p - q) for p, q in zip(a["rect"], b["rect"]))
            self.assertLessEqual(worst, 2, f"{a['name']} {a['rect']} -> {b['rect']}")

    def test_output_is_already_final(self):
        # kit colours, GO header templates and hover images are in the source, so the generator passes it through
        self.assertEqual(wl.remap_colors(self.new), self.new)
        self.assertEqual(wl.go_templates(self.new, OPTIONS), self.new)
        self.assertEqual(wl.hilite_images(self.new), self.new)

    def test_lint_reports_no_new_findings(self):
        if not wl.RETAIL_BIG.is_file():
            self.skipTest("retail WindowZH.big not found")
        code = "\n".join(p.read_bytes().decode("latin-1") for p in sorted(lint.CODE.rglob("*.cpp")))
        allow = lint.parse_allow(lint.ALLOW.read_text("utf-8"))
        new, _ = lint.apply_allow(lint.lint_layouts({OPTIONS: self.new}, code, lint.load_retail()), allow)
        self.assertEqual([str(f) for f in new], [])


if __name__ == "__main__":
    unittest.main()
