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
        self.assertEqual(defined, {wl.GO_TEMPLATE_PREFIX + name for name in wl.RETAIL_TEMPLATES})


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
        expected = wl.compact_combos(wnd_yaml.compile_file(wnd_yaml.sources()[options]), options)
        names = {name.lower(): name for name in results}
        text, out, info = results[names[options.lower()]]
        self.assertEqual(out, expected)
        self.assertEqual(info["changes"], [])
        self.assertEqual(results[other][1], wl.transform(retail[other].decode("latin-1"), name=other))


PREVIEW_CALLBACK = 'DRAWCALLBACK = "W3DDrawMapPreview";'
BLACK_SLOT = "IMAGE: NoImage, COLOR: 0 0 0 255, BORDERCOLOR: 0 0 0 255"
PREVIEW_NAMES = ("MapWindow", "WinMapPreview")


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
        self.assertEqual(sorted(found), [f"Window/Menus/{n}.wnd" for n in (
            "GameSpyGameOptionsMenu", "GameSpyLoadScreen", "LanGameOptionsMenu", "LanMapSelectMenu",
            "MultiplayerLoadScreen", "SkirmishGameOptionsMenu", "SkirmishMapSelectMenu", "WOLMapSelectMenu")])
        for name, borders in found.items():
            self.assertEqual(borders, [["47", "75", "255", "255"]], name)


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

    def test_same_row_entry_follows_and_the_button_stays(self):
        out = wl.transform(self.row(), name=MENU)
        self.assertEqual(by_kind(out, "ENTRYFIELD"), [(320, 102, 440, 102 + wl.COMBO_HEIGHT)])
        self.assertEqual(by_kind(out, "PUSHBUTTON"), [(500, 100, 600, 100 + self.TALL)])

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
        seen = 0
        for path in sorted((wl.DATA / "Window").rglob("*.wnd")):
            name = path.relative_to(wl.DATA).as_posix()
            if not wl.themed(name) or name.lower() in wl.GEOMETRY_KEPT:
                continue
            for base in (wl.DATA, go_themes.THEME_DIR):
                for win in wl.parse_windows((base / name).read_bytes().decode("latin-1")).walk():
                    if win.kind == "COMBOBOX" and win.name not in exceptions:
                        seen += 1
                        self.assertEqual(win.rect[3] - win.rect[1], wl.COMBO_HEIGHT, f"{base / name} {win.name} {win.rect}")
        self.assertGreater(seen, 200)


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

    def test_shipped_custom_lobby_sort_buttons_have_no_group_frame(self):
        import go_themes
        name = "Window/Menus/WOLCustomLobby.wnd"
        for path in (wl.DATA / name, go_themes.THEME_DIR / name):
            slots = enabled_slots(path.read_bytes().decode("latin-1"))
            self.assertEqual(slots["WOLCustomLobby.wnd:ParentSortButtons"][0][2][3], "0", path)


class ShippedLayoutTest(unittest.TestCase):
    def test_game_setup_column_headings_start_at_their_column(self):
        cases = [
            ("SkirmishGameOptionsMenu", "StaticTextPlayers", "TextEntryPlayerName"),
            ("SkirmishGameOptionsMenu", "StaticTextMapPreview", "MapWindow"),
            ("LanGameOptionsMenu", "StaticTextFaction", "ComboBoxPlayerTemplate0"),
            ("GameSpyGameOptionsMenu", "StaticTextMapPreview", "MapWindow"),
        ]
        for menu, heading, column in cases:
            path = wl.DATA / f"Window/Menus/{menu}.wnd"
            rects = {w.name.split(":")[-1]: w.rect for w in wl.parse_windows(path.read_bytes().decode("latin-1")).walk()}
            self.assertEqual(rects[heading][0], rects[column][0], f"{menu} {heading} {rects[heading]} {column} {rects[column]}")

    def test_custom_lobby_refresh_button_sits_inside_its_parent(self):
        # a child outside its parent draws but never gets clicks
        path = Path(__file__).resolve().parent.parent / "GenOnline/GameData/Window/Menus/WOLCustomLobby.wnd"
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
