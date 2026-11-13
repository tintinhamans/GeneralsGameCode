"""Tests for scripts/go_wnd_layouts.py.

Usage:
    python -m unittest scripts/test_go_wnd_layouts.py
"""

import re
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_wnd_layouts as wl  # noqa: E402

DRAW_SLOT = "IMAGE: NoImage, COLOR: 255 255 255 255, BORDERCOLOR: 255 255 255 255"


def window(kind, rect, children=(), indent=0, draw=None, props=()):
    """Build one WINDOW block in the retail layout, with CRLF line endings; props are extra property lines."""
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
    lines += [f"{pad}  {prop}" for prop in props]
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
        # the combo then shrinks to COMBO_HEIGHT about the same centre
        self.assertEqual(by_kind(out, "COMBOBOX"), [(190, 110 - wl.COMBO_HEIGHT // 2, 300, 110 + wl.COMBO_HEIGHT // 2)])

    def test_label_and_value_box_are_centred_on_a_slider(self):
        text = screen(
            window("STATICTEXT", (41, 336, 176, 360), indent=2),
            window("HORZSLIDER", (175, 344, 265, 371), indent=2),
            window("STATICTEXT", (282, 338, 314, 359), indent=2),
        )
        out = wl.transform(text, name=MENU)
        self.assertEqual(by_kind(out, "STATICTEXT"), [(41, 346, 176, 370), (282, 347, 314, 368)])
        self.assertEqual(by_kind(out, "HORZSLIDER"), [(175, 344, 265, 371)])

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


class HeadingTest(unittest.TestCase):
    def heading_layout(self):
        return layout(window("STATICTEXT", (57, 88, 177, 112)), window("ENTRYFIELD", (48, 112, 192, 134)),
                      window("STATICTEXT", (300, 88, 380, 112)))

    def test_heading_takes_its_column_left_edge_and_keeps_its_width(self):
        headings = {MENU: {"Test.wnd:STATICTEXT57_88": "Test.wnd:ENTRYFIELD48_112"}}
        with mock.patch.dict(wl.HEADINGS, headings, clear=True):
            out, info = wl.align(self.heading_layout(), MENU)
            self.assertEqual(wl.align(out, MENU)[0], out)
        self.assertEqual(by_kind(out, "STATICTEXT"), [(48, 88, 168, 112), (300, 88, 380, 112)])
        self.assertEqual(by_kind(out, "ENTRYFIELD"), [(48, 112, 192, 134)])
        self.assertIn(("Test.wnd:STATICTEXT57_88", (57, 88, 177, 112), (48, 88, 168, 112)), info["changes"])
        with mock.patch.dict(wl.HEADINGS, {}, clear=True):
            self.assertEqual(by_kind(wl.transform(self.heading_layout(), name=MENU), "STATICTEXT")[0], (57, 88, 177, 112))

    def test_heading_that_would_overlap_a_sibling_fails(self):
        headings = {MENU: {"Test.wnd:STATICTEXT300_88": "Test.wnd:ENTRYFIELD48_112"}}
        with mock.patch.dict(wl.HEADINGS, headings, clear=True):
            with self.assertRaises(ValueError):
                wl.transform(self.heading_layout(), name=MENU)

    def test_unknown_heading_or_column_name_fails(self):
        for pair in ({"Test.wnd:Missing": "Test.wnd:ENTRYFIELD48_112"}, {"Test.wnd:STATICTEXT57_88": "Test.wnd:Missing"}):
            with mock.patch.dict(wl.HEADINGS, {MENU: pair}, clear=True):
                with self.assertRaises(ValueError):
                    wl.transform(self.heading_layout(), name=MENU)


TITLE_PROPS = ('FONT = NAME: "Generals", SIZE: 20, BOLD: 0;', 'HEADERTEMPLATE = "Title";', "STATICTEXTDATA = CENTERED: 1;")
OUT_OF_SCOPE = ("Window/Diplomacy.wnd", "Window/Menus/MainMenu.wnd", "Window/ControlBar.wnd")


def ruler_menu(*children):
    """A ruler parent around a content frame on the ruler box holding the given children."""
    return layout(window("USER", (0, 0, 800, 600), [
        window("USER", (41, 41, 759, 559), [*children], indent=2),
    ], draw=RULER_DRAW))


def header_line(y0, y1):
    return window("USER", (41, y0, 759, y1), indent=4)


def column(kind, x0, x1, top, height, pitch, rows, indent=4):
    return [window(kind, (x0, top + k * pitch, x1, top + k * pitch + height), indent=indent) for k in range(rows)]


class TitleTest(unittest.TestCase):
    def test_title_moves_to_the_shared_left_edge_and_centres_in_its_band(self):
        text = ruler_menu(
            window("STATICTEXT", (48, 43, 257, 79), indent=4, props=TITLE_PROPS),
            header_line(83, 83),
            window("PUSHBUTTON", (100, 500, 200, 530), indent=4),
        )
        notes = []
        out = wl.transform(text, notes, name=MENU)
        # band 41-83 holds the 36 unit title at 44-80; the left edge is the shared title edge
        self.assertEqual(by_kind(out, "STATICTEXT"), [(wl.TITLE_LEFT, 44, wl.TITLE_LEFT + 209, 80)])
        self.assertIn("STATICTEXTDATA = CENTERED: 0;", out)
        self.assertNotIn("CENTERED: 1", out)
        self.assertEqual(len(out), len(wl.go_templates(text, MENU)))  # only the title rect and its GO template change
        self.assertTrue(any(n.startswith("titles:") and "STATICTEXT48_43" in n for n in notes), notes)

    def test_title_taller_than_its_band_fills_the_band(self):
        text = ruler_menu(
            window("STATICTEXT", (45, 41, 444, 82), indent=4, props=TITLE_PROPS),
            header_line(79, 80),
        )
        out = wl.transform(text, name=MENU)
        self.assertEqual(by_kind(out, "STATICTEXT"), [(wl.TITLE_LEFT, 41, wl.TITLE_LEFT + 399, 79)])

    def test_small_text_in_the_band_is_not_a_title(self):
        text = ruler_menu(
            window("STATICTEXT", (70, 46, 190, 76), indent=4, props=('FONT = NAME: "Arial", SIZE: 10, BOLD: 0;',)),
            header_line(81, 82),
        )
        notes = []
        self.assertEqual(by_kind(wl.transform(text, notes, name=MENU), "STATICTEXT"), [(70, 46, 190, 76)])
        self.assertIn("titles: no title found", notes)

    def test_in_game_and_kept_layouts_get_no_title_rule(self):
        text = ruler_menu(
            window("STATICTEXT", (48, 43, 257, 79), indent=4, props=TITLE_PROPS),
            header_line(83, 83),
        )
        for name in OUT_OF_SCOPE:
            out = wl.transform(text, name=name)
            self.assertEqual(by_kind(out, "STATICTEXT"), [(48, 43, 257, 79)], name)
            self.assertIn("CENTERED: 1", out)


# alignment fixtures use 22 unit combos; keep the combo height step out of them
@mock.patch.object(wl, "COMBO_HEIGHT", 1000)
class StackGapTest(unittest.TestCase):
    def test_touching_combo_run_opens_a_two_unit_gap_and_keeps_the_pitch(self):
        text = screen(*column("COMBOBOX", 100, 300, 100, 24, 24, 3, indent=2))
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "COMBOBOX"),
                         [(100, 100, 300, 122), (100, 124, 300, 146), (100, 148, 300, 170)])

    def test_one_unit_gap_opens_to_two(self):
        text = screen(*column("ENTRYFIELD", 100, 300, 100, 24, 25, 2, indent=2))
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "ENTRYFIELD"),
                         [(100, 100, 300, 123), (100, 125, 300, 148)])

    def test_run_that_would_drop_below_the_floor_is_left_and_logged(self):
        text = screen(*column("COMBOBOX", 100, 300, 100, 19, 19, 3, indent=2))
        notes = []
        out = wl.transform(text, notes, name=MENU)
        self.assertEqual(by_kind(out, "COMBOBOX"), [(100, 100, 300, 119), (100, 119, 300, 138), (100, 138, 300, 157)])
        self.assertTrue(any(n.startswith("gaps:") and "below 18" in n for n in notes), notes)

    def test_grid_rows_stay_aligned_across_columns(self):
        text = screen(
            window("ENTRYFIELD", (48, 112, 192, 136), indent=2),
            *column("COMBOBOX", 48, 192, 136, 24, 24, 2, indent=2),
            *column("COMBOBOX", 195, 279, 112, 24, 24, 3, indent=2),
            window("STATICTEXT", (195, 88, 279, 112), indent=2),
        )
        out = wl.transform(text, name=MENU)
        rows = [(48, 112, 192, 134), (48, 136, 192, 158), (48, 160, 192, 182)]
        self.assertEqual(by_kind(out, "ENTRYFIELD") + by_kind(out, "COMBOBOX")[:2], rows)
        self.assertEqual(by_kind(out, "COMBOBOX")[2:], [(195, y0, 279, y1) for _, y0, _, y1 in rows])
        # the column header above keeps its size
        self.assertEqual(by_kind(out, "STATICTEXT"), [(195, 88, 279, 112)])

    def test_kept_and_in_game_layouts_keep_touching_runs(self):
        text = screen(*column("COMBOBOX", 100, 300, 100, 24, 24, 3, indent=2))
        for name in OUT_OF_SCOPE:
            self.assertEqual(by_kind(wl.transform(text, name=name), "COMBOBOX"),
                             [(100, 100, 300, 124), (100, 124, 300, 148), (100, 148, 300, 172)], name)


# alignment fixtures use 22 unit combos; keep the combo height step out of them
@mock.patch.object(wl, "COMBO_HEIGHT", 1000)
class ColumnGapTest(unittest.TestCase):
    def grid(self):
        cells = []
        for x0, x1 in ((100, 250), (254, 300), (302, 360), (364, 400)):
            cells += column("COMBOBOX", x0, x1, 112, 22, 24, 3, indent=2)
        return screen(
            *cells,
            window("STATICTEXT", (110, 86, 200, 110), indent=2),
            window("STATICTEXT", (254, 86, 300, 110), indent=2),
        )

    def test_gaps_even_out_keeping_the_outer_edges_and_headers_follow(self):
        out = wl.transform(self.grid(), name=MENU)
        combos = by_kind(out, "COMBOBOX")
        columns = [(100, 248), (252, 298), (302, 360), (364, 400)]
        for c, (x0, x1) in enumerate(columns):
            self.assertEqual([(r[0], r[2]) for r in combos[3 * c:3 * c + 3]], [(x0, x1)] * 3)
            self.assertEqual([(r[1], r[3]) for r in combos[3 * c:3 * c + 3]], [(112, 134), (136, 158), (160, 182)])
        self.assertEqual(by_kind(out, "STATICTEXT"), [(110, 86, 200, 110), (252, 86, 298, 110)])

    def test_all_different_gaps_become_four(self):
        text = screen(
            window("COMBOBOX", (100, 100, 200, 122), indent=2),
            window("COMBOBOX", (201, 100, 260, 122), indent=2),
            window("COMBOBOX", (263, 100, 300, 122), indent=2),
        )
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "COMBOBOX"),
                         [(100, 100, 196, 122), (200, 100, 259, 122), (263, 100, 300, 122)])

    def test_kept_and_in_game_layouts_keep_their_columns(self):
        for name in OUT_OF_SCOPE:
            out = wl.transform(self.grid(), name=name)
            self.assertEqual([(r[0], r[2]) for r in by_kind(out, "COMBOBOX")[::3]],
                             [(100, 250), (254, 300), (302, 360), (364, 400)], name)

    def test_grid_change_is_idempotent(self):
        once = wl.transform(self.grid(), name=MENU)
        self.assertNotEqual(once, self.grid())
        self.assertEqual(wl.transform(once, name=MENU), once)


class EntryPairTest(unittest.TestCase):
    def test_field_reaches_the_button_and_the_button_takes_the_field_row(self):
        text = screen(
            window("ENTRYFIELD", (100, 500, 300, 524), indent=2),
            window("PUSHBUTTON", (330, 501, 366, 524), indent=2),
        )
        out = wl.transform(text, name=MENU)
        (button,), (field,) = by_kind(out, "PUSHBUTTON"), by_kind(out, "ENTRYFIELD")
        self.assertEqual((button[0], button[2]), (330, 366))
        self.assertEqual((field[0], field[2]), (100, 330 - wl.DEFAULT_COLUMN_GAP))
        self.assertEqual((button[1], button[3]), (field[1], field[3]))

    def test_pair_under_a_list_takes_the_list_edges(self):
        text = screen(
            window("SCROLLLISTBOX", (98, 380, 402, 494), indent=2),
            window("ENTRYFIELD", (100, 500, 340, 524), indent=2),
            window("PUSHBUTTON", (364, 500, 400, 524), indent=2),
        )
        out = wl.transform(text, name=MENU)
        self.assertEqual(by_kind(out, "SCROLLLISTBOX"), [(98, 380, 402, 494)])
        self.assertEqual(by_kind(out, "PUSHBUTTON"), [(366, 500, 402, 524)])
        self.assertEqual(by_kind(out, "ENTRYFIELD"), [(98, 500, 366 - wl.DEFAULT_COLUMN_GAP, 524)])

    def test_pair_uses_the_grid_gap_of_its_parent(self):
        text = screen(
            window("COMBOBOX", (100, 100, 200, 122), indent=2),
            window("COMBOBOX", (202, 100, 300, 122), indent=2),
            window("COMBOBOX", (302, 100, 360, 122), indent=2),
            window("ENTRYFIELD", (100, 500, 300, 524), indent=2),
            window("PUSHBUTTON", (330, 500, 366, 524), indent=2),
        )
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "ENTRYFIELD"), [(100, 500, 328, 524)])

    def test_window_between_field_and_button_breaks_the_pair(self):
        text = screen(
            window("ENTRYFIELD", (100, 500, 300, 524), indent=2),
            window("STATICTEXT", (305, 500, 325, 524), indent=2),
            window("PUSHBUTTON", (330, 500, 366, 524), indent=2),
        )
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "ENTRYFIELD"), [(100, 500, 300, 524)])

    def test_move_that_would_overlap_is_skipped_and_logged(self):
        text = screen(
            window("SCROLLLISTBOX", (100, 380, 412, 494), indent=2),
            window("ENTRYFIELD", (100, 500, 340, 524), indent=2),
            window("PUSHBUTTON", (370, 500, 410, 524), indent=2),
            window("USER", (411, 520, 430, 530), indent=2),
        )
        notes = []
        out = wl.transform(text, notes, name=MENU)
        self.assertEqual(by_kind(out, "ENTRYFIELD"), [(100, 500, 340, 524)])
        self.assertEqual(by_kind(out, "PUSHBUTTON"), [(370, 500, 410, 524)])
        self.assertTrue(any(n.startswith("pairs: skipped") for n in notes), notes)

    def test_kept_and_in_game_layouts_keep_their_pairs(self):
        text = screen(
            window("ENTRYFIELD", (100, 500, 300, 524), indent=2),
            window("PUSHBUTTON", (330, 500, 366, 524), indent=2),
        )
        for name in (*OUT_OF_SCOPE, "Window/InGameChat.wnd"):
            self.assertEqual(by_kind(wl.transform(text, name=name), "ENTRYFIELD"), [(100, 500, 300, 524)], name)

    def test_pair_change_is_idempotent(self):
        text = screen(
            window("SCROLLLISTBOX", (98, 380, 402, 494), indent=2),
            window("ENTRYFIELD", (100, 500, 340, 524), indent=2),
            window("PUSHBUTTON", (364, 501, 400, 523), indent=2),
        )
        once = wl.transform(text, name=MENU)
        self.assertNotEqual(once, text)
        self.assertEqual(wl.transform(once, name=MENU), once)


MAP_SELECT_LAYOUTS = ("SkirmishMapSelectMenu.wnd", "LanMapSelectMenu.wnd", "WOLMapSelectMenu.wnd")
MAP_SELECT_NAMES = (
    "ListboxMap", "WinMapPreview", "ButtonOK", "ButtonBack", "WinGadgetBorder", "StaticTextTitle",
    "TextEntryMapSearch", "ComboBoxMapPlayers", "ComboBoxMapSize", "RadioButtonMapsAll", "RadioButtonMapsOfficial",
    "RadioButtonMapsUser", "RadioButtonMapsFavourites", "StaticTextMapCount", "StaticTextMapLegend",
    *(f"ButtonMapStartPosition{i}" for i in range(8)),
)


class DialogTest(unittest.TestCase):
    def test_content_over_a_dialog_frame_keeps_its_rects(self):
        # a frame sibling under the content (backdrop, frame, content) keeps every inset, even near misses
        text = screen(
            window("USER", (130, 105, 670, 495), indent=2),
            window("SCROLLLISTBOX", (140, 192, 470, 487), indent=2),
            window("USER", (482, 192, 660, 432), indent=2),
            window("PUSHBUTTON", (480, 461, 566, 487), indent=2),
            window("PUSHBUTTON", (574, 462, 660, 488), indent=2),
        )
        self.assertEqual(rects(wl.transform(text, name=MENU)), rects(text))

    def test_map_select_dialogs_pass_the_rules_unchanged(self):
        for fn in MAP_SELECT_LAYOUTS:
            name = f"Window/Menus/{fn}"
            text = (wl.SOURCES / name).read_bytes().decode("latin-1")
            out, info = wl.align(wl.remap_colors(text), name)
            self.assertEqual(info["changes"], [], fn)
            self.assertEqual(wl.structure_counts(out), wl.structure_counts(text), fn)

    def test_map_select_dialogs_keep_the_window_names_the_code_uses(self):
        for fn in MAP_SELECT_LAYOUTS:
            root = wl.parse_windows((wl.SOURCES / "Window" / "Menus" / fn).read_bytes().decode("latin-1"))
            names = {w.name for w in root.walk()}
            for short in MAP_SELECT_NAMES:
                self.assertIn(f"{fn}:{short}", names, fn)
            # the backdrop covers the screen, the dialog frame is centred and holds every other window
            backdrop = root.children[0]
            self.assertEqual(backdrop.rect, (0, 0, 800, 600), fn)
            frame = next(w for w in root.walk() if w.name == f"{fn}:WinGadgetBorder")
            self.assertEqual((frame.rect[0] + frame.rect[2], frame.rect[1] + frame.rect[3]), (800, 600), fn)
            for win in backdrop.walk():
                if win is not frame:
                    self.assertTrue(frame.rect[0] <= win.rect[0] and win.rect[2] <= frame.rect[2], win.name)
                    self.assertTrue(frame.rect[1] <= win.rect[1] and win.rect[3] <= frame.rect[3], win.name)

    def test_map_select_filters_sit_on_the_list_and_preview_grid(self):
        for fn in MAP_SELECT_LAYOUTS:
            root = wl.parse_windows((wl.SOURCES / "Window" / "Menus" / fn).read_bytes().decode("latin-1"))
            r = {w.name.split(":", 1)[1]: w.rect for w in root.walk()}
            lst, preview = r["ListboxMap"], r["WinMapPreview"]
            gap = preview[0] - lst[2]
            # search over the list column, the combos over the preview column like Accept and Cancel
            self.assertEqual((r["TextEntryMapSearch"][0], r["TextEntryMapSearch"][2]), (lst[0], lst[2]), fn)
            self.assertEqual((r["ComboBoxMapPlayers"][0], r["ComboBoxMapPlayers"][2]), (r["ButtonOK"][0], r["ButtonOK"][2]), fn)
            self.assertEqual((r["ComboBoxMapSize"][0], r["ComboBoxMapSize"][2]), (r["ButtonBack"][0], r["ButtonBack"][2]), fn)
            self.assertEqual(r["ComboBoxMapSize"][0] - r["ComboBoxMapPlayers"][2], gap, fn)
            # four equal source buttons evenly spaced across the list column, the count over the preview column
            sources = [r[f"RadioButtonMaps{n}"] for n in ("All", "Official", "User", "Favourites")]
            self.assertEqual(len({s[2] - s[0] for s in sources}), 1, fn)
            self.assertEqual({b[0] - a[2] for a, b in zip(sources, sources[1:])}, {gap}, fn)
            self.assertEqual((sources[0][0], sources[-1][2]), (lst[0], lst[2]), fn)
            self.assertEqual((r["StaticTextMapCount"][0], r["StaticTextMapCount"][2]), (preview[0], preview[2]), fn)
            self.assertEqual((r["StaticTextMapCount"][1], r["StaticTextMapCount"][3]), (sources[0][1], sources[0][3]), fn)


# alignment fixtures use 22 unit combos; keep the combo height step out of them
@mock.patch.object(wl, "COMBO_HEIGHT", 1000)
class AlignmentIdempotenceTest(unittest.TestCase):
    def test_title_gaps_and_columns_together_are_idempotent(self):
        cells = []
        for x0, x1 in ((48, 192), (195, 279), (282, 490), (492, 568)):
            cells += column("COMBOBOX", x0, x1, 112, 24, 24, 8)
        text = ruler_menu(
            window("STATICTEXT", (48, 43, 257, 79), indent=4, props=TITLE_PROPS),
            header_line(83, 83),
            window("STATICTEXT", (57, 88, 177, 112), indent=4),
            window("STATICTEXT", (492, 88, 568, 112), indent=4),
            *cells,
        )
        once = wl.transform(text, name=MENU)
        self.assertNotEqual(once, text)
        self.assertEqual(wl.transform(once, name=MENU), once)
        self.assertEqual(wl.structure_counts(once), wl.structure_counts(text))
        combos = by_kind(once, "COMBOBOX")
        self.assertEqual({r[3] - r[1] for r in combos}, {22})
        self.assertEqual({(r[1], r[3]) for r in combos[::8]}, {(112, 134)})
        self.assertEqual(by_kind(once, "STATICTEXT")[1:], [(57, 88, 177, 112), (492, 88, 568, 112)])

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


class HeaderTemplateTest(unittest.TestCase):
    def layout_with(self, header):
        return layout(window("STATICTEXT", (100, 100, 300, 130), props=[f'HEADERTEMPLATE = "{header}";']))

    def headers(self, text):
        return [m.group("header") for m in wl.HEADER_RE.finditer(text)]

    def test_menu_layouts_use_the_go_templates(self):
        self.assertEqual(self.headers(wl.transform(self.layout_with("Title"), name=MENU)), ["GOTitle"])

    def test_in_game_layouts_keep_the_retail_templates(self):
        for name in ("Window/ControlBar.wnd", "Window/Menus/QuitMenu.wnd", "Window/Menus/Defeat.wnd"):
            self.assertEqual(self.headers(wl.transform(self.layout_with("LabelRegular"), name=name)), ["LabelRegular"], name)

    def test_unknown_and_empty_templates_stay(self):
        self.assertEqual(self.headers(wl.transform(self.layout_with("[NONE]"), name=MENU)), ["[NONE]"])
        self.assertEqual(self.headers(wl.transform(self.layout_with("MyModTemplate"), name=MENU)), ["MyModTemplate"])

    def test_go_templates_apply_once(self):
        once = wl.transform(self.layout_with("Button"), name=MENU)
        self.assertEqual(self.headers(wl.transform(once, name=MENU)), ["GOButton"])

    def test_every_go_template_is_defined(self):
        ini = (Path(__file__).resolve().parent.parent / "GenOnline/GameData/Data/GeneralsOnline/HeaderTemplate.ini").read_text("latin-1")
        defined = {line.split()[1] for line in ini.splitlines() if line.startswith("HeaderTemplate ")}
        self.assertEqual(defined, {wl.GO_TEMPLATE_PREFIX + name for name in wl.RETAIL_TEMPLATES} | wl.GO_ONLY_TEMPLATES)


class HiliteImageTest(unittest.TestCase):
    def test_hovered_checked_box_uses_the_kit_hover_image(self):
        slot = "COLOR: 255 255 255 0, BORDERCOLOR: 106 123 255 255"
        draw = [f"IMAGE: NoImage, {slot}", f"IMAGE: Active-Unchecked, {slot}", f"IMAGE: Active-Checked, {slot}"]
        hilite = [f"IMAGE: NoImage, {slot}", f"IMAGE: Active-HiLighted, {slot}", f"IMAGE: Active-Checked, {slot}"]
        text = layout(window("CHECKBOX", (100, 100, 300, 124), draw=draw,
                             props=["HILITEDRAWDATA = " + ", ".join(hilite) + ";"]))
        out = wl.transform(text, name=MENU)
        self.assertIn("IMAGE: Active-HiLighted-Checked,", out)
        self.assertIn(f"IMAGE: Active-Checked, {slot}", out)  # the enabled checked image stays
        self.assertEqual(wl.transform(out, name=MENU), out)


class YamlSourceTest(unittest.TestCase):
    def test_build_uses_the_compiled_yaml_layout_as_final_output(self):
        import wnd_yaml
        options = "Window/Menus/OptionsMenu.wnd"
        other = "Window/Menus/Other.wnd"
        retail = {options.replace("Options", "options"): b"FILE_VERSION = 2;\r\n",
                  other: layout(window("STATICTEXT", (100, 100, 300, 130))).encode("latin-1")}
        with mock.patch.object(wl, "read_big", return_value=retail):
            results = {name: (text, out, info) for name, text, out, info in wl.build(Path("unused.big"))}
        # the compiled source with the combo heights and the kit colours of every menu (KitColorsTest)
        expected = wl.kit_colors(wl.compact_combos(wnd_yaml.compile_file(wnd_yaml.sources()[options]), options), options)
        names = {name.lower(): name for name in results}
        text, out, info = results[names[options.lower()]]
        self.assertEqual(out, expected)
        self.assertEqual(info["changes"], [])
        self.assertEqual(results[other][1], wl.transform(retail[other].decode("latin-1"), name=other))


class ShellCopyTest(unittest.TestCase):
    """The GO shell copies of layouts the match opens too: the same input as the base, built as a shell menu."""

    BASE = "Window/Menus/QuitMessageBox.wnd"
    SHELL = "Window/Menus/GOShellQuitMessageBox.wnd"

    def test_build_makes_the_shell_quit_box_from_the_quit_box_input_as_a_shell_menu(self):
        draw = ["IMAGE: NoImage, COLOR: 0 0 0 0, BORDERCOLOR: 255 255 255 255"] + [DRAW_SLOT] * 8
        text = layout(window("USER", (0, 0, 800, 600), [window("USER", (248, 100, 555, 432), indent=2, draw=draw)]))
        retail = {self.BASE: text.encode("latin-1")}
        with mock.patch.object(wl, "read_big", return_value=retail):
            results = {name: out for name, text, out, info in wl.build(Path("unused.big"))}
        self.assertEqual(results[self.SHELL], wl.transform(text, name=self.SHELL))
        self.assertEqual(results[self.BASE], wl.transform(text, name=self.BASE))
        self.assertNotEqual(results[self.SHELL], results[self.BASE])  # the shell copy takes the kit line frame
        self.assertTrue(wl.themed(self.SHELL))
        self.assertFalse(wl.themed(self.BASE))

    def test_shipped_shell_quit_box_holds_the_quit_box_windows_under_their_quit_box_names(self):
        base = (wl.DATA / self.BASE).read_bytes().decode("latin-1")
        shell = (wl.DATA / self.SHELL).read_bytes().decode("latin-1")

        def windows(text):
            return [(w.name, w.kind, w.rect, sorted(w.status)) for w in wl.parse_windows(text).walk()]

        self.assertEqual(windows(shell), windows(base))
        self.assertEqual(wl.structure_counts(shell), wl.structure_counts(base))
        # gogoMessageBox and QuitMessageBoxSystem look its windows up as QuitMessageBox.wnd:<name>
        named = [w.name for w in wl.parse_windows(shell).walk() if w.name]
        self.assertTrue(named and all(n.startswith("QuitMessageBox.wnd:") for n in named), named)
        self.assertEqual(shell, wl.kit_colors(wl.go_templates(base, self.SHELL), self.SHELL))
        self.assertIn('HEADERTEMPLATE = "GOMainButton"', shell)
        self.assertNotIn('HEADERTEMPLATE = "MainButton"', shell)

    def test_the_shell_opens_the_shell_quit_box_and_falls_back_to_the_quit_box(self):
        for game in ("GeneralsMD", "Generals"):
            source = (wl.ROOT / game / "Code/GameEngine/Source/GameClient/GUI/GameWindowManager.cpp").read_text("latin-1")
            body = source[source.index("Bool useLogo"):]
            body = body[:body.index("menuName")]
            shell = body.index('"Menus/GOShellQuitMessageBox.wnd"')
            self.assertLess(body.index("TheShell->isShellActive()"), shell, game)
            self.assertLess(shell, body.index('"Menus/QuitMessageBox.wnd"'), game)
        # the Retail style leaves the GO shell copy out, so it opens the retail quit box
        archive = (wl.ROOT / "Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp").read_text("latin-1")
        retail = archive[archive.index("static Bool isNotGenOnlineInterfaceFile"):]
        self.assertIn('"window/menus/goshellquitmessagebox.wnd"', retail[:retail.index("void ArchiveFileSystem::")])


PREVIEW_CALLBACK = 'DRAWCALLBACK = "W3DDrawMapPreview";'
BLACK_SLOT = "IMAGE: NoImage, COLOR: 0 0 0 255, BORDERCOLOR: 0 0 0 255"
PREVIEW_NAMES = ("MapWindow", "WinMapPreview")
FILLS = {token: [str(v) for v in wl.go_themes._rgb(hex_color)] for token, hex_color in wl.go_themes.RADAR_FILLS.items()}


def enabled_slots(text):
    """{window NAME: [(image, colour, border)] of ENABLEDDRAWDATA}."""
    return {w.name: slots for w in wl.parse_windows(text).walk() for prop, slots in w.draws if prop == "ENABLEDDRAWDATA"}


class MapPreviewBorderTest(unittest.TestCase):
    def preview_layout(self):
        draw = [BLACK_SLOT] * 9
        return layout(window("USER", (100, 100, 300, 200), draw=draw, props=[PREVIEW_CALLBACK]),
                      window("USER", (400, 100, 600, 200), draw=draw, props=['DRAWCALLBACK = "[None]";']))

    def test_map_preview_border_takes_the_kit_line_colour(self):
        for name in (MENU, "Window/Menus/MultiplayerLoadScreen.wnd", "Window/ControlBar.wnd"):
            slots = enabled_slots(wl.transform(self.preview_layout(), name=name))
            preview, other = slots["Test.wnd:USER100_100"], slots["Test.wnd:USER400_100"]
            self.assertEqual(preview[0], ("NoImage", ["0", "0", "0", "255"], ["47", "75", "255", "255"]), name)
            self.assertEqual(preview[1:], other[1:], name)  # only slot 0 changes
            self.assertEqual(other[0], ("NoImage", ["0", "0", "0", "255"], ["0", "0", "0", "255"]), name)

    def test_map_preview_border_is_idempotent_and_build_applies_it(self):
        once = wl.transform(self.preview_layout(), name=MENU)
        self.assertEqual(wl.transform(once, name=MENU), once)
        retail = {MENU: self.preview_layout().encode("latin-1")}
        with mock.patch.object(wl, "read_big", return_value=retail), mock.patch.object(wl, "SOURCES", Path("missing")):
            results = {name: out for name, text, out, info in wl.build(Path("unused.big"))}
        self.assertEqual(results[MENU], once)

    def test_shipped_map_previews_have_the_kit_line_and_the_radar_bronze(self):
        import go_themes
        found = {}
        for path in sorted((wl.DATA / "Window").rglob("*.wnd")):
            text = path.read_bytes().decode("latin-1")
            if "W3DDrawMapPreview" not in text:
                continue
            name = path.relative_to(wl.DATA).as_posix()
            found[name] = [slots[0][2] for win, slots in enabled_slots(text).items() if win.endswith(PREVIEW_NAMES)]
            self.assertEqual(text.count("W3DDrawMapPreview"), len(found[name]), name)
            theme = (go_themes.THEME_DIR / name).read_bytes().decode("latin-1")
            theme_borders = [slots[0][2] for win, slots in enabled_slots(theme).items() if win.endswith(PREVIEW_NAMES)]
            self.assertEqual(theme_borders, [["191", "144", "72", "255"]] * len(found[name]), name)
        self.assertEqual(sorted(found), sorted(wl.shipped_name(f"Window/Menus/{n}.wnd") for n in (
            "GameSpyGameOptionsMenu", "GameSpyLoadScreen", "LanGameOptionsMenu", "LanMapSelectMenu",
            "MultiplayerLoadScreen", "SkirmishGameOptionsMenu", "SkirmishMapSelectMenu", "WOLMapSelectMenu")))
        for name, borders in found.items():
            self.assertEqual(borders, [["47", "75", "255", "255"]], name)


def fill_slot(rgba):
    return [f"IMAGE: NoImage, COLOR: {rgba}, BORDERCOLOR: 47 75 255 255"] + [DRAW_SLOT] * 8


def first_fills(text):
    """{window NAME: {draw data property: first slot colour}}."""
    return {w.name: {prop: slots[0][1] for prop, slots in w.draws} for w in wl.parse_windows(text).walk()}


def in_game_names():
    return [p.relative_to(wl.DATA).as_posix() for p in sorted((wl.DATA / "Window").rglob("*.wnd"))
            if not wl.themed(p.relative_to(wl.DATA).as_posix())]


class WindowFillTest(unittest.TestCase):
    DIALOG = "Window/Menus/GOSignInDialog.wnd"  # a new menu layout: the rule applies without a name list

    def dialog(self):
        """A full screen dim backdrop around a dialog with nested panels, an opaque copy, a list and windows the step
        must leave: a clear static text, an image drawn window, a W3DNoDraw window and a navy fill."""
        hilite = "HILITEDRAWDATA = IMAGE: NoImage, COLOR: 2 2 2 162, BORDERCOLOR: 0 0 0 255;"
        disabled = "DISABLEDDRAWDATA = IMAGE: NoImage, COLOR: 64 64 64 255, BORDERCOLOR: 0 0 0 255;"
        deep = window("USER", (230, 130, 570, 190), indent=8, draw=fill_slot("0 0 0 200"))
        box = window("USER", (220, 120, 580, 200), [deep], indent=6, draw=fill_slot("0 0 2 255"))
        section = window("USER", (210, 110, 590, 300), [box], indent=4, draw=fill_slot("2 2 2 175"))
        dialog = window("USER", (200, 100, 600, 560), [
            section,
            window("USER", (210, 310, 590, 400), indent=4, draw=fill_slot("0 0 0 255")),
            window("SCROLLLISTBOX", (210, 410, 590, 480), indent=4, draw=fill_slot("0 0 0 126")),
            window("STATICTEXT", (210, 484, 300, 500), indent=4, draw=fill_slot("0 0 0 0")),
            window("USER", (310, 484, 400, 500), indent=4, draw=fill_slot("0 0 0 255"), props=["STATUS = ENABLED+IMAGE;"]),
            window("USER", (410, 484, 500, 500), indent=4, draw=fill_slot("0 0 0 255"), props=['DRAWCALLBACK = "W3DNoDraw";']),
            window("USER", (510, 484, 590, 500), indent=4, draw=fill_slot("0 0 128 255")),
        ], indent=2, draw=fill_slot("0 0 0 255"), props=[hilite, disabled])
        return layout(window("USER", (0, 0, 800, 600), [dialog], draw=fill_slot("2 2 2 126")))

    def test_radar_fills_take_the_fill_of_their_class_and_keep_alpha(self):
        out = first_fills(wl.theme_layout(self.dialog()))
        enabled = {name: fills["ENABLEDDRAWDATA"] for name, fills in out.items()}
        expected = {
            "USER0_0": FILLS["backdrop"] + ["126"],          # full screen dim backdrop
            "USER200_100": FILLS["panel"] + ["255"],         # the dialog body
            "USER210_110": FILLS["subpanel"] + ["175"],      # a panel inside it
            "USER220_120": FILLS["deep"] + ["255"],          # three deep
            "USER230_130": FILLS["deep"] + ["200"],          # deeper stays deep
            "USER210_310": FILLS["panel"] + ["255"],         # an opaque copy of the dialog fill reads as the dialog
            "SCROLLLISTBOX210_410": FILLS["field"] + ["126"],  # a field
            "STATICTEXT210_484": ["0", "0", "0", "0"],       # clear: left alone
            "USER310_484": ["0", "0", "0", "255"],           # image drawn
            "USER410_484": ["0", "0", "0", "255"],           # not drawn
            "USER510_484": FILLS["subpanel"] + ["255"],      # a retail navy panel is a panel too
        }
        self.assertEqual({name.split(":")[1]: fill for name, fill in enabled.items()}, expected)
        dialog = out["Test.wnd:USER200_100"]
        self.assertEqual(dialog["HILITEDRAWDATA"], FILLS["panel"] + ["162"])    # a black hover fill follows
        self.assertEqual(dialog["DISABLEDDRAWDATA"], ["64", "64", "64", "255"])  # the grey disabled fill stays

    def test_radar_fills_are_faint_slate_steps_over_near_black(self):
        rgbs = {token: wl.go_themes._rgb(hex_color) for token, hex_color in wl.go_themes.RADAR_FILLS.items()}
        self.assertEqual(set(rgbs), {"backdrop", "field", "panel", "subpanel", "deep"})
        layers = [rgbs[token] for token in ("field", "panel", "subpanel", "deep")]
        self.assertEqual(layers, sorted(layers))
        for token, (r, g, b) in rgbs.items():
            self.assertTrue(r <= g <= b, token)                     # slate: blue and green over red
            self.assertLessEqual(max(r, g, b), 0x20, token)         # near black
            self.assertGreater(max(r, g, b), wl.NEAR_BLACK, token)  # never retail black: the pass stays idempotent
            self.assertNotIn((r, g, b), wl.go_themes.LAYOUT_COLORS, token)  # the palette swap leaves them

    def test_only_fill_numbers_change(self):
        text = self.dialog()
        out = wl.window_fills(text)
        strip = lambda t: re.sub(r"COLOR: \d+ \d+ \d+", "COLOR:", t)  # noqa: E731
        self.assertEqual(strip(out), strip(text))
        self.assertEqual(out.count("\r\n"), text.count("\r\n"))

    def test_base_layouts_keep_their_retail_fills(self):
        for name in (self.DIALOG, MENU, "Window/ControlBar.wnd", "Window/InGameChat.wnd", "Window/Menus/QuitMenu.wnd"):
            out = wl.transform(self.dialog(), name=name)
            # menus give the retail grey disabled fill the kit disabled fill (KitColorsTest)
            disabled = ["10", "10", "16", "255"] if wl.themed(name) else ["64", "64", "64", "255"]
            self.assertEqual(sorted(first_fills(out)["Test.wnd:USER200_100"].items()),
                             [("DISABLEDDRAWDATA", disabled), ("ENABLEDDRAWDATA", ["0", "0", "0", "255"]),
                              ("HILITEDRAWDATA", ["2", "2", "2", "162"])], name)
            self.assertEqual(out.count("COLOR: 2 2 2 126"), 1, name)
            self.assertEqual(len(wl.fill_classes(out)), 8, name)

    def test_radar_fills_are_idempotent(self):
        once = wl.theme_layout(self.dialog())
        self.assertEqual(wl.theme_layout(once), once)
        self.assertEqual(wl.window_fills(once), once)
        self.assertEqual(wl.fill_classes(once), [])

    def test_shipped_menus_keep_their_retail_fills(self):
        login = first_fills((wl.DATA / wl.shipped_name("Window/Menus/GameSpyLoginProfile.wnd")).read_bytes().decode("latin-1"))
        self.assertEqual(login["GameSpyLoginProfile.wnd:GadgetParent"]["ENABLEDDRAWDATA"], ["0", "0", "0", "129"])
        options = first_fills((wl.DATA / "Window/Menus/OptionsMenu.wnd").read_bytes().decode("latin-1"))  # YAML built
        self.assertEqual(options["OptionsMenu.wnd:OptionsMenuParent"]["ENABLEDDRAWDATA"], ["0", "0", "0", "255"])
        self.assertEqual(options["OptionsMenu.wnd:OptionsMenuParentOld"]["ENABLEDDRAWDATA"], ["2", "2", "2", "175"])
        self.assertEqual(options["OptionsMenu.wnd:VideoParent"]["ENABLEDDRAWDATA"], ["0", "0", "0", "255"])
        lobby = first_fills((wl.DATA / "Window/Menus/LanLobbyMenu.wnd").read_bytes().decode("latin-1"))
        self.assertEqual(lobby["LanLobbyMenu.wnd:GadgetParent"]["ENABLEDDRAWDATA"], ["2", "2", "2", "190"])
        self.assertEqual(lobby["LanLobbyMenu.wnd:StaticTextGameInfo"]["ENABLEDDRAWDATA"], ["1", "0", "0", "126"])
        main = first_fills((wl.DATA / "Window/Menus/MainMenu.wnd").read_bytes().decode("latin-1"))
        self.assertEqual(main["MainMenu.wnd:MapBorder"]["ENABLEDDRAWDATA"], ["0", "0", "0", "126"])
        classed = 0
        for path in sorted((wl.DATA / "Window").rglob("*.wnd")):
            name = path.relative_to(wl.DATA).as_posix()
            if wl.themed(name):
                classed += len(wl.fill_classes(path.read_bytes().decode("latin-1")))
        self.assertGreater(classed, 150)  # the base keeps every retail black fill for the theme pass to classify

    def test_shipped_in_game_layouts_keep_their_fills_and_get_no_theme_copy(self):
        names = in_game_names()
        texts = {name: (wl.DATA / name).read_bytes().decode("latin-1") for name in names}
        self.assertTrue(any(wl.fill_classes(text) for text in texts.values()))
        for name in names:
            self.assertFalse((wl.go_themes.THEME_DIR / name).exists(), name)


class KitColorsTest(unittest.TestCase):
    """The kit colour fixes of the menu layouts: white frames, rare editor default slots, colour drawn buttons and
    disabled text."""

    IN_MATCH = "Window/Menus/QuitMessageBox.wnd"

    def slots(self, text, short):
        win = next(w for w in wl.parse_windows(text).walk() if w.name.endswith(":" + short))
        return {prop: [(color, border) for _, color, border in slots] for prop, slots in win.draws}

    def frames(self):
        dark = ["IMAGE: NoImage, COLOR: 0 0 0 0, BORDERCOLOR: 255 255 255 255"] + [DRAW_SLOT] * 8
        navy = ["IMAGE: NoImage, COLOR: 0 0 128 255, BORDERCOLOR: 254 254 254 200"] + [DRAW_SLOT] * 8
        return layout(window("USER", (0, 0, 800, 600), [
            window("USER", (40, 40, 760, 560), indent=2, draw=dark),
            window("USER", (100, 100, 200, 200), indent=2, draw=navy),
            window("USER", (300, 100, 400, 200), indent=2),  # the editor default white on white
            window("USER", (500, 100, 600, 200), indent=2, draw=dark, props=["STATUS = ENABLED+IMAGE;"]),
            window("USER", (600, 100, 700, 200), indent=2, draw=dark, props=['DRAWCALLBACK = "W3DMainMenuDraw";']),
            window("STATICTEXT", (100, 300, 200, 320), indent=2, draw=dark),
        ]))

    def test_white_frames_on_dark_or_clear_panels_take_the_kit_line_and_keep_alpha(self):
        out = wl.kit_colors(self.frames(), MENU)
        self.assertEqual(self.slots(out, "USER40_40")["ENABLEDDRAWDATA"][0], (["0", "0", "0", "0"], ["47", "75", "255", "255"]))
        self.assertEqual(self.slots(out, "USER100_100")["ENABLEDDRAWDATA"][0],
                         (["0", "0", "128", "255"], ["47", "75", "255", "200"]))
        for short in ("USER300_100", "USER500_100", "USER600_100", "STATICTEXT100_300"):
            self.assertEqual(self.slots(out, short), self.slots(self.frames(), short), short)

    def rare(self, kind):
        disabled = "DISABLEDDRAWDATA = IMAGE: NoImage, COLOR: 64 64 64 255, BORDERCOLOR: 192 192 192 255;"
        hilite = "HILITEDRAWDATA = IMAGE: NoImage, COLOR: 0 128 0 255, BORDERCOLOR: 128 255 128 255;"
        blue = "HILITEDRAWDATA = IMAGE: NoImage, COLOR: 0 0 255 200, BORDERCOLOR: 0 0 0 255;"
        return layout(window("USER", (0, 0, 800, 600), [
            window(kind, (100, 100, 200, 120), indent=2, props=[disabled, hilite]),
            window(kind, (100, 200, 200, 220), indent=2, props=[blue]),
        ]))

    def test_rare_disabled_and_hover_slots_of_panels_and_static_text_take_the_kit_colours(self):
        for kind in ("USER", "STATICTEXT"):
            out = wl.kit_colors(self.rare(kind), MENU)
            first = self.slots(out, f"{kind}100_100")
            self.assertEqual(first["DISABLEDDRAWDATA"][0], (["10", "10", "16", "255"], ["74", "78", "110", "255"]), kind)
            self.assertEqual(first["HILITEDRAWDATA"][0], (["21", "32", "120", "255"], ["106", "123", "255", "255"]), kind)
            self.assertEqual(self.slots(out, f"{kind}100_200")["HILITEDRAWDATA"][0],
                             (["21", "32", "120", "200"], ["106", "123", "255", "255"]), kind)
        # a progress bar draws its own disabled and hover slots
        self.assertEqual(wl.kit_colors(self.rare("PROGRESSBAR"), MENU), self.rare("PROGRESSBAR"))

    def toast(self, status="ENABLED"):
        enabled = ["IMAGE: NoImage, COLOR: 2 2 2 162, BORDERCOLOR: 47 75 255 255"] * 2 + [DRAW_SLOT] * 7
        disabled = ("DISABLEDDRAWDATA = IMAGE: NoImage, COLOR: 128 128 128 255, BORDERCOLOR: 192 192 192 255, "
                    "IMAGE: NoImage, COLOR: 192 192 192 255, BORDERCOLOR: 128 128 128 255;")
        hilite = ("HILITEDRAWDATA = IMAGE: NoImage, COLOR: 2 2 2 162, BORDERCOLOR: 148 112 1 255, "
                  "IMAGE: NoImage, COLOR: 52 54 55 162, BORDERCOLOR: 254 254 254 255;")
        return layout(window("PUSHBUTTON", (592, 10, 792, 59), draw=enabled,
                             props=[f"STATUS = {status};", disabled, hilite]))

    def test_colour_drawn_buttons_take_the_kit_button_colours_and_keep_alpha(self):
        out = self.slots(wl.kit_colors(self.toast(), MENU), "PUSHBUTTON592_10")
        self.assertEqual(out["ENABLEDDRAWDATA"][:2], [(["7", "10", "24", "162"], ["47", "75", "255", "255"]),
                                                      (["43", "52", "184", "162"], ["140", "155", "255", "255"])])
        self.assertEqual(out["HILITEDRAWDATA"], [(["21", "32", "120", "162"], ["106", "123", "255", "255"]),
                                                 (["43", "52", "184", "162"], ["140", "155", "255", "255"])])
        self.assertEqual(out["DISABLEDDRAWDATA"], [(["10", "10", "16", "255"], ["74", "78", "110", "255"])] * 2)
        # image drawn and clear buttons draw no colour slots
        self.assertEqual(wl.kit_colors(self.toast("ENABLED+IMAGE"), MENU), self.toast("ENABLED+IMAGE"))
        clear = layout(window("PUSHBUTTON", (0, 0, 100, 20), draw=["IMAGE: NoImage, COLOR: 255 255 255 0, BORDERCOLOR: 255 255 255 0"] * 9))
        self.assertEqual(wl.kit_colors(clear, MENU), clear)

    def disabled_text(self):
        def with_text(kind, x, rgb):
            text = window(kind, (x, 10, x + 90, 30), indent=2)
            return text.replace("DISABLED: 255 255 255 255", f"DISABLED: {rgb} 255")
        return layout(window("USER", (0, 0, 800, 600), [
            with_text("PUSHBUTTON", 0, "62 64 92"), with_text("COMBOBOX", 100, "122 48 1"),
            with_text("PUSHBUTTON", 200, "192 192 192"),
        ]))

    def test_dim_disabled_text_takes_the_kit_disabled_text_colour(self):
        text = self.disabled_text()
        out = wl.kit_colors(text, MENU)
        self.assertEqual(out.count("DISABLED: 138 143 176 255"), 2)
        self.assertIn("DISABLED: 192 192 192 255", out)
        self.assertNotIn("62 64 92", out)
        self.assertNotIn("122 48 1", out)
        self.assertEqual(out.count("DISABLEDBORDER: 49 55 168 128"), 4)  # text borders stay
        strip = lambda t: re.sub(r"DISABLED: \d+ \d+ \d+", "DISABLED:", t)  # noqa: E731
        self.assertEqual(strip(out), strip(text))

    def test_only_menus_change_and_the_pass_is_idempotent(self):
        for build in (self.frames, lambda: self.rare("USER"), self.toast, self.disabled_text):
            text = build()
            out = wl.kit_colors(text, MENU)
            self.assertNotEqual(out, text)
            self.assertEqual(wl.kit_colors(out, MENU), out)
            self.assertEqual(out.count("\r\n"), text.count("\r\n"))
            for name in ("Window/ControlBar.wnd", "Window/InGameChat.wnd", self.IN_MATCH, None):
                self.assertEqual(wl.kit_colors(text, name), text, name)
            self.assertEqual(wl.transform(text, name=MENU), wl.transform(out, name=MENU))  # transform applies it

    def test_radar_maps_every_kit_colour_the_pass_writes(self):
        for rgb in (wl.KIT_LINE, wl.KIT_HILITE, wl.KIT_DISABLED_LINE, wl.KIT_BUTTON_FILL, wl.KIT_HILITE_FILL,
                    wl.KIT_PUSHED_FILL, wl.KIT_PUSHED_LINE, wl.KIT_DISABLED_FILL, wl.KIT_DISABLED_TEXT):
            self.assertIn(rgb, wl.go_themes.LAYOUT_COLORS, rgb)


BIG_FONT = 'FONT = NAME: "Arial", SIZE: 14, BOLD: 0;'


class ComboHeightTest(unittest.TestCase):
    TALL = wl.COMBO_HEIGHT + 4

    def row(self, entry_height=None, combo_props=()):
        """A tall combo, an entry field and a button on one row, far enough apart for no pair or column rule."""
        h = self.TALL
        return screen(
            window("COMBOBOX", (100, 100, 300, 100 + h), indent=2, props=combo_props),
            window("ENTRYFIELD", (320, 100, 440, 100 + (entry_height or h)), indent=2),
            window("PUSHBUTTON", (500, 100, 600, 100 + h), indent=2),
        )

    def test_tall_combo_takes_the_combo_height_and_keeps_its_centre(self):
        out = wl.transform(self.row(), name=MENU)
        self.assertEqual(by_kind(out, "COMBOBOX"), [(100, 102, 300, 102 + wl.COMBO_HEIGHT)])

    def test_odd_difference_rounds_the_top_down(self):
        text = screen(window("COMBOBOX", (100, 100, 300, 100 + wl.COMBO_HEIGHT + 3), indent=2))
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "COMBOBOX"), [(100, 101, 300, 101 + wl.COMBO_HEIGHT)])

    def test_same_row_entry_and_button_follow(self):
        out = wl.transform(self.row(), name=MENU)
        self.assertEqual(by_kind(out, "ENTRYFIELD"), [(320, 102, 440, 102 + wl.COMBO_HEIGHT)])
        self.assertEqual(by_kind(out, "PUSHBUTTON"), [(500, 102, 600, 102 + wl.COMBO_HEIGHT)])

    def test_square_button_stays_square_on_its_left_edge_and_a_check_box_follows(self):
        h = self.TALL
        text = screen(window("COMBOBOX", (100, 100, 300, 100 + h), indent=2),
                      window("PUSHBUTTON", (400, 100, 400 + h, 100 + h), indent=2),
                      window("CHECKBOX", (500, 100, 640, 100 + h), indent=2))
        out = wl.transform(text, name=MENU)
        self.assertEqual(by_kind(out, "PUSHBUTTON"), [(400, 102, 400 + wl.COMBO_HEIGHT, 102 + wl.COMBO_HEIGHT)])
        self.assertEqual(by_kind(out, "CHECKBOX"), [(500, 102, 640, 102 + wl.COMBO_HEIGHT)])

    def test_button_of_another_row_or_font_stays(self):
        h = self.TALL
        text = screen(window("COMBOBOX", (100, 100, 300, 100 + h), indent=2),
                      window("PUSHBUTTON", (400, 100, 500, 100 + h + 6), indent=2),
                      window("PUSHBUTTON", (600, 100, 700, 100 + h), indent=2, props=[BIG_FONT]))
        out = wl.transform(text, name=MENU)
        self.assertEqual(by_kind(out, "PUSHBUTTON"), [(400, 100, 500, 100 + h + 6), (600, 100, 700, 100 + h)])

    def test_entry_of_another_height_stays(self):
        out = wl.transform(self.row(entry_height=self.TALL + 6), name=MENU)
        self.assertEqual(by_kind(out, "ENTRYFIELD"), [(320, 100, 440, 100 + self.TALL + 6)])

    def test_combo_with_a_larger_font_keeps_its_height(self):
        out = wl.transform(self.row(combo_props=[BIG_FONT]), name=MENU)
        self.assertEqual(by_kind(out, "COMBOBOX"), [(100, 100, 300, 100 + self.TALL)])
        self.assertEqual(by_kind(out, "ENTRYFIELD"), [(320, 100, 440, 100 + self.TALL)])

    def test_combo_at_or_below_the_height_stays(self):
        text = screen(window("COMBOBOX", (100, 100, 300, 100 + wl.COMBO_HEIGHT), indent=2),
                      window("COMBOBOX", (100, 200, 300, 198 + wl.COMBO_HEIGHT), indent=2))
        self.assertEqual(by_kind(wl.transform(text, name=MENU), "COMBOBOX"),
                         [(100, 100, 300, 100 + wl.COMBO_HEIGHT), (100, 200, 300, 198 + wl.COMBO_HEIGHT)])

    def test_in_game_kept_and_in_match_layouts_keep_their_combos(self):
        for name in (*OUT_OF_SCOPE, "Window/Menus/QuitMenu.wnd", None):
            out = wl.transform(self.row(), name=name)
            self.assertEqual(by_kind(out, "COMBOBOX"), [(100, 100, 300, 100 + self.TALL)], name)
            self.assertEqual(by_kind(out, "ENTRYFIELD"), [(320, 100, 440, 100 + self.TALL)], name)

    def test_combo_height_is_idempotent_and_build_applies_it(self):
        once = wl.transform(self.row(), name=MENU)
        self.assertEqual(wl.compact_combos(once, MENU), once)
        retail = {MENU: self.row().encode("latin-1")}
        with mock.patch.object(wl, "read_big", return_value=retail), mock.patch.object(wl, "SOURCES", Path("missing")):
            results = {name: out for name, text, out, info in wl.build(Path("unused.big"))}
        self.assertEqual(results[MENU], once)
        self.assertEqual(by_kind(results[MENU], "COMBOBOX"), [(100, 102, 300, 102 + wl.COMBO_HEIGHT)])

    def test_shipped_menu_combos_have_the_combo_height(self):
        import go_themes
        # documented exceptions: the WOL login combos draw a 14 pt font
        exceptions = {"WOLLoginMenu.wnd:ComboBoxLoginName", "WOLLoginMenu.wnd:ComboBoxServer"}
        seen = rows = 0
        for path in sorted((wl.DATA / "Window").rglob("*.wnd")):
            name = path.relative_to(wl.DATA).as_posix()
            if not wl.themed(name) or name.lower() in wl.GEOMETRY_KEPT:
                continue
            for base in (wl.DATA, go_themes.THEME_DIR):
                for win in wl.parse_windows((base / name).read_bytes().decode("latin-1")).walk():
                    if win.kind == "COMBOBOX" and win.name not in exceptions:
                        seen += 1
                        # TheSuperHackers @tweak arcticdolphin 09/11/2026 Only the setup screens' player rows are shorter.
                        row = wl.retail_name(name) in wl.SETUP_LAYOUTS and wl.SLOT_NAME_RE.match(win.name.split(":")[-1])
                        rows += bool(row)
                        height = 16 if row else wl.COMBO_HEIGHT
                        self.assertEqual(win.rect[3] - win.rect[1], height, f"{base / name} {win.name} {win.rect}")
        self.assertGreater(seen, 200)
        self.assertEqual(rows, 2 * (3 * 4 * wl.SLOT_ROWS - 1))


CLEAR_FRAMED_SLOT = "IMAGE: NoImage, COLOR: 2 2 2 0, BORDERCOLOR: 47 75 255 255"


class ButtonGroupBorderTest(unittest.TestCase):
    def group_layout(self, children, slot=CLEAR_FRAMED_SLOT, status="ENABLED"):
        draw = [slot] + [DRAW_SLOT] * 8
        return layout(window("USER", (100, 100, 400, 124), children, draw=draw, props=[f"STATUS = {status};"]))

    def buttons(self):
        overlay = ["STATUS = ENABLED+IMAGE+NOINPUT+NOFOCUS;"]
        return [window("PUSHBUTTON", (100, 100, 124, 124), indent=2),
                window("PUSHBUTTON", (130, 100, 250, 124), indent=2),
                window("USER", (136, 106, 152, 118), indent=2, props=overlay)]

    def group_border(self, text, name=MENU):
        return enabled_slots(wl.transform(text, name=name))["Test.wnd:USER100_100"][0][2]

    def test_clear_button_group_draws_no_border(self):
        for name in (MENU, "Window/ControlBar.wnd"):
            self.assertEqual(self.group_border(self.group_layout(self.buttons()), name), ["47", "75", "255", "0"], name)

    def test_button_group_rule_is_idempotent_and_build_applies_it(self):
        once = wl.transform(self.group_layout(self.buttons()), name=MENU)
        self.assertEqual(wl.transform(once, name=MENU), once)
        retail = {MENU: self.group_layout(self.buttons()).encode("latin-1")}
        with mock.patch.object(wl, "read_big", return_value=retail), mock.patch.object(wl, "SOURCES", Path("missing")):
            results = {name: out for name, text, out, info in wl.build(Path("unused.big"))}
        self.assertEqual(results[MENU], once)

    def test_other_frames_keep_their_border(self):
        field = [window("ENTRYFIELD", (130, 100, 250, 124), indent=2)]
        loose = [window("USER", (130, 100, 250, 124), indent=2)]
        cases = [
            ("filled", self.group_layout(self.buttons(), slot="IMAGE: NoImage, COLOR: 2 2 2 255, BORDERCOLOR: 47 75 255 255")),
            ("image", self.group_layout(self.buttons(), status="ENABLED+IMAGE")),
            ("entry field", self.group_layout(field)),
            ("input window", self.group_layout(loose)),
            ("no children", self.group_layout([])),
        ]
        for label, text in cases:
            self.assertEqual(self.group_border(text), ["47", "75", "255", "255"], label)



LIST_DATA = ["LISTBOXDATA = LENGTH: 100,", "COLUMNS: 4,", "COLUMNSWIDTH: 50,", "COLUMNSWIDTH: 0,", "COLUMNSWIDTH: 30,",
             "COLUMNSWIDTH: 20,", "FORCESELECT: 0;"]
NOINPUT = ["STATUS = ENABLED+IMAGE+NOINPUT+NOFOCUS;"]


class ListHeaderTest(unittest.TestCase):
    """Header cells over a four column list (50%, a hidden 0% column, 30%, 20%) in a USER frame: a label, two sorting
    push buttons and a corner cell."""

    CELLS = ("Test.wnd:STATICTEXT110_105", None, "Test.wnd:PUSHBUTTON200_105", "Test.wnd:PUSHBUTTON300_105")

    def headers(self, cells=CELLS):
        return {MENU: {"Test.wnd:SCROLLLISTBOX100_140": {
            "cells": cells, "corner": "Test.wnd:PUSHBUTTON400_105", "arrow": "Test.wnd:USER112_108"}}}

    def frame(self, list_rect=(100, 140, 500, 400), middle=True):
        cells = [window("STATICTEXT", (110, 105, 190, 130), indent=4),
                 window("USER", (112, 108, 120, 114), indent=4, props=NOINPUT)]
        if middle:
            cells.append(window("PUSHBUTTON", (200, 105, 290, 130), indent=4))
        cells += [window("PUSHBUTTON", (300, 105, 390, 130), indent=4),
                  window("PUSHBUTTON", (400, 105, 450, 130), indent=4, props=NOINPUT)]
        return screen(window("USER", (100, 100, 500, 400), [
            *cells,
            window("SCROLLLISTBOX", list_rect, indent=4, props=LIST_DATA),
        ], indent=2))

    def placed(self, text=None, headers=None):
        with mock.patch.object(wl, "LIST_HEADERS", headers or self.headers()):
            out = wl.transform(text or self.frame(), name=MENU)
        return out, {w.name.split(":")[-1]: w.rect for w in wl.parse_windows(out).walk()}

    def test_cells_line_up_with_the_list_columns_on_the_list_top(self):
        out, rects = self.placed()
        total = 400 - wl.LIST_SCROLL_COLUMN
        x1, x3 = 100 + 50 * total // 100, 100 + 50 * total // 100 + 30 * total // 100
        x4 = x3 + 20 * total // 100
        top = 140 - wl.COMBO_HEIGHT
        self.assertEqual(rects["STATICTEXT110_105"], (100, top, x1, 140))
        self.assertEqual(rects["PUSHBUTTON200_105"], (x1, top, x3, 140))
        self.assertEqual(rects["PUSHBUTTON300_105"], (x3, top, x4, 140))
        self.assertEqual(rects["PUSHBUTTON400_105"], (x4, top, 500, 140))
        self.assertEqual(rects["SCROLLLISTBOX100_140"], (100, 140, 500, 400))

    def test_arrow_keeps_its_size_at_the_right_end_of_the_first_sorting_cell(self):
        out, rects = self.placed()
        cell = rects["PUSHBUTTON200_105"]
        right = cell[2] - wl.HEADER_ARROW_INSET
        top = cell[1] + (wl.COMBO_HEIGHT - 6) // 2
        self.assertEqual(rects["USER112_108"], (right - 8, top, right, top + 6))

    def test_cells_follow_the_list(self):
        out, rects = self.placed(self.frame((100, 150, 500, 400)).replace("SCROLLLISTBOX100_150", "SCROLLLISTBOX100_140"))
        self.assertEqual(rects["STATICTEXT110_105"][1::2], (150 - wl.COMBO_HEIGHT, 150))

    def test_placement_is_idempotent(self):
        once, _ = self.placed()
        again, _ = self.placed(once)
        self.assertEqual(again, once)

    def test_a_visible_column_may_go_without_a_cell(self):
        cells = ("Test.wnd:STATICTEXT110_105", None, None, "Test.wnd:PUSHBUTTON300_105")
        out, rects = self.placed(self.frame(middle=False), self.headers(cells))
        total = 400 - wl.LIST_SCROLL_COLUMN
        x3 = 100 + 50 * total // 100 + 30 * total // 100
        self.assertEqual(rects["PUSHBUTTON300_105"][0::2], (x3, x3 + 20 * total // 100))
        self.assertEqual(rects["USER112_108"][2], rects["PUSHBUTTON300_105"][2] - wl.HEADER_ARROW_INSET)

    def test_a_hidden_column_with_a_cell_or_a_cell_count_mismatch_fails(self):
        for cells in (("Test.wnd:STATICTEXT110_105", "Test.wnd:PUSHBUTTON200_105", None, "Test.wnd:PUSHBUTTON300_105"),
                      ("Test.wnd:STATICTEXT110_105", None, "Test.wnd:PUSHBUTTON200_105")):
            with self.assertRaises(ValueError, msg=cells):
                self.placed(headers=self.headers(cells))


LOBBY = "Window/Menus/WOLCustomLobby.wnd"
# header window: its game list column; the lock, ranked and scroll bar columns have no header window
LOBBY_HEADERS = {"StaticTextHeaderGame": 0, "ButtonHeaderMap": 1, "ButtonHeaderPlayers": 3, "ButtonHeaderPing": 7}
LOBBY_SORTING = ("ButtonHeaderMap", "ButtonHeaderPlayers", "ButtonHeaderPing")
OLD_HEADER_CELLS = ("ButtonHeaderGame", "ButtonHeaderPassword", "ButtonHeaderStats", "ButtonHeaderCorner")
OLD_SORT_ROW = ("ParentSortButtons", "ButtonSortAlpha", "ButtonSortPing", "ButtonSortBuddies", "WindowSortAlpha",
                "WindowSortPing", "WindowSortBuddies")
DRAW_CALLBACK_RE = wl.re.compile(r'DRAWCALLBACK[ \t]*=[ \t]*"(?P<name>[^"]*)"')


def lobby_text(base=wl.DATA):
    return (base / wl.shipped_name(LOBBY)).read_bytes().decode("latin-1")


def lobby_windows(base=wl.DATA):
    """{short NAME: window} of the shipped custom lobby layout under base (the GO data or a theme folder)."""
    return {w.name.split(":")[-1]: w for w in wl.parse_windows(lobby_text(base)).walk()}


def lobby_bases():
    import go_themes
    return (wl.DATA, go_themes.THEME_DIR)


def game_columns(games):
    """[(x0, x1)] of the game list columns as the engine sizes them."""
    x0, _, x1, _ = games.rect
    total = x1 - x0 - wl.LIST_SCROLL_COLUMN
    edges, x = [], x0
    for pct in games.columns:
        edges.append((x, x + pct * total // 100))
        x += pct * total // 100
    return edges


class CustomLobbyHeaderTest(unittest.TestCase):
    def test_header_labels_line_up_with_their_game_list_columns(self):
        for base in lobby_bases():
            wins = lobby_windows(base)
            games = wins["ListboxGamesLarge"]
            columns = game_columns(games)
            for name, column in LOBBY_HEADERS.items():
                # the cell starts on its column; the engine insets the label like the list rows
                self.assertEqual(wins[name].rect, (columns[column][0], games.rect[1] - wl.COMBO_HEIGHT,
                                                   columns[column][1], games.rect[1]), (base, name))
            self.assertEqual(wins["StaticTextHeaderGame"].centered[0], 0, base)

    def test_only_map_players_and_ping_are_push_buttons(self):
        for base in lobby_bases():
            wins = lobby_windows(base)
            strip = wins["ParentGameListLarge"]
            buttons = sorted(w.name.split(":")[-1] for w in strip.children if w.kind == "PUSHBUTTON")
            self.assertEqual(buttons, sorted(LOBBY_SORTING), base)
            for name in LOBBY_SORTING:
                self.assertIn("ENABLED", wins[name].status, (base, name))
                self.assertNotIn("NOINPUT", wins[name].status, (base, name))
            game = wins["StaticTextHeaderGame"]
            self.assertEqual(game.kind, "STATICTEXT", base)
            self.assertIn("NOINPUT", game.status, base)
            self.assertIn("NOINPUT", wins["WindowHeaderArrow"].status, base)
            for name in OLD_HEADER_CELLS:
                self.assertNotIn(name, wins, (base, name))

    def test_header_cells_draw_no_box(self):
        for base in lobby_bases():
            wins = lobby_windows(base)
            for name in LOBBY_HEADERS:
                win = wins[name]
                self.assertNotIn("IMAGE", win.status, (base, name))
                self.assertTrue(win.draws, (base, name))
                for prop, slots in win.draws:
                    for image, color, border in slots:
                        self.assertEqual((image, color[3], border[3]), ("NoImage", "0", "0"), (base, name, prop))

    def test_one_line_between_header_and_list(self):
        for base in lobby_bases():
            text = lobby_text(base)
            wins = lobby_windows(base)
            games, strip = wins["ListboxGamesLarge"], wins["ParentGameListLarge"]
            # the list frame draws the only line: its visible enabled border
            self.assertNotEqual(dict(games.draws)["ENABLEDDRAWDATA"][0][2][3], "0", base)
            # the parent draws nothing and the header windows sit on the list top without reaching into it
            parent_block = text[strip.block[0]:strip.block[1]]
            self.assertEqual(DRAW_CALLBACK_RE.search(parent_block)["name"], "W3DNoDraw", base)
            top = games.rect[1] - wl.COMBO_HEIGHT
            self.assertEqual(strip.rect, (games.rect[0], top, games.rect[2], games.rect[3]), base)
            for win in strip.children:
                if win is games:
                    continue
                self.assertTrue(top <= win.rect[1] and win.rect[3] <= games.rect[1], (base, win.name))
                short = win.name.split(":")[-1]
                self.assertTrue(short in LOBBY_HEADERS or short == "WindowHeaderArrow", (base, short))

    def test_header_row_refresh_buddies_and_combo_share_the_combo_height(self):
        wins = lobby_windows()
        combo = wins["ComboBoxGroupRooms"].rect
        self.assertEqual(combo[3] - combo[1], wl.COMBO_HEIGHT)
        for name in ("ButtonRefresh", "CheckBoxBuddiesFirst"):
            self.assertEqual(wins[name].rect[1::2], combo[1::2], name)
        for name in LOBBY_HEADERS:
            rect = wins[name].rect
            self.assertEqual(rect[3] - rect[1], wl.COMBO_HEIGHT, name)
        refresh = wins["ButtonRefresh"].rect
        self.assertEqual(refresh[2] - refresh[0], wl.COMBO_HEIGHT)
        self.assertEqual(refresh[0], wins["ListboxGamesLarge"].rect[0])

    def test_old_sort_row_is_gone_from_the_shipped_layouts(self):
        for base in lobby_bases():
            wins = lobby_windows(base)
            for name in OLD_SORT_ROW:
                self.assertNotIn(name, wins, base)
            self.assertIn("ButtonRefresh", wins, base)

    def test_radar_copy_has_the_same_windows_rects_status_and_alphas(self):
        main, radar = (lobby_windows(base) for base in lobby_bases())
        self.assertEqual({name: (w.kind, w.rect, w.status) for name, w in radar.items()},
                         {name: (w.kind, w.rect, w.status) for name, w in main.items()})
        for name in LOBBY_HEADERS:
            alphas = [[(c[3], b[3]) for _, c, b in slots] for _, slots in main[name].draws]
            self.assertEqual([[(c[3], b[3]) for _, c, b in slots] for _, slots in radar[name].draws], alphas, name)


SETUP_MENUS = ("SkirmishGameOptionsMenu", "LanGameOptionsMenu", "GameSpyGameOptionsMenu")
# visible sibling overlaps the game setup layouts ship on purpose
SETUP_OVERLAPS = {
    # the host sees the player combo, the other players the static text in its spot; the code hides one
    ("ComboBoxPlayer", "StaticTextPlayer"),
    # retail slivers the rows never touch: the info list 7 units above its unnamed frame, 1 unit label and value
    # overlaps, and the map name reaching a few units into the select map button
    ("", "ListboxInfo"),
    ("SliderGameSpeed", "StaticTextBattleHonors1"),
    ("StaticTextBestStreak", "StaticTextBestStreakValue"),
    ("StaticTextLosses", "StaticTextLossesValue"),
    ("StaticTextStreak", "StaticTextStreakValue"),
    ("StaticTextWins", "StaticTextWinsValue"),
    ("ButtonSelectMap", "TextEntryMapDisplay"),
}
# the retail map name also reaches 3 to 5 units into the team column beside it, on the rows beside it (7 to 9 online)
MAP_NAME_TEAM_ROWS = range(5, 10)


def _short(win):
    return win.name.split(":")[-1]


def _slot(win):
    m = wl.SLOT_NAME_RE.match(_short(win))
    return (m["base"], int(m["index"])) if m else None


def _hidden(win):
    while win is not None:
        if "HIDDEN" in win.status:
            return True
        win = win.parent
    return False


class SetupRowsTest(unittest.TestCase):
    """The game setup screens lay out SLOT_ROWS player rows; the rows past the engine's 8 slots ship disabled."""

    def shipped(self, themes=True):
        import go_themes
        for menu in SETUP_MENUS:
            name = f"Window/Menus/{menu}.wnd"
            for base in (wl.DATA, go_themes.THEME_DIR) if themes else (wl.DATA,):
                path = base / wl.shipped_name(name)
                yield path, name, wl.parse_windows(path.read_bytes().decode("latin-1"))

    def slots(self, root):
        return {_slot(win): win for win in root.walk() if _slot(win)}

    def test_every_slot_control_has_twelve_rows(self):
        for path, name, root in self.shipped():
            slots = self.slots(root)
            bases = {base for base, index in slots if index == wl.ENGINE_SLOTS - 1}
            self.assertGreaterEqual(len(bases), 5, path)
            for base in bases:
                # skirmish row 0 has the local player's name field instead of ComboBoxPlayer0
                first = 1 if base == "ComboBoxPlayer" and "Skirmish" in name else 0
                self.assertEqual(sorted(i for b, i in slots if b == base), list(range(first, wl.SLOT_ROWS)), f"{path} {base}")

    def test_rows_past_the_engine_slots_are_disabled_and_hidden(self):
        # hidden until the engine has more slots; SHOW_EXTRA_ROWS shows their combos again
        self.assertFalse(wl.SHOW_EXTRA_ROWS)
        for path, name, root in self.shipped():
            slots = self.slots(root)
            for (base, index), win in slots.items():
                if index < wl.ENGINE_SLOTS:
                    self.assertIn("ENABLED", win.status, f"{path} {win.name}")
                    self.assertNotIn("HIDDEN", win.status, f"{path} {win.name}")
                    continue
                self.assertNotIn("ENABLED", win.status, f"{path} {win.name}")
                self.assertIn("HIDDEN", win.status, f"{path} {win.name}")

    def test_rows_have_one_pitch_and_centre_on_their_row(self):
        for path, name, root in self.shipped():
            slots = self.slots(root)
            anchors = [slots[("ComboBoxColor", i)] for i in range(wl.SLOT_ROWS)]
            frame = anchors[0].parent
            tops = [w.rect[1] for w in anchors]
            self.assertEqual([b - a for a, b in zip(tops, tops[1:])], [wl.ROW_PITCH] * (wl.SLOT_ROWS - 1), path)
            row_windows = [(i, w) for (b, i), w in slots.items() if w.parent is frame]
            row_windows += [(0, w) for w in frame.children if _short(w) in wl.ROW_STAND_INS]
            for index, win in row_windows:
                height = win.rect[3] - win.rect[1]
                self.assertEqual(win.rect[1] - tops[index], (wl.SETUP_COMBO_HEIGHT - height) // 2, f"{path} {win.name} {win.rect}")

    def test_columns_stay_aligned(self):
        for path, name, root in self.shipped():
            columns = {}
            for (base, index), win in self.slots(root).items():
                if base != "ButtonMapStartPosition":
                    columns.setdefault(base, set()).add((win.rect[0], win.rect[2]))
            for base, edges in columns.items():
                self.assertEqual(len(edges), 1, f"{path} {base} {edges}")

    def test_lower_sections_start_under_the_last_row(self):
        for path, name, root in self.shipped():
            slots = self.slots(root)
            last = slots[("ComboBoxColor", wl.SLOT_ROWS - 1)]
            frame = last.parent
            rows = {id(w) for w in slots.values()} | {id(w) for w in frame.children if _short(w) in wl.ROW_STAND_INS}
            below = [w for w in frame.children if id(w) not in rows and w.rect[1] > last.rect[1] and not _hidden(w)]
            self.assertTrue(below, path)
            self.assertEqual(min(w.rect[1] for w in below), last.rect[3] + wl.SECTION_GAP, path)

    def test_everything_sits_inside_the_ruler_frame(self):
        x0, y0, x1, y1 = wl.RULER_BOX
        for path, name, root in self.shipped():
            frame = self.slots(root)[("ComboBoxColor", 0)].parent
            for win in frame.walk():
                r = win.rect
                self.assertTrue(x0 <= r[0] and y0 <= r[1] and r[2] <= x1 and r[3] <= y1, f"{path} {win.name} {r}")

    def test_visible_siblings_do_not_overlap(self):
        for path, name, root in self.shipped():
            for parent in (root, *root.walk()):
                shown = [w for w in parent.children if w.rect and not _hidden(w)]
                for i, a in enumerate(shown):
                    for b in shown[i + 1:]:
                        if not wl._overlaps(a.rect, b.rect):
                            continue
                        sa, sb = _slot(a), _slot(b)
                        bases = tuple(sorted(s[0] if s else _short(w) for s, w in ((sa, a), (sb, b))))
                        same_row = sa is None or sb is None or sa[1] == sb[1]
                        rows = [s[1] for s in (sa, sb) if s]
                        map_name = bases == ("ComboBoxTeam", "TextEntryMapDisplay") and rows[0] in MAP_NAME_TEAM_ROWS
                        # start position markers are a stack the game code spreads over the map preview
                        markers = bases == ("ButtonMapStartPosition",) * 2
                        self.assertTrue((bases in SETUP_OVERLAPS and same_row) or map_name or markers,
                                        f"{path} {a.name} {a.rect} overlaps {b.name} {b.rect}")

    def test_setup_rows_is_idempotent_on_the_shipped_layouts(self):
        for path, name, root in self.shipped(themes=False):
            text = path.read_bytes().decode("latin-1")
            self.assertEqual(wl.setup_rows(text, name), text, path)

    def test_other_layouts_are_left_alone(self):
        text = layout(window("COMBOBOX", (100, 100, 200, 120)))
        self.assertEqual(wl.setup_rows(text, "Window/Menus/OptionsMenu.wnd"), text)
        self.assertEqual(wl.setup_rows(text, None), text)


# TheSuperHackers @tweak arcticdolphin 09/11/2026 Compact setup rows: 16 unit combos 18 apart, a taller chat and full drop-downs.
SETUP_ROW_HEIGHT = 16
SETUP_PITCH = 18
SETUP_TEXT_KINDS = ("COMBOBOX", "ENTRYFIELD", "STATICTEXT")
# the chat box (the skirmish info list, beside the stats panel), its retail height and its least growth
SETUP_PANELS = {
    "SkirmishGameOptionsMenu": ("ListboxInfo", 92, 1.4),
    "LanGameOptionsMenu": ("ListboxChatWindowLanGame", 57, 1.6),
    "GameSpyGameOptionsMenu": ("ListboxChatWindowGameSpyGameSetup", 62, 1.6),
}
# the options row between the player rows and the chat box
SETUP_OPTIONS = {
    "SkirmishGameOptionsMenu": ("SliderGameSpeed", "ComboBoxStartingCash", "CheckboxLimitSuperweapons"),
    "LanGameOptionsMenu": ("ComboBoxStartingCash", "CheckboxLimitSuperweapons"),
    "GameSpyGameOptionsMenu": ("CheckboxLimitSuperweapons", "StartingCashLabel", "ComboBoxStartingCash",
                               "CheckBoxUseStats", "CheckBoxLimitArmies"),
}
MAXDISPLAY_RE = re.compile(r"MAXDISPLAY:[ \t]*(\d+)")


def _template_points():
    ini = (wl.DATA / "Data/GeneralsOnline/HeaderTemplate.ini").read_text("latin-1")
    pattern = r"(?m)^HeaderTemplate (\w+)\r?\n(?:[ \t]+[^\r\n]*\r?\n)*?[ \t]+Point = (\d+)"
    return {m[1]: int(m[2]) for m in re.finditer(pattern, ini)}


class SetupRowSizeTest(unittest.TestCase):
    """The setup rows are 16 unit combos with a smaller font, the chat takes the freed height and the lists open full."""

    shipped = SetupRowsTest.shipped
    slots = SetupRowsTest.slots

    def row_windows(self, root):
        slots = self.slots(root)
        frame = slots[("ComboBoxColor", 0)].parent
        rows = [(i, w) for (b, i), w in slots.items() if w.parent is frame]
        return frame, rows + [(0, w) for w in frame.children if _short(w) in wl.ROW_STAND_INS]

    def named(self, frame):
        return {_short(w): w for w in frame.children}

    def test_row_combos_are_16_units_18_apart(self):
        for path, name, root in self.shipped():
            slots = self.slots(root)
            tops = [slots[("ComboBoxColor", i)].rect[1] for i in range(wl.SLOT_ROWS)]
            self.assertEqual([b - a for a, b in zip(tops, tops[1:])], [SETUP_PITCH] * (wl.SLOT_ROWS - 1), path)
            frame, rows = self.row_windows(root)
            for index, win in rows:
                height = win.rect[3] - win.rect[1]
                if win.kind in SETUP_TEXT_KINDS:
                    self.assertEqual(height, SETUP_ROW_HEIGHT, f"{path} {win.name} {win.rect}")
                self.assertLessEqual(height, SETUP_ROW_HEIGHT, f"{path} {win.name} {win.rect}")
                self.assertEqual(win.rect[1] - tops[index], (SETUP_ROW_HEIGHT - height) // 2, f"{path} {win.name}")

    def test_row_text_uses_a_smaller_go_template(self):
        points = _template_points()
        for path, name, root in self.shipped():
            frame, rows = self.row_windows(root)
            for index, win in rows:
                if win.kind not in SETUP_TEXT_KINDS:
                    continue
                self.assertTrue(win.header.startswith(wl.GO_TEMPLATE_PREFIX), f"{path} {win.name} {win.header}")
                self.assertLess(points.get(win.header, 99), points["GOComboBoxEntry"], f"{path} {win.name} {win.header}")
                self.assertEqual(win.font_size, points[win.header], f"{path} {win.name}")

    def test_rows_and_options_sit_above_the_chat_box(self):
        for path, name, root in self.shipped():
            menu = Path(name).stem
            frame, rows = self.row_windows(root)
            wins = self.named(frame)
            chat = wins[SETUP_PANELS[menu][0]]
            options = [wins[n] for n in SETUP_OPTIONS[menu]]
            last_row = max(w.rect[3] for i, w in rows)
            for win in options:
                self.assertGreaterEqual(win.rect[1], last_row, f"{path} {win.name} {win.rect}")
                self.assertLessEqual(win.rect[3], chat.rect[1], f"{path} {win.name} {win.rect} {chat.rect}")
            self.assertLess(last_row, chat.rect[1], path)

    def test_chat_box_takes_the_freed_height(self):
        for path, name, root in self.shipped():
            panel, old, growth = SETUP_PANELS[Path(name).stem]
            chat = self.named(self.row_windows(root)[0])[panel]
            self.assertGreaterEqual(chat.rect[3] - chat.rect[1], growth * old, f"{path} {chat.rect}")

    def test_drop_downs_open_without_scrolling_and_fit_the_screen(self):
        # least entries each list needs: Random, 12 armies and Observer; ??? and 8 colours; no team and 4 teams
        need = {"ComboBoxPlayerTemplate": 14, "ComboBoxColor": 9, "ComboBoxTeam": 5}
        for path, name, root in self.shipped():
            text = path.read_bytes().decode("latin-1")
            for win in root.walk():
                if win.kind != "COMBOBOX":
                    continue
                shown = int(MAXDISPLAY_RE.search(text, *win.block)[1])
                slot = _slot(win)
                self.assertGreaterEqual(shown, need.get(slot[0], 5) if slot else 5, f"{path} {win.name}")
                # a drop-down row is at most two units per font point from 800x600 to 1080p
                self.assertLessEqual(win.rect[3] + shown * 2 * win.font_size, wl.SCREEN[3], f"{path} {win.name}")


def status(flags):
    return [f"STATUS = {flags};"]


def hidden_names(text):
    """{short NAME: True when the window or an ancestor is hidden}."""
    return {w.name.split(":")[-1]: _hidden(w) for w in wl.parse_windows(text).walk()}


class BlankLayoutTest(unittest.TestCase):
    def form(self):
        title = status("ENABLED") + ['FONT = NAME: "Arial", SIZE: 20, BOLD: 0;']
        return layout(window("USER", (0, 0, 800, 600), [
            window("USER", (100, 100, 700, 500), [
                window("STATICTEXT", (110, 110, 400, 150), indent=4, props=title),
                window("USER", (100, 150, 700, 151), indent=4, props=status("ENABLED")),
                window("PUSHBUTTON", (120, 400, 220, 430), indent=4, props=status("ENABLED+IMAGE")),
                window("ENTRYFIELD", (120, 300, 300, 324), indent=4, props=status("ENABLED+IMAGE")),
            ], indent=2, props=status("ENABLED")),
            window("USER", (100, 520, 300, 560), [
                window("STATICTEXT", (110, 525, 290, 545), indent=4, props=status("ENABLED")),
            ], indent=2, props=status("ENABLED")),
        ], props=status("ENABLED+IMAGE")))

    def blanked(self, text=None):
        with mock.patch.object(wl, "BLANK_LAYOUTS", {MENU: ("Test.wnd:USER100_150",)}):
            return wl.transform(text or self.form(), name=MENU)

    def test_kept_windows_their_ancestors_and_the_title_stay_and_the_rest_hides(self):
        out = self.blanked()
        self.assertEqual(hidden_names(out), {
            "USER0_0": False, "USER100_100": False, "STATICTEXT110_110": False, "USER100_150": False,
            "PUSHBUTTON120_400": True, "ENTRYFIELD120_300": True, "USER100_520": True, "STATICTEXT110_525": True})
        statuses = {w.name.split(":")[-1]: w.status for w in wl.parse_windows(out).walk()}
        self.assertEqual(statuses["PUSHBUTTON120_400"], {"ENABLED", "HIDDEN", "IMAGE"})
        self.assertEqual(statuses["STATICTEXT110_525"], {"ENABLED"})
        self.assertEqual(wl.structure_counts(out), wl.structure_counts(self.form()))

    def test_blanking_is_idempotent_and_other_layouts_are_left_alone(self):
        once = self.blanked()
        self.assertEqual(self.blanked(once), once)
        self.assertEqual(wl.blank_layout(self.form(), "Window/Menus/OptionsMenu.wnd"), self.form())

    def test_unknown_kept_name_fails(self):
        with mock.patch.object(wl, "BLANK_LAYOUTS", {MENU: ("Test.wnd:Missing",)}):
            with self.assertRaises(ValueError):
                wl.blank_layout(self.form(), MENU)


LOGIN_FORMS = {
    "GameSpyLoginProfile": ("ButtonLogin", "ButtonCreateAccount", "ButtonTOS", "ButtonBack", "CheckBoxRememberInfo",
                            "TextEntryPassword", "ComboBoxEmail", "ComboBoxLoginName", "TextEntryMonth", "TextEntryDay",
                            "TextEntryYear", "StaticTextEmail", "StaticTextLoginName", "StaticTextPassword", "ParentESRB",
                            "ParentTOS", "ListboxTOS", "ButtonTOSOK"),
    "GameSpyLoginQuick": ("ButtonLogin", "ButtonCreateAccount", "ButtonUseAccount", "ButtonTOS", "ButtonBack",
                          "TextEntryLoginName", "ParentTOS", "ListboxTOS", "ButtonTOSOK", "MainMenuBackground"),
}
LOGIN_SHOWN = {
    "GameSpyLoginProfile": ("WOLLoginMenuParent", "GadgetParent", "StaticTextTitle", "Line"),
    "GameSpyLoginQuick": ("WOLLoginMenuParent",),  # the W3DMainMenuDraw strip with its brown lines hides too
}


class ShippedLoginLayoutTest(unittest.TestCase):
    def test_login_forms_are_hidden_behind_the_sign_in_box_and_the_backdrop_and_title_stay(self):
        import go_themes
        for menu in LOGIN_FORMS:
            for base in (wl.DATA, go_themes.THEME_DIR):
                path = base / wl.shipped_name(f"Window/Menus/{menu}.wnd")
                root = wl.parse_windows(path.read_bytes().decode("latin-1"))
                hidden = {w.name.split(":")[-1]: _hidden(w) for w in root.walk() if w.name.split(":")[-1]}
                for name in LOGIN_FORMS[menu]:
                    self.assertTrue(hidden[name], f"{path} {name}")
                for name in LOGIN_SHOWN[menu]:
                    self.assertFalse(hidden[name], f"{path} {name}")
                controls = [w for w in root.walk() if w.kind in ("PUSHBUTTON", "CHECKBOX", "ENTRYFIELD", "COMBOBOX",
                                                                  "SCROLLLISTBOX", "RADIOBUTTON")
                            and _short(w) not in SIGN_IN_WINDOWS.get(menu, {})]
                self.assertTrue(controls and all(_hidden(w) for w in controls), path)
                titles = [w for w in root.walk() if wl._is_title(w) and not _hidden(w)]
                self.assertEqual(len(titles), 1, path)


SIGN_IN_PROVIDERS = ("ButtonProviderDiscord", "ButtonProviderSteam", "ButtonProviderGameReplays")
# GO sign-in screen windows on the login profile layout, looked up by WOLLoginMenu.cpp: {short NAME: hidden by default}
SIGN_IN_WINDOWS = {"GameSpyLoginProfile": {
    "StaticTextTitle": False, "StaticTextSignInStatus": False,
    **dict.fromkeys(SIGN_IN_PROVIDERS, False), "ButtonUseAnotherAccount": False, "ButtonCancel": False}}


def login_profile(base=wl.DATA):
    """(root, {short NAME: window}) of the shipped GameSpyLoginProfile layout under base."""
    root = wl.parse_windows((base / wl.shipped_name("Window/Menus/GameSpyLoginProfile.wnd")).read_bytes().decode("latin-1"))
    return root, {_short(w): w for w in root.walk() if w.name}


class ShippedSignInScreenTest(unittest.TestCase):
    def bases(self):
        import go_themes
        return (wl.DATA, go_themes.THEME_DIR)

    def test_sign_in_windows_exist_with_their_default_visibility(self):
        for base in self.bases():
            _, wins = login_profile(base)
            for name, hidden in SIGN_IN_WINDOWS["GameSpyLoginProfile"].items():
                self.assertIn(name, wins, base)
                self.assertEqual(_hidden(wins[name]), hidden, f"{base} {name}")

    def test_kinds_and_kit_templates(self):
        _, wins = login_profile()
        for name in (*SIGN_IN_PROVIDERS, "ButtonUseAnotherAccount", "ButtonCancel"):
            self.assertEqual((wins[name].kind, wins[name].header), ("PUSHBUTTON", "GOMainButton"), name)
        self.assertEqual((wins["StaticTextSignInStatus"].kind, wins["StaticTextSignInStatus"].header),
                         ("STATICTEXT", "GOMinorTitle"))

    def test_no_sign_in_code_window(self):
        # the sign-in code only goes into the provider URL; no window shows it or the login page link
        for base in self.bases():
            _, wins = login_profile(base)
            self.assertNotIn("StaticTextSignInCode", wins, base)

    def test_sign_in_windows_sit_inside_the_ruler_frame_and_their_parent(self):
        for base in self.bases():
            _, wins = login_profile(base)
            self.assertEqual(wins["GadgetParent"].rect, wl.RULER_BOX, base)
            for name in SIGN_IN_WINDOWS["GameSpyLoginProfile"]:
                win = wins[name]
                self.assertTrue(wl._within(win.rect, wl.RULER_BOX, 0), f"{base} {name} {win.rect}")
                self.assertTrue(wl._within(win.rect, win.parent.rect, 0), f"{base} {name} {win.rect} {win.parent.rect}")

    def test_provider_buttons_stack_full_width_in_order(self):
        _, wins = login_profile()
        status = wins["StaticTextSignInStatus"].rect
        column = [wins[name].rect for name in SIGN_IN_PROVIDERS]
        for rect in column:
            self.assertEqual((rect[0], rect[2]), (status[0], status[2]), rect)
        self.assertEqual({r[3] - r[1] for r in column}, {column[0][3] - column[0][1]})
        gaps = {b[1] - a[3] for a, b in zip(column, column[1:])}
        self.assertEqual(len(gaps), 1, column)
        self.assertGreaterEqual(gaps.pop(), wl.MIN_GAP)

    def test_rows_stack_title_status_providers_and_buttons(self):
        _, wins = login_profile()
        title, status = wins["StaticTextTitle"].rect, wins["StaticTextSignInStatus"].rect
        first, last = wins[SIGN_IN_PROVIDERS[0]].rect, wins[SIGN_IN_PROVIDERS[-1]].rect
        other, cancel = wins["ButtonUseAnotherAccount"].rect, wins["ButtonCancel"].rect
        self.assertLessEqual(title[3], status[1])
        self.assertLess(status[3], first[1])
        self.assertLess(last[3], other[1])
        self.assertEqual(other[1::2], cancel[1::2])
        self.assertEqual((other[0], cancel[2]), (status[0], status[2]))

    def test_radar_copy_matches_the_geometry_and_visibility(self):
        import go_themes
        base, _ = login_profile()
        theme, _ = login_profile(go_themes.THEME_DIR)
        shape = [(w.name, w.kind, w.rect, w.status, w.header) for w in base.walk()]
        self.assertEqual([(w.name, w.kind, w.rect, w.status, w.header) for w in theme.walk()], shape)


DIALOG = "GOSignInDialog.wnd"
# GO sign-in dialog windows looked up by WOLLoginMenu.cpp, {short NAME: kind}, in drawing order under the panel
DIALOG_WINDOWS = {
    "StaticTextTitle": "STATICTEXT", "StaticTextSignInStatus": "STATICTEXT", "StaticTextMessage": "STATICTEXT",
    "ProgressBarDownload": "PROGRESSBAR", "StaticTextDownloadSize": "STATICTEXT",
    **dict.fromkeys(SIGN_IN_PROVIDERS, "PUSHBUTTON"),
    "ButtonUseAnotherAccount": "PUSHBUTTON", "ButtonUpdate": "PUSHBUTTON", "ButtonCancel": "PUSHBUTTON", "Header": "USER"}
DIALOG_ROW = ("ButtonUseAnotherAccount", "ButtonUpdate", "ButtonCancel")
DIALOG_DOWNLOAD = ("ProgressBarDownload", "StaticTextDownloadSize")
# content windows each dialog step of WOLLoginMenu.cpp shows (title and header always show)
DIALOG_STEPS = {
    "checking": ("StaticTextSignInStatus", "StaticTextMessage", "ButtonCancel"),
    "update": ("StaticTextSignInStatus", "StaticTextMessage", "ButtonUpdate", "ButtonCancel"),
    "error": ("StaticTextSignInStatus", "StaticTextMessage", "ButtonCancel"),
    "sign in": ("StaticTextSignInStatus", "StaticTextMessage", *SIGN_IN_PROVIDERS, "ButtonUseAnotherAccount", "ButtonCancel"),
    "downloading": ("StaticTextSignInStatus", *DIALOG_DOWNLOAD, "ButtonCancel"),
    "ready": ("StaticTextSignInStatus", "StaticTextMessage", "ButtonUpdate", "ButtonCancel"),
}


def sign_in_dialog(base=wl.DATA):
    """(root, {short NAME: window}) of the shipped GO sign-in dialog layout under base."""
    root = wl.parse_windows((base / "Window/Menus" / DIALOG).read_bytes().decode("latin-1"))
    return root, {_short(w): w for w in root.walk() if w.name}


class ShippedSignInDialogTest(unittest.TestCase):
    def bases(self):
        import go_themes
        return (wl.DATA, go_themes.THEME_DIR)

    def test_every_window_exists_under_the_panel_with_its_kind(self):
        for base in self.bases():
            root, wins = sign_in_dialog(base)
            self.assertEqual([_short(w) for w in root.children], ["GOSignInDialogRoot"], base)
            self.assertEqual([_short(w) for w in wins["GOSignInDialogRoot"].children], ["GOSignInDialogParent"], base)
            panel = wins["GOSignInDialogParent"]
            self.assertEqual({_short(w): w.kind for w in panel.children}, DIALOG_WINDOWS, base)
            self.assertEqual([_short(w) for w in panel.children], list(DIALOG_WINDOWS), base)
            self.assertFalse(any(_hidden(w) for w in root.walk()), base)

    def test_the_code_looks_up_only_windows_the_layout_has(self):
        code = (wl.ROOT / "Core/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/GenOnlineLoginMenu.cpp").read_text(
            encoding="latin-1")
        names = set(re.findall(r'"GOSignInDialog\.wnd:(\w+)"', code))
        _, wins = sign_in_dialog()
        self.assertTrue(names, "GenOnlineLoginMenu.cpp looks up no dialog window")
        self.assertEqual(names - set(wins), set())

    def test_backdrop_covers_the_screen_and_the_panel_is_centred(self):
        for base in self.bases():
            _, wins = sign_in_dialog(base)
            self.assertEqual(wins["GOSignInDialogRoot"].rect, wl.SCREEN, base)
            x0, y0, x1, y1 = wins["GOSignInDialogParent"].rect
            self.assertEqual((x0 + x1, y0 + y1), (800, 600), base)

    def test_the_panel_is_a_narrow_card_and_the_buttons_keep_its_inset(self):
        # a login card, not the 546 unit wide provider buttons of the full-screen sign-in
        for base in self.bases():
            _, wins = sign_in_dialog(base)
            x0, _, x1, _ = wins["GOSignInDialogParent"].rect
            self.assertTrue(300 <= x1 - x0 <= 340, f"{base} panel width {x1 - x0}")
            status = wins["StaticTextSignInStatus"].rect
            inset = status[0] - x0
            self.assertEqual(x1 - status[2], inset, base)
            self.assertTrue(8 <= inset <= 20, f"{base} inset {inset}")
            for name in (*SIGN_IN_PROVIDERS, "StaticTextMessage"):
                rect = wins[name].rect
                self.assertEqual((rect[0], rect[2]), (x0 + inset, x1 - inset), f"{base} {name}")
                self.assertTrue(260 <= rect[2] - rect[0] <= 300, f"{base} {name} width {rect[2] - rect[0]}")
            row = [wins[name].rect for name in DIALOG_ROW]
            self.assertTrue(all(x0 + inset <= r[0] and r[2] <= x1 - inset for r in row), f"{base} {row}")
            update, cancel = wins["ButtonUpdate"].rect, wins["ButtonCancel"].rect
            self.assertEqual(update[2] - update[0], cancel[2] - cancel[0], base)
            self.assertEqual(cancel[2], x1 - inset, base)

    def test_everything_sits_inside_the_panel(self):
        for base in self.bases():
            _, wins = sign_in_dialog(base)
            panel = wins["GOSignInDialogParent"]
            for win in panel.walk():
                self.assertTrue(wl._within(win.rect, panel.rect, 0), f"{base} {win.name} {win.rect}")

    def test_no_windows_of_one_step_overlap_but_the_header_frame_over_the_title(self):
        for base in self.bases():
            _, wins = sign_in_dialog(base)
            content = {_short(w) for w in wins["GOSignInDialogParent"].children} - {"Header", "StaticTextTitle"}
            self.assertEqual(set().union(*DIALOG_STEPS.values()), content, base)
            for step, names in DIALOG_STEPS.items():
                shown = ["StaticTextTitle", *names]
                for i, a in enumerate(shown):
                    for b in shown[i + 1:]:
                        self.assertFalse(wl._overlaps(wins[a].rect, wins[b].rect), f"{base} {step} {a} {b}")
            self.assertTrue(wl._within(wins["StaticTextTitle"].rect, wins["Header"].rect, 0), base)

    def test_the_download_progress_shares_the_message_area(self):
        for base in self.bases():
            _, wins = sign_in_dialog(base)
            message = wins["StaticTextMessage"].rect
            bar, size = (wins[name].rect for name in DIALOG_DOWNLOAD)
            for rect in (bar, size):
                self.assertTrue(wl._within(rect, message, 0), f"{base} {rect} {message}")
                self.assertEqual((rect[0], rect[2]), (message[0], message[2]), f"{base} {rect}")
            self.assertLess(bar[3], size[1], base)
            self.assertGreaterEqual(size[1] - bar[3], wl.MIN_GAP, base)

    def test_the_download_bar_uses_the_load_screen_bar_images(self):
        load = wl.parse_windows((wl.DATA / "Window/Menus/SinglePlayerLoadScreen.wnd").read_bytes().decode("latin-1"))
        shape = [(prop, [image for image, _, _ in entries]) for prop, entries in
                 next(w for w in load.walk() if _short(w) == "ProgressLoad").draws]
        for base in self.bases():
            _, wins = sign_in_dialog(base)
            bar = wins["ProgressBarDownload"]
            self.assertEqual([(prop, [image for image, _, _ in entries]) for prop, entries in bar.draws], shape, base)
            self.assertIn("LoadingBar_Progress", shape[0][1])
            self.assertEqual(bar.header, "[NONE]", base)

    def test_rows_stack_status_message_providers_and_the_button_row(self):
        _, wins = sign_in_dialog()
        status, message = wins["StaticTextSignInStatus"].rect, wins["StaticTextMessage"].rect
        column = [wins[name].rect for name in SIGN_IN_PROVIDERS]
        row = [wins[name].rect for name in DIALOG_ROW]
        self.assertLessEqual(wins["Header"].rect[3], status[1])
        self.assertLessEqual(status[3], message[1])
        self.assertLess(message[3], column[0][1])
        self.assertLess(column[-1][3], row[0][1])
        for rect in (message, *column):
            self.assertEqual((rect[0], rect[2]), (status[0], status[2]), rect)
        self.assertEqual({r[3] - r[1] for r in column}, {column[0][3] - column[0][1]})
        gaps = {b[1] - a[3] for a, b in zip(column, column[1:])}
        self.assertEqual(len(gaps), 1, column)
        self.assertGreaterEqual(gaps.pop(), wl.MIN_GAP)
        self.assertEqual({(r[1], r[3]) for r in row}, {(row[0][1], row[0][3])})
        self.assertEqual((row[0][0], row[-1][2]), (status[0], status[2]))
        self.assertTrue(all(a[2] < b[0] for a, b in zip(row, row[1:])), row)

    def test_kinds_use_the_kit_templates(self):
        _, wins = sign_in_dialog()
        for name in (*SIGN_IN_PROVIDERS, *DIALOG_ROW):
            self.assertEqual(wins[name].header, "GOMainButton", name)
        self.assertEqual(wins["StaticTextSignInStatus"].header, "GOMinorTitle")

    def test_the_source_passes_the_rules_unchanged(self):
        name = f"Window/Menus/{DIALOG}"
        text = (wl.SOURCES / name).read_bytes().decode("latin-1")
        out, info = wl.align(wl.remap_colors(text), name)
        self.assertEqual(info["changes"], [])
        self.assertEqual(wl.structure_counts(out), wl.structure_counts(text))

    def test_radar_copy_matches_the_geometry_and_visibility(self):
        import go_themes
        base, _ = sign_in_dialog()
        theme, _ = sign_in_dialog(go_themes.THEME_DIR)
        shape = [(w.name, w.kind, w.rect, w.status, w.header) for w in base.walk()]
        self.assertEqual([(w.name, w.kind, w.rect, w.status, w.header) for w in theme.walk()], shape)


class ShippedLayoutTest(unittest.TestCase):
    def test_game_setup_column_headings_start_at_their_column(self):
        cases = [
            ("SkirmishGameOptionsMenu", "StaticTextPlayers", "TextEntryPlayerName"),
            ("SkirmishGameOptionsMenu", "StaticTextMapPreview", "MapWindow"),
            ("LanGameOptionsMenu", "StaticTextFaction", "ComboBoxPlayerTemplate0"),
            ("GameSpyGameOptionsMenu", "StaticTextMapPreview", "MapWindow"),
        ]
        for menu, heading, column in cases:
            path = wl.DATA / wl.shipped_name(f"Window/Menus/{menu}.wnd")
            rects = {w.name.split(":")[-1]: w.rect for w in wl.parse_windows(path.read_bytes().decode("latin-1")).walk()}
            self.assertEqual(rects[heading][0], rects[column][0], f"{menu} {heading} {rects[heading]} {column} {rects[column]}")

    def test_custom_lobby_refresh_button_sits_inside_its_parent(self):
        # a child outside its parent draws but never gets clicks
        path = Path(__file__).resolve().parent.parent / "GenOnline/GameData/Window/Menus/GenOnlineCustomLobby.wnd"
        root = wl.parse_windows(path.read_bytes().decode("latin-1"))
        parents = {}

        def visit(win):
            for child in win.children:
                parents[(child.name or "").split(":")[-1]] = (child.rect, win.rect)
                visit(child)

        visit(root)
        (x0, y0, x1, y1), (px0, py0, px1, py1) = parents["ButtonRefresh"]
        self.assertTrue(px0 <= x0 and py0 <= y0 and x1 <= px1 and y1 <= py1, parents["ButtonRefresh"])


if __name__ == "__main__":
    unittest.main()
