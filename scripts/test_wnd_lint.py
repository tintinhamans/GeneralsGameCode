"""Tests for scripts/wnd_lint.py.

Usage:
    python -m unittest scripts/test_wnd_lint.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import wnd_lint as lint  # noqa: E402
from test_go_wnd_layouts import layout, window  # noqa: E402

FILE = "Window/Menus/Test.wnd"


def hidden(kind, rect, children=(), **kw):
    return window(kind, rect, children, props=["STATUS = ENABLED+HIDDEN;"], **kw)


def rules(text, code="", retail=None):
    return sorted((f.rule, f.name) for f in lint.lint_layouts({FILE: text}, code, retail))


class ChildInsideParentTest(unittest.TestCase):
    def test_clickable_child_outside_its_parent_is_reported(self):
        text = layout(window("USER", (0, 0, 800, 600), [
            window("USER", (100, 100, 300, 200), [window("PUSHBUTTON", (80, 120, 120, 140), indent=4)], indent=2),
        ]))
        self.assertEqual(rules(text), [("outside-parent", "Test.wnd:PUSHBUTTON80_120")])

    def test_child_inside_its_parent_passes(self):
        text = layout(window("USER", (0, 0, 800, 600), [
            window("USER", (100, 100, 300, 200), [window("PUSHBUTTON", (110, 120, 150, 140), indent=4)], indent=2),
        ]))
        self.assertEqual(rules(text), [])

    def test_hidden_child_or_child_of_a_hidden_parent_is_skipped(self):
        text = layout(window("USER", (0, 0, 800, 600), [
            window("USER", (100, 100, 300, 200), [hidden("PUSHBUTTON", (80, 120, 120, 140), indent=4)], indent=2),
            hidden("USER", (400, 100, 500, 200), [window("CHECKBOX", (380, 120, 420, 140), indent=4)], indent=2),
        ]))
        self.assertEqual(rules(text), [])


class OverlapTest(unittest.TestCase):
    def test_overlapping_clickable_siblings_are_reported(self):
        text = layout(window("USER", (0, 0, 800, 600), [
            window("PUSHBUTTON", (100, 100, 200, 130), indent=2),
            window("PUSHBUTTON", (180, 100, 280, 130), indent=2),
        ]))
        self.assertEqual(rules(text), [("overlap", "Test.wnd:PUSHBUTTON100_100")])

    def test_touching_or_hidden_siblings_pass(self):
        text = layout(window("USER", (0, 0, 800, 600), [
            window("PUSHBUTTON", (100, 100, 200, 130), indent=2),
            window("PUSHBUTTON", (200, 100, 300, 130), indent=2),
            hidden("PUSHBUTTON", (150, 100, 250, 130), indent=2),
        ]))
        self.assertEqual(rules(text), [])


class CodeNameTest(unittest.TestCase):
    TEXT = layout(window("USER", (0, 0, 800, 600), [window("PUSHBUTTON", (100, 100, 200, 130), indent=2)]))

    def test_name_the_code_looks_up_but_the_layout_lacks_is_reported(self):
        code = 'nameToKey("Test.wnd:ButtonGone"); nameToKey("Test.wnd:PUSHBUTTON100_100");'
        self.assertEqual(rules(self.TEXT, code), [("missing-name", "Test.wnd:ButtonGone")])

    def test_names_retail_never_had_are_skipped_and_dropped_ones_reported(self):
        code = 'nameToKey("Test.wnd:NeverThere"); nameToKey("Test.wnd:Dropped");'
        retail = {"test.wnd": {"Test.wnd:Dropped"}}
        self.assertEqual(rules(self.TEXT, code, retail), [("missing-name", "Test.wnd:Dropped")])

    def test_lookups_in_layouts_go_does_not_ship_are_skipped(self):
        self.assertEqual(rules(self.TEXT, 'nameToKey("Other.wnd:Anything");'), [])


class AllowListTest(unittest.TestCase):
    def test_allowed_findings_are_dropped_and_unused_entries_reported(self):
        findings = [lint.Finding("overlap", FILE, "Test.wnd:A", ""), lint.Finding("overlap", FILE, "Test.wnd:B", "")]
        allow = lint.parse_allow("overlap Test.wnd:A  # two hit areas on purpose\nmissing-name Test.wnd:Old  # gone\n")
        new, unused = lint.apply_allow(findings, allow)
        self.assertEqual([f.name for f in new], ["Test.wnd:B"])
        self.assertEqual(unused, [("missing-name", "Test.wnd:Old")])

    def test_entries_need_a_reason(self):
        with self.assertRaises(ValueError):
            lint.parse_allow("overlap Test.wnd:A\n")


class WildcardAllowTest(unittest.TestCase):
    def test_a_wildcard_entry_covers_every_matching_name(self):
        findings = [lint.Finding("overlap", FILE, f"Test.wnd:Marker{i}", "") for i in range(3)]
        new, unused = lint.apply_allow(findings, lint.parse_allow("overlap *.wnd:Marker*  # placed by code\n"))
        self.assertEqual((new, unused), ([], []))


class ShippedLayoutsTest(unittest.TestCase):
    def test_shipped_layouts_have_no_new_findings(self):
        new, unused = lint.run()
        self.assertEqual([str(f) for f in new], [])
        self.assertEqual(unused, [])


if __name__ == "__main__":
    unittest.main()
