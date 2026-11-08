"""Tests for scripts/go_wnd_layouts.py.

Usage:
    python -m unittest scripts/test_go_wnd_layouts.py
"""

import sys
import unittest
from pathlib import Path
from unittest import mock

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

    def test_row_never_leaves_its_parent_and_takes_a_size_that_fits(self):
        text = layout(window("USER", (100, 100, 300, 176), [
            window("CHECKBOX", (100, 100, 300, 123), indent=2),
            window("CHECKBOX", (100, 127, 300, 150), indent=2),
            window("CHECKBOX", (100, 154, 300, 176), indent=2),
        ]))
        # the most common height (23) would push the last row out, so the run evens at 22
        out = wl.equalize_rows(text)[0]
        self.assertEqual(by_kind(out, "CHECKBOX"), [(100, 100, 300, 122), (100, 126, 300, 148), (100, 152, 300, 174)])

    def test_run_that_cannot_fit_is_left_alone(self):
        # gap 4 would overlap the marker under the second row, gap 5 would leave the parent
        text = layout(window("USER", (100, 100, 300, 178), [
            window("CHECKBOX", (100, 100, 300, 123), indent=2),
            window("CHECKBOX", (100, 127, 300, 150), indent=2),
            window("CHECKBOX", (100, 155, 300, 178), indent=2),
            window("PUSHBUTTON", (150, 152, 250, 155), indent=2),
        ]))
        notes = []
        self.assertEqual(wl.equalize_rows(text, notes)[0], text)
        self.assertTrue(any("skipped" in n for n in notes), notes)

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


RULER_DRAW = ["IMAGE: MainMenuRuler, COLOR: 0 0 0 255, BORDERCOLOR: 0 0 0 255"] + [DRAW_SLOT] * 8
MENU = "Window/Menus/Test.wnd"


def by_kind(text, kind):
    return [w.rect for w in wl.parse_windows(text).walk() if w.kind == kind]


def screen(*children):
    """A full-screen USER parent (no inset rule) around the given children."""
    return layout(window("USER", (0, 0, 800, 600), [*children]))


class FrameOnRulerTest(unittest.TestCase):
    def ruler_layout(self, frame=(41, 41, 759, 559)):
        return layout(window("USER", (0, 0, 800, 600), [
            window("USER", frame, [
                window("PUSHBUTTON", (100, 500, 200, 530), indent=4),
            ], indent=2),
            window("PUSHBUTTON", (600, 520, 700, 550), indent=2),
            window("PUSHBUTTON", (10, 10, 30, 30), indent=2),
        ], draw=RULER_DRAW))

    def test_frame_and_descendants_map_onto_the_ruler(self):
        out = wl.transform(self.ruler_layout(frame=(45, 45, 755, 555)), name=MENU)
        self.assertEqual(by_kind(out, "USER"), [(0, 0, 800, 600), (41, 41, 759, 559)])
        # descendant mapped linearly, sibling inside the old frame box mapped too, outside sibling kept
        self.assertEqual(by_kind(out, "PUSHBUTTON"), [(97, 503, 198, 534), (602, 523, 703, 554), (10, 10, 30, 30)])

    def test_ruler_parent_is_found_by_its_image_only(self):
        text = self.ruler_layout().replace("MainMenuRuler", "SomeOtherImage")
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "USER")[1], (41, 41, 759, 559))

    def test_in_game_layouts_never_map_frames(self):
        out = wl.transform(self.ruler_layout(), name="Window/ControlBar.wnd")
        self.assertEqual(by_kind(out, "USER")[1], (41, 41, 759, 559))

    def test_equal_sizes_stay_equal_after_mapping(self):
        text = layout(window("USER", (0, 0, 800, 600), [
            window("USER", (41, 41, 759, 559), [
                window("PUSHBUTTON", (100, 500, 200, 530), indent=4),
                window("PUSHBUTTON", (330, 500, 430, 530), indent=4),
            ], indent=2),
        ], draw=RULER_DRAW))
        widths = {r[2] - r[0] for r in by_kind(wl.transform(text, name=MENU), "PUSHBUTTON")}
        self.assertEqual(len(widths), 1)


class EdgeSnapTest(unittest.TestCase):
    def test_left_edges_within_three_snap_and_beyond_three_do_not(self):
        text = screen(
            window("SCROLLLISTBOX", (100, 100, 300, 150), indent=2),
            window("ENTRYFIELD", (102, 200, 302, 220), indent=2),
            window("PUSHBUTTON", (100, 300, 200, 330), indent=2),
            window("CHECKBOX", (104, 400, 204, 430), indent=2),
        )
        out = wl.transform(text, name=MENU)
        self.assertEqual(by_kind(out, "ENTRYFIELD"), [(100, 200, 300, 220)])
        self.assertEqual(by_kind(out, "CHECKBOX"), [(104, 400, 204, 430)])

    def test_main_menu_and_control_bar_keep_their_geometry(self):
        text = screen(
            window("SCROLLLISTBOX", (100, 100, 300, 150), indent=2),
            window("ENTRYFIELD", (102, 200, 302, 220), indent=2),
        )
        for name in ("Window/Menus/MainMenu.wnd", "Window/ControlBar.wnd"):
            self.assertEqual(wl.transform(text, name=name), wl.remap_colors(text))

    def test_right_edges_of_one_column_get_the_same_width(self):
        text = screen(
            window("SCROLLLISTBOX", (100, 100, 300, 150), indent=2),
            window("SCROLLLISTBOX", (100, 160, 300, 180), indent=2),
            window("ENTRYFIELD", (100, 200, 302, 220), indent=2),
        )
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "ENTRYFIELD"), [(100, 200, 300, 220)])

    def test_top_edges_within_three_snap(self):
        text = screen(
            window("SCROLLLISTBOX", (100, 500, 200, 530), indent=2),
            window("ENTRYFIELD", (300, 502, 400, 532), indent=2),
        )
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "ENTRYFIELD"), [(300, 500, 400, 530)])

    def test_snap_that_would_overlap_is_skipped_and_logged(self):
        text = screen(
            window("SCROLLLISTBOX", (100, 100, 200, 150), indent=2),
            window("ENTRYFIELD", (103, 150, 203, 200), indent=2),
            window("PUSHBUTTON", (60, 165, 102, 185), indent=2),
        )
        notes = []
        out = wl.transform(text, notes, name=MENU)
        self.assertEqual(by_kind(out, "ENTRYFIELD"), [(103, 150, 203, 200)])
        self.assertTrue(any("skip" in n and "ENTRYFIELD" in n for n in notes), notes)


class LabelTest(unittest.TestCase):
    def test_label_is_centred_on_the_control_to_its_right(self):
        text = screen(
            window("STATICTEXT", (100, 100, 180, 114), indent=2),
            window("COMBOBOX", (190, 98, 300, 122), indent=2),
        )
        out = wl.transform(text, name=MENU)
        self.assertEqual(by_kind(out, "STATICTEXT"), [(100, 103, 180, 117)])
        self.assertEqual(by_kind(out, "COMBOBOX"), [(190, 98, 300, 122)])

    def test_label_far_from_its_control_is_left_alone(self):
        text = screen(
            window("STATICTEXT", (100, 100, 180, 114), indent=2),
            window("COMBOBOX", (230, 96, 300, 126), indent=2),
        )
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "STATICTEXT"), [(100, 100, 180, 114)])


class EvenSpacingTest(unittest.TestCase):
    def test_run_within_three_units_is_evened(self):
        text = screen(
            window("PUSHBUTTON", (100, 100, 200, 125), indent=2),
            window("PUSHBUTTON", (100, 130, 200, 152), indent=2),
            window("PUSHBUTTON", (100, 158, 200, 183), indent=2),
        )
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "PUSHBUTTON"),
                         [(100, 100, 200, 125), (100, 130, 200, 155), (100, 160, 200, 185)])


class InsetTest(unittest.TestCase):
    def test_left_and_right_insets_become_symmetric(self):
        text = layout(window("USER", (100, 100, 300, 200), [
            window("PUSHBUTTON", (110, 120, 190, 140), indent=2),
            window("PUSHBUTTON", (200, 150, 286, 170), indent=2),
        ]))
        out = wl.transform(text, name=MENU)
        self.assertEqual(by_kind(out, "PUSHBUTTON"), [(112, 120, 192, 140), (202, 150, 288, 170)])

    def test_tight_top_and_bottom_insets_become_symmetric(self):
        text = layout(window("USER", (100, 100, 300, 200), [
            window("SCROLLLISTBOX", (110, 108, 290, 190), indent=2),
        ]))
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "SCROLLLISTBOX"), [(110, 109, 290, 191)])


class OverrideTest(unittest.TestCase):
    def test_override_sets_the_rect_and_moves_descendants(self):
        text = layout(window("USER", (100, 100, 300, 200), [
            window("PUSHBUTTON", (150, 150, 250, 170), indent=2),
        ]))
        fixes = {MENU: {"Test.wnd:USER100_100": (120, 110, 320, 210)}}
        with mock.patch.dict(wl.FIXES, fixes, clear=True):
            out = wl.transform(text, name=MENU)
        self.assertEqual(by_kind(out, "USER"), [(120, 110, 320, 210)])
        self.assertEqual(by_kind(out, "PUSHBUTTON"), [(170, 160, 270, 180)])

    def test_unknown_override_name_fails(self):
        with mock.patch.dict(wl.FIXES, {MENU: {"Test.wnd:Missing": (0, 0, 1, 1)}}, clear=True):
            with self.assertRaises(ValueError):
                wl.transform(screen(), name=MENU)


class AlignmentIdempotenceTest(unittest.TestCase):
    def test_all_rules_together_are_idempotent(self):
        text = layout(window("USER", (0, 0, 800, 600), [
            window("USER", (41, 41, 759, 559), [
                window("STATICTEXT", (60, 100, 140, 114), indent=4),
                window("COMBOBOX", (150, 98, 300, 122), indent=4),
                window("STATICTEXT", (60, 130, 140, 144), indent=4),
                window("COMBOBOX", (151, 127, 300, 149), indent=4),
                window("STATICTEXT", (60, 158, 140, 172), indent=4),
                window("COMBOBOX", (150, 155, 302, 180), indent=4),
                window("PUSHBUTTON", (60, 500, 200, 530), indent=4),
                window("PUSHBUTTON", (220, 502, 360, 532), indent=4),
                window("PUSHBUTTON", (382, 500, 521, 530), indent=4),
            ], indent=2),
        ], draw=RULER_DRAW))
        once = wl.transform(text, name=MENU)
        self.assertNotEqual(once, text)
        self.assertEqual(wl.transform(once, name=MENU), once)
        self.assertEqual(wl.structure_counts(once), wl.structure_counts(text))


if __name__ == "__main__":
    unittest.main()
