"""Tests for scripts/go_wnd_layouts.py.

Usage:
    python -m unittest scripts/test_go_wnd_layouts.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_wnd_layouts as wl  # noqa: E402

DRAW_SLOT = "IMAGE: NoImage, COLOR: 255 255 255 255, BORDERCOLOR: 255 255 255 255"


def window(kind, rect, children=(), indent=0, draw=None):
    """Build one WINDOW block in the retail layout, with CRLF line endings."""
    pad = " " * indent
    x0, y0, x1, y1 = rect
    slots = draw or [DRAW_SLOT] * 9
    lines = [
        f"{pad}WINDOW",
        f"{pad}  WINDOWTYPE = {kind};",
        f"{pad}  SCREENRECT = UPPERLEFT: {x0} {y0},",
        f"{pad}               BOTTOMRIGHT: {x1} {y1},",
        f"{pad}               CREATIONRESOLUTION: 800 600;",
        f'{pad}  NAME = "Test.wnd:{kind}{x0}_{y0}";',
        f"{pad}  TEXTCOLOR = ENABLED:  255 255 255 255, ENABLEDBORDER:  47 55 168 255,",
        f"{pad}              DISABLED: 255 255 255 255, DISABLEDBORDER: 49 55 168 128,",
        f"{pad}              HILITE:   128 128 255 255, HILITEBORDER:   128 128 255 0;",
    ]
    lead = f"{pad}  ENABLEDDRAWDATA = "
    for i, slot in enumerate(slots):
        head = lead if i == 0 else " " * len(lead)
        lines.append(head + slot + (";" if i == len(slots) - 1 else ","))
    text = "\r\n".join(lines) + "\r\n"
    for child in children:
        text += f"{pad}  CHILD\r\n" + child
    if children:
        text += f"{pad}  ENDALLCHILDREN\r\n"
    return text + f"{pad}END\r\n"


def layout(*windows):
    return "FILE_VERSION = 2;\r\nSTARTLAYOUTBLOCK\r\nENDLAYOUTBLOCK\r\n" + "".join(windows)


def rects(text):
    return [w.rect for w in wl.parse_windows(text).walk()]


class ColorRemapTest(unittest.TestCase):
    def test_remap_keeps_alpha_and_leaves_other_colours(self):
        draw = [
            "IMAGE: NoImage, COLOR: 47 55 168 255, BORDERCOLOR: 128 128 255 77",
            "IMAGE: NoImage, COLOR: 46 54 165 0, BORDERCOLOR: 49 55 168 12",
            "IMAGE: NoImage, COLOR: 10 20 30 255, BORDERCOLOR: 47 55 169 255",
        ] + [DRAW_SLOT] * 6
        text = layout(window("PUSHBUTTON", (0, 0, 100, 20), draw=draw))
        out = wl.remap_colors(text)
        self.assertIn("COLOR: 47 75 255 255, BORDERCOLOR: 106 123 255 77", out)
        self.assertIn("COLOR: 47 75 255 0, BORDERCOLOR: 47 75 255 12", out)
        self.assertIn("COLOR: 10 20 30 255, BORDERCOLOR: 47 55 169 255", out)
        self.assertIn("ENABLEDBORDER:  47 75 255 255", out)
        self.assertIn("DISABLEDBORDER: 47 75 255 128", out)
        self.assertIn("HILITEBORDER:   106 123 255 0", out)
        self.assertIn("HILITE:   128 128 255 255", out)  # text colour, not a border
        self.assertEqual(len(out), len(text))


class EqualRowsTest(unittest.TestCase):
    def uneven_rows(self):
        return layout(window("USER", (0, 0, 800, 600), [
            window("COMBOBOX", (100, 100, 300, 122), indent=2),
            window("COMBOBOX", (100, 127, 300, 150), indent=2),
            window("COMBOBOX", (100, 156, 300, 178), indent=2),
            window("PUSHBUTTON", (400, 50, 500, 80), indent=2),
        ]))

    def test_uneven_run_becomes_equal_and_keeps_first_top(self):
        out = wl.transform(self.uneven_rows())
        combos = [w.rect for w in wl.parse_windows(out).walk() if w.kind == "COMBOBOX"]
        self.assertEqual(combos, [(100, 100, 300, 122), (100, 127, 300, 149), (100, 154, 300, 176)])
        button = [w.rect for w in wl.parse_windows(out).walk() if w.kind == "PUSHBUTTON"]
        self.assertEqual(button, [(400, 50, 500, 80)])

    def test_children_move_with_their_row(self):
        def row(y, h):
            return window("USER", (100, y, 300, y + h), [
                window("STATICTEXT", (110, y + 2, 200, y + 12), indent=4),
            ], indent=2)

        text = layout(window("USER", (0, 0, 800, 600), [row(100, 30), row(135, 31), row(171, 30)]))
        out = wl.transform(text)
        labels = [w.rect for w in wl.parse_windows(out).walk() if w.kind == "STATICTEXT"]
        self.assertEqual(labels, [(110, 102, 200, 112), (110, 137, 200, 147), (110, 172, 200, 182)])
        rows = [w.rect for w in wl.parse_windows(out).walk() if w.kind == "USER"][1:]
        self.assertEqual(rows, [(100, 100, 300, 130), (100, 135, 300, 165), (100, 170, 300, 200)])

    def test_horizontal_run_becomes_equal(self):
        text = layout(window("USER", (0, 0, 800, 600), [
            window("PUSHBUTTON", (10, 500, 110, 530), indent=2),
            window("PUSHBUTTON", (120, 500, 221, 530), indent=2),
            window("PUSHBUTTON", (231, 500, 331, 530), indent=2),
        ]))
        out = wl.transform(text)
        buttons = [w.rect for w in wl.parse_windows(out).walk() if w.kind == "PUSHBUTTON"]
        self.assertEqual(buttons, [(10, 500, 110, 530), (120, 500, 220, 530), (230, 500, 330, 530)])

    def test_same_rect_alternates_move_as_one_row(self):
        text = layout(window("USER", (0, 0, 800, 600), [
            window("PUSHBUTTON", (20, 100, 44, 124), indent=2),
            window("PUSHBUTTON", (20, 130, 44, 155), indent=2),
            window("PUSHBUTTON", (20, 130, 44, 155), indent=2),
            window("PUSHBUTTON", (20, 160, 44, 184), indent=2),
        ]))
        out = wl.equalize_rows(text)[0]
        buttons = [w.rect for w in wl.parse_windows(out).walk() if w.kind == "PUSHBUTTON"]
        self.assertEqual(buttons, [(20, 100, 44, 124), (20, 130, 44, 154), (20, 130, 44, 154), (20, 160, 44, 184)])

    def test_exact_run_is_byte_identical(self):
        text = layout(window("USER", (0, 0, 800, 600), [
            window("CHECKBOX", (100, 100, 300, 122), indent=2),
            window("CHECKBOX", (100, 127, 300, 149), indent=2),
            window("CHECKBOX", (100, 154, 300, 176), indent=2),
        ]))
        self.assertEqual(wl.equalize_rows(text)[0], text)

    def test_row_never_leaves_its_parent(self):
        text = layout(window("USER", (100, 100, 300, 176), [
            window("CHECKBOX", (100, 100, 300, 123), indent=2),
            window("CHECKBOX", (100, 127, 300, 150), indent=2),
            window("CHECKBOX", (100, 154, 300, 176), indent=2),
        ]))
        self.assertEqual(wl.equalize_rows(text)[0], text)

    def test_crlf_is_preserved(self):
        out = wl.transform(self.uneven_rows())
        self.assertNotIn("\n", out.replace("\r\n", ""))
        self.assertEqual(out.count("\r\n"), self.uneven_rows().count("\r\n"))

    def test_transform_is_idempotent(self):
        once = wl.transform(self.uneven_rows())
        self.assertNotEqual(once, self.uneven_rows())
        self.assertEqual(wl.transform(once), once)

    def test_structure_counts_are_kept(self):
        text = self.uneven_rows()
        self.assertEqual(wl.structure_counts(wl.transform(text)), wl.structure_counts(text))


if __name__ == "__main__":
    unittest.main()
