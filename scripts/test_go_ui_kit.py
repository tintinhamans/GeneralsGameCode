"""Tests for the glyph placement and the load screen bar of scripts/go_ui_kit.py.

Usage:
    python -m unittest scripts/test_go_ui_kit.py
"""

import inspect
import math
import re
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_themes as themes  # noqa: E402
import go_ui_kit as kit  # noqa: E402

CHECK_SIZE = 22  # the check box pieces, in retail pixels


def ink(hit, w, h):
    """Texel centres, in pixels, where a glyph hit test draws."""
    step = 1.0 / kit.SCALE
    return [((tx + 0.5) * step, (ty + 0.5) * step)
            for ty in range(h * kit.SCALE) for tx in range(w * kit.SCALE)
            if hit((tx + 0.5) * step, (ty + 0.5) * step)]


class CheckGlyphTest(unittest.TestCase):
    def setUp(self):
        self.points = ink(kit.check_glyph(CHECK_SIZE, CHECK_SIZE), CHECK_SIZE, CHECK_SIZE)
        self.centre = CHECK_SIZE / 2.0
        self.texel = 1.0 / kit.SCALE

    def bbox_centre(self, axis):
        values = [p[axis] for p in self.points]
        return (min(values) + max(values)) / 2.0

    def centroid(self, axis):
        return sum(p[axis] for p in self.points) / len(self.points)

    def test_check_ink_is_centred_across(self):
        self.assertLessEqual(abs(self.bbox_centre(0) - self.centre), self.texel)

    def test_check_ink_box_is_centred_down(self):
        self.assertLessEqual(abs(self.bbox_centre(1) - self.centre), 2 * self.texel)

    def test_check_optical_centre_is_box_centre(self):
        # a tick's mass sits in its lower vertex: centre the midpoint of its ink box centre and its centroid
        optical = (self.bbox_centre(1) + self.centroid(1)) / 2.0
        self.assertLessEqual(abs(optical - self.centre), self.texel)

    def test_checked_pieces_draw_the_glyph_in_the_box(self):
        names = [name for name, (_, checked) in kit.CHECK_BOXES.items() if checked]
        self.assertTrue(names)
        line = int(kit.SYMBOL_LINE * kit.SCALE) + 1
        for name, w, h, texels in kit.pieces():
            if name not in names:
                continue
            fill = texels[h * kit.SCALE // 2][line]
            rows = [ty for ty, row in enumerate(texels[line:-line], line) if any(t != fill for t in row[line:-line])]
            top_gap, bottom_gap = rows[0] - line, h * kit.SCALE - line - 1 - rows[-1]
            self.assertLessEqual(abs(top_gap - bottom_gap), 4, name)



def opaque_box(texels):
    """Bounding box (x0, y0, x1, y1), in texels, of the texels with any alpha; None when the piece is empty."""
    rows = [ty for ty, row in enumerate(texels) if any(t[3] > 0.0 for t in row)]
    cols = [tx for tx in range(len(texels[0])) if any(row[tx][3] > 0.0 for row in texels)]
    if not rows:
        return None
    return cols[0], rows[0], cols[-1] + 1, rows[-1] + 1


class KitPiecesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pieces = {name: (w, h, texels) for name, w, h, texels in kit.pieces()}

    def named(self, *prefixes):
        names = [name for name in self.pieces if name.startswith(prefixes)]
        self.assertTrue(names, prefixes)
        return names

    def assert_plain_rectangle(self, name):
        """Every texel inside the inked box is fully opaque and in one colour: square ends, no rounding."""
        w, h, texels = self.pieces[name]
        x0, y0, x1, y1 = opaque_box(texels)
        colour = texels[y0][x0]
        for row in texels[y0:y1]:
            for t in row[x0:x1]:
                self.assertEqual(t, colour, name)
        self.assertEqual(colour[3], 1.0, name)
        return x0, y0, x1, y1

    def test_selected_radio_art_stands_out_from_an_unselected_radio(self):
        # map source tabs, options and diplomacy draw a selected radio with the Hilighted art, difficulty select with Pushed
        def over(colour, back):
            a = colour[3]
            return tuple(a * c + (1.0 - a) * b for c, b in zip(colour[:3], back))

        def luminance(rgb):
            lin = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in rgb]
            return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]

        for palette in (None, themes.KIT_COLORS):
            states = kit.RADIO_STATES if palette is None else kit.recolor(kit.RADIO_STATES, palette)
            back = kit.rgba(themes.PALETTE["panel"])[:3]
            enabled = luminance(over(states["Enabled"][1], back))
            for state in ("Hilighted", "Pushed"):
                self.assertEqual(states[state], states["Pushed"], state)
                selected = luminance(over(states[state][1], back))
                self.assertGreaterEqual((selected + 0.05) / (enabled + 0.05), 1.75, (palette is None, state))
                self.assertGreater(luminance(states[state][0][:3]), luminance(states["Enabled"][0][:3]), state)

    def test_scroll_and_slider_bars_are_opaque_rectangles(self):
        for name in self.named("GOScrollTrack", "GOScrollThumb", "GOSliderTrack", "GOSliderFill", "GOSliderThumb"):
            w, h, _ = self.pieces[name]
            self.assertEqual(self.assert_plain_rectangle(name), (0, 0, w * kit.SCALE, h * kit.SCALE), name)

    def test_retail_scroll_thumbs_are_centred_plain_bars(self):
        for name in self.named("ScrollBarThumb"):
            w, h, _ = self.pieces[name]
            x0, y0, x1, y1 = self.assert_plain_rectangle(name)
            self.assertEqual(x0, w * kit.SCALE - x1, name)
            self.assertEqual(y0, h * kit.SCALE - y1, name)

    def test_scroll_arrows_draw_no_cell_highlight(self):
        # hover and pushed light only the chevron, so the kit has no cell pieces behind it
        self.assertEqual([name for name in self.pieces if name.startswith("GOScrollCell")], [])

    def test_lit_scroll_chevrons_take_the_active_glyph(self):
        def ink_colour(name):
            _, _, texels = self.pieces[name]
            return {tuple(round(c * 255) for c in t[:3]) for row in texels for t in row if t[3] > 0.0}

        active = {tuple(round(c * 255) for c in kit.GLYPH_ACTIVE[:3])}
        for arrow in ("GOScrollUp", "GOScrollDown"):
            self.assertNotEqual(ink_colour(arrow), active, arrow)
            for state in ("Hilite", "Pushed"):
                self.assertEqual(ink_colour(arrow + state), active, arrow + state)

    def test_scroll_chevrons_fill_their_piece_centred(self):
        # the engine centres the chevron piece on the column line the pill shares, so no offset may be baked in
        for name in self.named("GOScrollUp", "GOScrollDown"):
            w, h, texels = self.pieces[name]
            x0, y0, x1, y1 = opaque_box(texels)
            self.assertLessEqual(abs(x0 - (w * kit.SCALE - x1)), 1, name)
            self.assertLessEqual(abs(y0 - (h * kit.SCALE - y1)), 1, name)
            self.assertLessEqual(x0, 1, name)
            self.assertLessEqual(y0, 1, name)

    def test_scroll_chevrons_carry_only_their_glyph(self):
        for name in self.named("GOScrollUp", "GOScrollDown"):
            _, _, texels = self.pieces[name]
            colours = {tuple(round(c * 255) for c in t[:3]) for row in texels for t in row if t[3] > 0.0}
            self.assertEqual(len(colours), 1, name)

    def test_down_chevron_mirrors_up(self):
        for state in kit.SCROLL_ARROWS:
            up = self.pieces[f"GOScrollUp{state}"][2]
            down = self.pieces[f"GOScrollDown{state}"][2]
            self.assertEqual([[round(t[3] * 255) for t in row] for row in up],
                             [[round(t[3] * 255) for t in row] for row in reversed(down)], state)

    # combo arrow state -> the text entry state of the field it joins; pushed looks like hover
    COMBO_ARROW_FIELD = {"Enabled": "Enabled", "Hilite": "Hilite", "HiliteSelected": "Hilite", "Disabled": "Disabled"}

    def test_combo_arrows_are_one_box_with_the_field(self):
        # no left line and the field fill, so the arrow square reads as part of the field; outline in the field colour
        for state, field in self.COMBO_ARROW_FIELD.items():
            _, eh, entry = self.pieces[f"TextEntry{field}RepeatingCenter"]
            field_fill, field_line = entry[eh * kit.SCALE // 2][0], entry[0][0]
            for name in (f"VSliderUpButton{state}", f"VSliderDownButton{state}"):
                w, h, texels = self.pieces[name]
                mid = h * kit.SCALE // 2
                self.assertEqual(texels[mid][0], field_fill, name)
                self.assertEqual(texels[0][w * kit.SCALE // 2], field_line, name)
                self.assertEqual(texels[mid][-1], field_line, name)

    def test_pushed_combo_arrow_matches_hover(self):
        for direction in ("Up", "Down"):
            pushed = self.pieces[f"VSlider{direction}ButtonHiliteSelected"]
            self.assertTrue(pushed == self.pieces[f"VSlider{direction}ButtonHilite"], direction)


def q(colour):
    """A float colour as the 8 bit texel the TGA stores."""
    return tuple(round(c * 255) for c in colour)


def retail_excluded_files():
    """The lower-case data paths the Retail style leaves out by name (ArchiveFileSystem.cpp isNotGenOnlineInterfaceFile)."""
    source = (kit.ROOT / "Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp").read_text("latin-1")
    body = source[source.index("static Bool isNotGenOnlineInterfaceFile"):]
    start = body.index("files[] = {")
    return set(re.findall(r'"([^"]+)"', body[start:body.index("};", start)]))


class LoadingBarTest(unittest.TestCase):
    # the retail Coords sizes of the load screen bar images
    RETAIL_SIZES = {"LoadingBar_L": (20, 20), "LoadingBar_C": (10, 20), "LoadingBar_R": (20, 20),
                    "LoadingBar_Progress": (3, 11), "LoadingBar_DePowered": (3, 11)}

    @classmethod
    def setUpClass(cls):
        cls.pieces = {name: (w, h, texels) for name, w, h, texels in kit.load_bar_pieces()}

    def colour(self, token):
        return kit.rgba(themes.PALETTE[token])

    def build(self):
        return kit.build(kit.load_bar_pieces(), kit.LOAD_BAR_TEXTURE_NAME, kit.LOAD_BAR_ATLAS_WIDTH, kit.LOAD_BAR_TITLE)

    def test_bar_pieces_replace_the_retail_images_at_their_size(self):
        self.assertEqual({name: (w, h) for name, (w, h, _) in self.pieces.items()}, self.RETAIL_SIZES)

    def test_fill_is_plain_bronze_and_the_track_the_field_back(self):
        for name, colour in (("LoadingBar_Progress", self.colour("bronze")), ("LoadingBar_DePowered", kit.FIELD)):
            _, _, texels = self.pieces[name]
            self.assertEqual({q(t) for row in texels for t in row}, {q(colour)}, name)

    def assert_square_frame(self, pieces, line, ground):
        line, ground = q(line), q(ground)
        for name, left, right in (("LoadingBar_L", True, False), ("LoadingBar_C", False, False), ("LoadingBar_R", False, True)):
            w, h, texels = pieces[name]
            texels = [[q(t) for t in row] for row in texels]
            mid_y, mid_x = h * kit.SCALE // 2, w * kit.SCALE // 2
            self.assertEqual(texels[0][mid_x], line, name)
            self.assertEqual(texels[-1][mid_x], line, name)
            self.assertEqual(texels[mid_y][0], line if left else ground, name)
            self.assertEqual(texels[mid_y][-1], line if right else ground, name)
            self.assertEqual(texels[mid_y][mid_x], ground, name)
            # square ends: the outer corners are drawn, not cut
            if left:
                self.assertEqual(texels[0][0], line, name)
            if right:
                self.assertEqual(texels[-1][-1], line, name)
            # one kit line: LINE_LAYOUT pixels of line, then the back
            band = round(kit.LINE_LAYOUT * kit.SCALE)
            self.assertEqual(texels[band - 1][mid_x], line, name)
            self.assertEqual(texels[band][mid_x], ground, name)

    def test_back_is_a_square_kit_frame_in_the_signal_line_on_the_field_back(self):
        self.assert_square_frame(self.pieces, kit.BUTTON_STATES[""][0], kit.FIELD)

    def test_radar_bar_takes_the_radar_line_and_field_and_keeps_the_bronze_fill(self):
        radar = {name: (w, h, texels) for name, w, h, texels in kit.load_bar_pieces(themes.KIT_COLORS)}
        field = kit.rgba(themes.PALETTE["fill-field"], kit.FIELD[3])
        self.assert_square_frame(radar, self.colour("bronze"), field)
        self.assertEqual(radar["LoadingBar_Progress"], self.pieces["LoadingBar_Progress"])
        self.assertEqual({q(t) for row in radar["LoadingBar_DePowered"][2] for t in row}, {q(field)})
        # the kit atlas the palettes recolour has none of these names; the bar has its own atlas and its own theme copy
        kit_names = {name for name, _, _, _ in kit.pieces()}
        self.assertFalse(kit_names & set(self.pieces))
        texture, ini = kit.build(kit.load_bar_pieces(themes.KIT_COLORS), kit.LOAD_BAR_TEXTURE_NAME, kit.LOAD_BAR_ATLAS_WIDTH,
                                 kit.LOAD_BAR_TITLE)
        self.assertEqual(ini, self.build()[1])
        self.assertEqual(kit.THEME_LOAD_BAR_TEXTURE_PATH, themes.THEME_DIR / kit.LOAD_BAR_TEXTURE_PATH.relative_to(kit.DATA))
        self.assertEqual(kit.THEME_LOAD_BAR_TEXTURE_PATH.read_bytes(), texture)
        self.assertFalse((themes.THEME_DIR / kit.LOAD_BAR_INI_PATH.relative_to(kit.DATA)).exists())

    def test_ini_gives_the_frame_the_kit_line_border_only(self):
        _, ini = self.build()
        blocks = ini.split("MappedImage ")[1:]
        borders = {block.split("\r\n")[0]: f"  Border = {kit.LINE_LAYOUT}" in block for block in blocks}
        self.assertEqual(borders, {"LoadingBar_C": True, "LoadingBar_DePowered": False, "LoadingBar_L": True,
                                   "LoadingBar_Progress": False, "LoadingBar_R": True})
        # loads after the retail SCSmShellUserInterface512.INI that defines the retail bar
        self.assertEqual(kit.LOAD_BAR_INI_PATH.parent, kit.INI_PATH.parent)
        self.assertGreater(kit.LOAD_BAR_INI_PATH.name.lower(), "scsmshelluserinterface512.ini")

    def test_committed_bar_files_are_current(self):
        texture, ini = self.build()
        self.assertEqual(kit.LOAD_BAR_TEXTURE_PATH.read_bytes(), texture)
        self.assertEqual(kit.LOAD_BAR_INI_PATH.read_bytes(), ini.encode("ascii"))

    def test_retail_style_keeps_the_retail_bar(self):
        excluded = retail_excluded_files()
        for path in (kit.LOAD_BAR_TEXTURE_PATH, kit.LOAD_BAR_INI_PATH):
            self.assertIn(path.relative_to(kit.DATA).as_posix().lower(), excluded)
        # the Retail style mounts no palette, so the Radar copy goes with the Themes folder
        self.assertTrue(kit.THEME_LOAD_BAR_TEXTURE_PATH.relative_to(kit.DATA).as_posix().lower().startswith("themes/"))


SCREENS = ((800, 600), (1024, 768), (1366, 768), (1600, 900), (1920, 1080), (2560, 1080), (2560, 1440), (3440, 1440),
           (3840, 2160))


class LoadRoadArtTest(unittest.TestCase):
    """go_ui_kit.load_road_pieces: the road art the shell load bar draws, and its Radar copy."""

    NAMES = {"GORoadL", "GORoadC", "GORoadR", "GORoadDash", "GORoadDashLit", "GORoadTint"}

    @classmethod
    def setUpClass(cls):
        cls.pieces = {name: (w, h, texels) for name, w, h, texels in kit.load_road_pieces()}
        cls.radar = {name: (w, h, texels) for name, w, h, texels in kit.load_road_pieces(themes.KIT_COLORS)}

    def build(self, palette=None):
        return kit.build(kit.load_road_pieces(palette), kit.LOAD_ROAD_TEXTURE_NAME, kit.LOAD_ROAD_ATLAS_WIDTH, kit.LOAD_ROAD_TITLE)

    def assert_road(self, pieces, line):
        line, asphalt = q(line), q(kit.LOAD_ROAD_ASPHALT)
        band = round(kit.LINE_LAYOUT * kit.SCALE)
        for name in ("GORoadL", "GORoadC", "GORoadR"):
            w, h, texels = pieces[name]
            texels = [[q(t) for t in row] for row in texels]
            mid_x, mid_y = w * kit.SCALE // 2, h * kit.SCALE // 2
            # edge lines top and bottom at the kit line weight, asphalt between, no line at the ends
            self.assertEqual(texels[0][mid_x], line, name)
            self.assertEqual(texels[band - 1][mid_x], line, name)
            self.assertEqual(texels[band][mid_x], asphalt, name)
            self.assertEqual(texels[-1][mid_x], line, name)
            self.assertEqual(texels[mid_y][0], asphalt, name)
            self.assertEqual(texels[mid_y][-1], asphalt, name)
        # the caps round their outer corners
        self.assertEqual(pieces["GORoadL"][2][0][0][3], 0.0)
        self.assertEqual(pieces["GORoadR"][2][-1][-1][3], 0.0)
        self.assertEqual(pieces["GORoadC"][2][0][0][3], 1.0)

    def test_road_pieces_are_asphalt_between_kit_lines(self):
        self.assertEqual(set(self.pieces), self.NAMES)
        self.assert_road(self.pieces, kit.BUTTON_STATES[""][0])

    def test_asphalt_is_a_warm_near_black_distinct_from_the_panel_fill(self):
        r, g, b = q(kit.LOAD_ROAD_ASPHALT)[:3]
        self.assertTrue(r >= g >= b and r > b, (r, g, b))  # warm
        self.assertLess(max(r, g, b), 48)                   # near black
        for fill in (kit.FIELD, kit.rgba(themes.PALETTE["fill-field"]), kit.rgba(themes.PALETTE["panel"])):
            self.assertNotEqual(q(fill)[:3], (r, g, b))

    def test_dashes_dim_ahead_and_lit_behind_with_a_bronze_tint(self):
        flat = {name: {q(t) for row in texels for t in row} for name, (_, _, texels) in self.pieces.items()}
        self.assertEqual(flat["GORoadDash"], {q(kit.rgba(themes.PALETTE["empty"], 0.55))})
        self.assertEqual(flat["GORoadDashLit"], {q(kit.rgba(themes.PALETTE["tan"]))})
        self.assertEqual(flat["GORoadTint"], {q(kit.rgba(themes.PALETTE["bronze"], 0.18))})

    def test_radar_road_takes_the_bronze_line_and_shares_the_ini(self):
        self.assert_road(self.radar, kit.rgba(themes.PALETTE["bronze"]))
        for name in ("GORoadDash", "GORoadDashLit", "GORoadTint"):
            self.assertEqual(self.radar[name], self.pieces[name], name)
        texture, ini = self.build(themes.KIT_COLORS)
        self.assertEqual(ini, self.build()[1])
        self.assertEqual(kit.THEME_LOAD_ROAD_TEXTURE_PATH.read_bytes(), texture)
        self.assertFalse((themes.THEME_DIR / kit.LOAD_ROAD_INI_PATH.relative_to(kit.DATA)).exists())

    def test_ini_gives_the_road_pieces_the_kit_line_border(self):
        _, ini = self.build()
        borders = {block.split("\r\n")[0]: f"  Border = {kit.LINE_LAYOUT}" in block for block in ini.split("MappedImage ")[1:]}
        self.assertEqual(borders, {"GORoadC": True, "GORoadDash": False, "GORoadDashLit": False, "GORoadL": True,
                                   "GORoadR": True, "GORoadTint": False})

    def test_committed_road_files_are_current_and_retail_leaves_them_out(self):
        texture, ini = self.build()
        self.assertEqual(kit.LOAD_ROAD_TEXTURE_PATH.read_bytes(), texture)
        self.assertEqual(kit.LOAD_ROAD_INI_PATH.read_bytes(), ini.encode("ascii"))
        excluded = retail_excluded_files()
        for path in (kit.LOAD_ROAD_TEXTURE_PATH, kit.LOAD_ROAD_INI_PATH):
            self.assertIn(path.relative_to(kit.DATA).as_posix().lower(), excluded)


class ShellRoadTest(unittest.TestCase):
    """LoadScreen.cpp drawGoShellLoadBar: the road over the wider window ShellGameLoadScreen::init sets."""

    @classmethod
    def setUpClass(cls):
        cls.source = (kit.ROOT / "Core/GameEngine/Source/GameClient/GUI/LoadScreen.cpp").read_text("latin-1")
        start = cls.source.index("static void drawGoShellLoadBar( GameWindow *window, WinInstanceData *instData )")
        cls.draw = cls.source[start:cls.source.index("\n}\n", start)]

    def constant(self, name):
        return re.search(rf"static const (?:Int|Real) {name} = ([\d.]+)f?;", self.source).group(1)

    def test_road_is_a_third_of_the_width_centred_near_the_bottom(self):
        left, top, right, bottom = kit.LOAD_ROAD_RECT
        self.assertAlmostEqual((right - left) / 800, 1 / 3, delta=1 / 800)
        self.assertEqual(left, 800 - right)
        self.assertTrue(16 <= bottom - top <= 20)
        self.assertTrue(570 <= top and bottom <= 596)
        self.assertEqual([int(self.constant(n)) for n in ("GO_ROAD_LEFT", "GO_ROAD_TOP", "GO_ROAD_RIGHT", "GO_ROAD_BOTTOM")],
                         list(kit.LOAD_ROAD_RECT))
        self.assertEqual(float(self.constant("GO_ROAD_DASH_UNITS")), kit.LOAD_ROAD_DASH_UNITS)

    def test_init_sets_the_road_rect_only_on_the_go_credit_screen(self):
        # later shell loads (back from a match) show the box art with the plain bar in its retail place
        source = self.source[self.source.index("void ShellGameLoadScreen::init( GameInfo *game )"):]
        init = source[:source.index("\n}\n")]
        self.assertLess(init.index("initCredit();"), init.index("winSetDrawFunc(drawGoShellLoadBar)"))
        self.assertRegex(init, r"if \(m_creditLogo != nullptr && findGoLoadBarImage\(\) != nullptr\)\s*\{\s*"
                               r"m_progressBar->winSetDrawFunc\(drawGoShellLoadBar\);\s*"
                               r"const Int screenWidth = TheDisplay->getWidth\(\);\s*"
                               r"const Int screenHeight = TheDisplay->getHeight\(\);\s*"
                               r"m_progressBar->winSetPosition\(goLayoutScale\(GO_ROAD_LEFT, screenWidth, 800\), "
                               r"goLayoutScale\(GO_ROAD_TOP, screenHeight, 600\)\);\s*"
                               r"m_progressBar->winSetSize\(goLayoutScale\(GO_ROAD_RIGHT - GO_ROAD_LEFT, screenWidth, 800\), "
                               r"goLayoutScale\(GO_ROAD_BOTTOM - GO_ROAD_TOP, screenHeight, 600\)\);\s*\}")
        self.assertIn("return (value * numerator + denominator / 2) / denominator;", self.source)

    def test_dashes_are_whole_pixels_with_symmetric_padding(self):
        for screen in SCREENS:
            x, y, w, h = kit.load_road_window(*screen)
            line, dashes = kit.load_road_dashes(x, y, w, h, screen[1])
            self.assertGreater(len(dashes), 15, screen)
            lengths = {d[2] - d[0] for d in dashes}
            periods = {b[0] - a[0] for a, b in zip(dashes, dashes[1:])}
            self.assertEqual(len(lengths), 1)
            self.assertEqual(len(periods), 1)
            self.assertEqual(periods.pop() % 2, 0)
            # centred on the road between the edge lines, at the kit line weight
            self.assertEqual(dashes[0][1] - y, y + h - dashes[0][3], screen)
            self.assertIn(dashes[0][3] - dashes[0][1], (line, line + 1))
            # the same whole pixel padding at both ends, odd road widths included
            self.assertEqual(dashes[0][0] - x, x + w - dashes[-1][2], screen)

    def test_draw_follows_the_road_rule(self):
        for text in ("TheWindowManager->winDrawThreePieceImage(left, center, nullptr, right, x, y, width, height);",
                     "Int line = REAL_TO_INT_CEIL(left->getBorder() * TheDisplay->getHeight() / 600.0f - 0.01f);",
                     "const Int thickness = line + ((height - line) & 1);",
                     "Int half = REAL_TO_INT_FLOOR(GO_ROAD_DASH_UNITS * TheDisplay->getHeight() / 600.0f + 0.5f);",
                     "const Int period = 2 * half;",
                     "Int length = 2 * (half / 2);",
                     "const Int span = width - 2 * (height / 2);",
                     "const Int count = span >= length ? (span - length) / period + 1 : 0;",
                     "const Int pad = (width - total) / 2;",
                     "if (count > 0 && ((width - total) & 1) != 0)",
                     "const Int dashTop = y + (height - thickness) / 2;",
                     "const Int dashLeft = x + pad + k * period;",
                     "TheDisplay->drawImage(tint, x, y + line, litEnd, y + height - line);",
                     "Int litEnd = x + width * progress / 100;"):
            self.assertIn(text, self.draw)
        # the dashes stay put: nothing in the road draw reads the clock
        self.assertNotIn("timeGetTime", self.draw)
        # road, tint, dashes, then the van
        order = [self.draw.index(s) for s in ("winDrawThreePieceImage(", "drawImage(tint", "drawImage(dashLit",
                                                "drawGoLoadBarVan(window, progress, place);")]
        self.assertEqual(order, sorted(order))

    def test_lit_road_follows_progress_and_fills_the_road(self):
        road = (641, 1033, 1279, 1065)
        self.assertEqual(kit.load_road_lit_end(road, 0), road[0])
        self.assertEqual(kit.load_road_lit_end(road, 50), road[0] + (road[2] - road[0]) // 2)
        self.assertEqual(kit.load_road_lit_end(road, 100), road[2])
        # the lit end stays under the van while it drives: between its rear and its front
        for progress in (10, 35, 70, 90):
            height, (x0, _, x1, _) = kit.van_layout(road, progress, lambda strip: True)
            self.assertLessEqual(x0, kit.load_road_lit_end(road, progress))
            self.assertLessEqual(kit.load_road_lit_end(road, progress), x1)
        self.assertNotIn("place.x0 + goVanScale(GO_VAN_OUTLINE_LEFT", self.draw)


class LoadScreenPlayerBarTest(unittest.TestCase):
    """LoadScreen.cpp: the per-player bars keep their house colours; the campaign bar takes the bronze GO bar art."""

    KIT_CHECK = "findGoLoadBarImage()"

    @classmethod
    def setUpClass(cls):
        cls.source = (kit.ROOT / "Core/GameEngine/Source/GameClient/GUI/LoadScreen.cpp").read_text("latin-1")

    def body(self, signature):
        start = self.source.index(signature)
        return self.source[start:self.source.index("\n}\n", start)]

    def test_kit_check_is_the_bronze_bar_image_from_the_go_bar_art(self):
        # the Retail style leaves the GO bar art out, so LoadingBar_Progress is the retail image there and the check fails
        check = self.body("static const Image *findGoLoadBarImage()")
        self.assertIn('findImageByName("LoadingBar_Progress")', check)
        self.assertIn(f'getFilename().compareNoCase("{kit.LOAD_BAR_TEXTURE_NAME}")', check)
        self.assertIn("return nullptr;", check)

    def test_player_bars_keep_their_house_colour_image(self):
        sites = [m.start() for m in re.finditer(r'"LoadingBar_ProgressCenter%d"', self.source)]
        self.assertEqual(len(sites), 2)  # MultiPlayerLoadScreen and GameSpyLoadScreen
        for site in sites:
            block = self.source[self.source.rindex("imageName.format(", 0, site):]
            block = block[:block.index("winSetEnabledImage( 6, houseImage );") + len("winSetEnabledImage( 6, houseImage );")]
            # the retail house image by slot colour, the plain bar when a colour has none, and nothing GO over it
            self.assertRegex(block, r'^imageName\.format\("LoadingBar_ProgressCenter%d", slot->getApparentColor\(\)\);\s*'
                                    r"const Image \*houseImage = TheMappedImageCollection->findImageByName\(imageName\);\s*"
                                    r"if \(! houseImage\)\s*"
                                    r'houseImage = TheMappedImageCollection->findImageByName\("LoadingBar_Progress"\);\s*'
                                    r"m_progressBars\[netSlot\]->winSetEnabledImage\( 6, houseImage \);$")
            # the Generals half keeps its house colour bar
            generals = self.source.rindex("#if RTS_GENERALS", 0, site)
            self.assertIn("GadgetProgressBarSetEnabledBarColor(m_progressBars[netSlot],houseColor );",
                          self.source[generals:site])

    def test_campaign_bar_takes_the_bronze_image_under_the_kit_check(self):
        start = self.source.index('findImageByName("LoadingBar_ProgressCenter1")')
        block = self.source[start:self.source.index("TheGameLODManager", start)]
        self.assertRegex(block, r"if \(const Image \*goBarImage = findGoLoadBarImage\(\)\)\s*\{\s*"
                                r"m_progressBar->winSetEnabledImage\( 6, goBarImage \);\s*\}")

    def test_map_transfer_bar_keeps_its_house_colour(self):
        init = self.body("void MapTransferLoadScreen::init( GameInfo *game )")
        self.assertIn("GadgetProgressBarSetEnabledBarColor(m_progressBars[netSlot], houseColor );", init)
        self.assertNotIn(self.KIT_CHECK, init)
        self.assertNotIn("GameMakeColor(191, 144, 72", init)

    def test_kit_check_serves_only_the_campaign_bar_and_the_shell_road(self):
        calls = self.source.count(self.KIT_CHECK) - 1  # less the definition
        self.assertEqual(calls, 2)
        for screen in ("void MultiPlayerLoadScreen::init( GameInfo *game )", "void GameSpyLoadScreen::init( GameInfo *game )",
                       "void MapTransferLoadScreen::init( GameInfo *game )"):
            self.assertNotIn(self.KIT_CHECK, self.body(screen))

    def test_player_names_keep_their_house_colour(self):
        self.assertEqual(self.source.count("m_playerNames[netSlot]->winSetEnabledTextColors(houseColor"), 3)


def van_composite(layers, wheel):
    """The van layers stacked body, dish, wheels as premultiplied (r, g, b, a) floats of 0..255."""
    width, height = 256, 156
    out = [[(0.0, 0.0, 0.0, 0.0)] * width for _ in range(height)]
    for rows in (layers["GOVanBody"], layers["GOVanDish"], layers[f"GOVanWheels{wheel}"]):
        for y in range(height):
            for x in range(width):
                r, g, b, a = rows[y][x]
                if a:
                    k = a / 255.0
                    dr, dg, db, da = out[y][x]
                    out[y][x] = (r * k + dr * (1 - k), g * k + dg * (1 - k), b * k + db * (1 - k), a + da * (1 - k))
    return out


def premultiplied(t):
    k = t[3] / 255.0
    return (t[0] * k, t[1] * k, t[2] * k, float(t[3]))


class VanArtTest(unittest.TestCase):
    """scripts/go_ui_kit.py build_van: the van layers and the puff in one texture, one MappedImage each."""

    NAMES = {"GOVanBody", "GOVanDish", "GOVanWheels0", "GOVanWheels1", "GOVanWheels2", "GOVanWheels3", "GOVanPuff"}

    @classmethod
    def setUpClass(cls):
        cls.texture, cls.ini, cls.rects = kit.build_van()
        cls.images = {name: (w, h, rows) for name, w, h, rows in kit.van_images()}

    def test_sources_sit_in_the_scripts_art_folder(self):
        self.assertEqual(kit.VAN_SOURCES, kit.ROOT / "scripts" / "art" / "van-side")
        for name in list(kit.VAN_LAYER_FILES.values()) + ["van-side.svg", "van-side-256.png", kit.VAN_FRAMES_PNG.name]:
            self.assertTrue((kit.VAN_SOURCES / name).exists(), name)

    def test_images_are_the_layers_on_the_256x156_canvas_and_a_32_puff(self):
        self.assertEqual(set(self.images), self.NAMES)
        for name, (w, h, _) in self.images.items():
            self.assertEqual((w, h), (32, 32) if name == "GOVanPuff" else (256, 156), name)

    def test_layers_stack_to_the_full_van_frames(self):
        frames = kit.van_strip_frames(kit.VAN_FRAMES_PNG)
        layers = {name: rows for name, (_, _, rows) in self.images.items()}
        for wheel, (_, _, frame) in enumerate(frames):
            stack = van_composite(layers, wheel)
            seams = 0
            for y in range(156):
                for x in range(256):
                    want, got = premultiplied(frame[y][x]), stack[y][x]
                    if max(abs(a - b) for a, b in zip(want, got)) > 1.5:
                        seams += 1
            self.assertLess(seams, 200, wheel)

    def test_texture_is_a_32_bit_power_of_two_holding_every_image_texel(self):
        width, height, rows = themes.read_tga(self.texture)
        self.assertEqual(self.texture[16], 32)
        self.assertEqual(width & (width - 1), 0)
        self.assertEqual(height & (height - 1), 0)
        self.assertEqual(set(self.rects), self.NAMES)
        for name, (left, top, w, h) in self.rects.items():
            source = self.images[name][2]
            for y in range(h):
                for x in range(w):
                    self.assertEqual(premultiplied(rows[top + y][left + x]), premultiplied(source[y][x]), (name, x, y))

    def test_images_keep_a_clear_gap_and_clear_texels_take_the_drawn_colour(self):
        _, _, rows = themes.read_tga(self.texture)
        boxes = list(self.rects.values())
        for i, a in enumerate(boxes):
            for b in boxes[i + 1:]:
                apart = (a[0] + a[2] + kit.VAN_GAP <= b[0] or b[0] + b[2] + kit.VAN_GAP <= a[0]
                         or a[1] + a[3] + kit.VAN_GAP <= b[1] or b[1] + b[3] + kit.VAN_GAP <= a[1])
                self.assertTrue(apart, (a, b))
        # the art stores white under its clear texels; filtering would pull it in as a light fringe
        left, top, w, h = self.rects["GOVanBody"]
        self.assertNotIn((255, 255, 255, 0), {rows[top + y][left + x] for y in range(h) for x in range(w)})

    def test_ini_points_each_image_at_its_cell_in_texels(self):
        width, height, _ = themes.read_tga(self.texture)
        for name, (left, top, w, h) in self.rects.items():
            fields = themes.mapped_image(self.ini, name)
            self.assertEqual(fields["Texture"], kit.VAN_TEXTURE_NAME)
            self.assertEqual((int(fields["TextureWidth"]), int(fields["TextureHeight"])), (width, height))
            self.assertEqual(fields["Coords"], f"Left:{left} Top:{top} Right:{left + w} Bottom:{top + h}")
        self.assertEqual(kit.VAN_INI_PATH.parent, kit.INI_PATH.parent)

    def test_committed_van_files_are_current_and_checked(self):
        self.assertEqual(kit.VAN_TEXTURE_PATH.read_bytes(), self.texture)
        self.assertEqual(kit.VAN_INI_PATH.read_bytes(), self.ini.encode("ascii"))
        main = inspect.getsource(kit.main)
        self.assertIn("(VAN_TEXTURE_PATH, van_texture)", main)
        self.assertIn('(VAN_INI_PATH, van_ini.encode("ascii"))', main)

    def test_retail_style_leaves_the_van_out(self):
        excluded = retail_excluded_files()
        for path in (kit.VAN_TEXTURE_PATH, kit.VAN_INI_PATH):
            self.assertIn(path.relative_to(kit.DATA).as_posix().lower(), excluded)
        self.assertFalse((themes.THEME_DIR / kit.VAN_TEXTURE_PATH.relative_to(kit.DATA)).exists())


def screen_rect(win, screen):
    """A layout rect in pixels, as parseScreenRect scales it."""
    x0, y0, x1, y1 = win.rect
    x, y = kit.layout_scale(x0, screen[0], 800), kit.layout_scale(y0, screen[1], 600)
    return x, y, x + kit.layout_scale(x1 - x0, screen[0], 800), y + kit.layout_scale(y1 - y0, screen[1], 600)


def strip_clear(root, bar, screen, strip, bar_rect):
    """placeGoVan's room check: the strip lies in the bar's top window and no shown window that draws (not SEE_THRU) and
    does not hold the bar meets it."""
    top = screen_rect(root, screen)
    if strip[0] < top[0] or strip[1] < top[1] or strip[2] > top[2] or strip[3] > top[3]:
        return False

    def blocks(win):
        for child in win.children:
            if child is bar or "HIDDEN" in child.status or child.rect is None:
                continue
            r = screen_rect(child, screen)
            holds = r[0] <= bar_rect[0] and r[1] <= bar_rect[1] and r[2] >= bar_rect[2] and r[3] >= bar_rect[3]
            meets = r[0] < strip[2] and strip[0] < r[2] and r[1] < strip[3] and strip[1] < r[3]
            if "SEE_THRU" not in child.status and not holds and meets:
                return True
            if blocks(child):
                return True
        return False

    return not blocks(root)


def shell_layout(legal_shown):
    """The GO shell load layout: (top window, progress bar), with the legal text shown or hidden."""
    import go_wnd_layouts as wl
    root = wl.parse_windows((kit.DATA / "Window/Menus/ShellGameLoadScreen.wnd").read_bytes().decode("latin-1"))
    legal = next(w for w in root.walk() if w.name == "ShellGameLoadScreen.wnd:StaticTextLegal")
    legal.status = legal.status - {"HIDDEN"} if legal_shown else legal.status | {"HIDDEN"}
    bar = next(w for w in root.walk() if w.kind == "PROGRESSBAR")
    return root.children[0], bar


def shell_van(screen, progress, legal_shown=False):
    """(road rect, van layout) of the shell load road at a screen size."""
    root, bar = shell_layout(legal_shown)
    x, y, w, h = kit.load_road_window(*screen)
    road = (x, y, x + w, y + h)
    return road, kit.van_layout(road, progress, lambda strip: strip_clear(root, bar, screen, strip, road))


class VanRoadPlacementTest(unittest.TestCase):
    """go_ui_kit.van_layout, the rule placeGoVan follows on the shell load road."""

    def test_the_first_go_load_has_the_van_above_the_road_at_every_screen(self):
        for screen in SCREENS:
            for progress in (0, 35, 70, 100):
                road, layout = shell_van(screen, progress)
                self.assertIsNotNone(layout, (screen, progress))

    def test_van_stands_on_the_road_at_about_twice_its_height(self):
        for screen in SCREENS:
            road, (height, (x0, y0, _, _)) = shell_van(screen, 50)
            self.assertEqual(y0 + kit.van_scale(kit.VAN["GO_VAN_TYRE_BOTTOM"], height), road[1], screen)
            road_height = road[3] - road[1]
            self.assertTrue(height == kit.VAN["GO_VAN_MIN_HEIGHT"] or 1.6 * road_height <= height <= 2 * road_height, screen)

    def test_van_position_marks_progress_from_the_road_start_to_its_end(self):
        for screen in SCREENS:
            positions = []
            for progress in range(0, 101, 5):
                road, (height, (x0, _, _, _)) = shell_van(screen, progress)
                s = lambda v, h=height: kit.van_scale(v, h)  # noqa: E731
                bumper = x0 + s(kit.VAN["GO_VAN_BUMPER"])
                positions.append(bumper)
                if progress == 0:
                    self.assertEqual(bumper - s(kit.VAN["GO_VAN_BUMPER"] - kit.VAN["GO_VAN_REAR_TYRE"]), road[0], screen)
                if progress == 100:
                    self.assertEqual(bumper + s(kit.VAN["GO_VAN_OUTLINE_RIGHT"] - kit.VAN["GO_VAN_BUMPER"]), road[2], screen)
            self.assertEqual(positions, sorted(positions))
            self.assertGreater(positions[-1] - positions[0], (road[2] - road[0]) // 2)

    def test_retail_legal_text_takes_the_room_and_leaves_no_van(self):
        # -nologo keeps the retail title art and its legal text over the road: the road shows progress without the van
        self.assertIsNone(shell_van((1920, 1080), 50, legal_shown=True)[1])

    def test_van_and_puffs_stay_clear_of_the_website_line(self):
        for screen in SCREENS:
            road, (height, (x0, y0, _, _)) = shell_van(screen, 50)
            top = y0 + kit.van_scale(kit.VAN["GO_VAN_OUTLINE_TOP"], height) - kit.van_scale(kit.VAN["GO_VAN_BOB"], height)
            self.assertGreater(top, kit.url_rect(*screen)[3] + 20, screen)


class VanMotionTest(unittest.TestCase):
    """go_ui_kit.VanBar and the wiggle: what moves, how fast, and when it parks."""

    LAYOUT = (52, (100, 100, 190, 155))

    def drive(self, rate, until, start=1000):
        """Run a van whose position advances rate pixels per second; returns the bar and the spawn times."""
        bar = kit.VanBar(start, 0, 100)
        spawns = []
        for now in range(start, until, 16):
            position = 100 + int(rate * (now - start) / 1000)
            before = list(bar.puffs)
            bar.step(now, position - 100, position, self.LAYOUT)
            if bar.puffs != before:
                spawns.append(now)
        return bar, spawns

    def test_wheels_turn_at_about_12_frames_a_second_and_park(self):
        self.assertAlmostEqual(1000 / kit.VAN["GO_VAN_FRAME_MS"], 12, delta=0.5)
        frames = [kit.van_wheel_frame(t, True, 0) for t in range(0, 1000, kit.VAN["GO_VAN_FRAME_MS"])]
        self.assertEqual(frames[:5], [0, 1, 2, 3, 0])
        self.assertEqual(kit.van_wheel_frame(5000, False, 2), 2)
        self.assertEqual(kit.van_settle(1000, 1000 - 499), (True, 1.0))
        self.assertFalse(kit.van_settle(1000, 1000 - 500)[0])
        self.assertEqual(kit.van_settle(1000, 1000 - 800), (False, 0.0))

    def test_puffs_spawn_with_speed_and_never_while_parked(self):
        _, slow = self.drive(60, 3000)
        _, fast = self.drive(600, 3000)
        self.assertGreater(len(slow), 0)
        self.assertGreater(len(fast), 2 * len(slow))
        _, parked = self.drive(0, 3000)
        self.assertEqual(parked, [])
        # stopped: after the stall time no puff spawns, and the ring empties as they fade
        bar, _ = self.drive(600, 2000)
        spawned = []
        for now in range(2000, 4000, 16):
            before = list(bar.puffs)
            bar.step(now, bar.progress, bar.position, self.LAYOUT)
            if bar.puffs != before:
                spawned.append(now)
        self.assertTrue(all(now < 2000 + kit.VAN["GO_VAN_STALL_MS"] for now in spawned))
        self.assertEqual(bar.puff_rects(4000, 1.0), [])

    def test_ring_holds_twelve_puffs_and_spawns_are_spaced(self):
        bar, spawns = self.drive(5000, 3000)
        self.assertEqual(len(bar.puffs), kit.VAN["GO_VAN_PUFFS"])
        self.assertTrue(all(b - a >= kit.VAN["GO_VAN_PUFF_GAP_MS"] for a, b in zip(spawns, spawns[1:])))

    def test_puffs_drift_back_and_up_grow_and_fade_within_their_life(self):
        bar = kit.VanBar(0, 0, 0)
        bar.puffs[0] = (1000, 650, 200, 300, 52)
        early, late = bar.puff_rects(1050, 1.0)[0], bar.puff_rects(1600, 1.0)[0]
        self.assertLess(late[0] + late[2], early[0] + early[2])  # back
        self.assertLess(late[1] + late[3], early[1] + early[3])  # up
        self.assertGreater(late[2] - late[0], early[2] - early[0])  # grows
        self.assertLess(late[4], early[4])  # fades
        self.assertEqual(bar.puff_rects(1650, 1.0), [])
        lives = (kit.VAN["GO_VAN_PUFF_LIFE_MS"], kit.VAN["GO_VAN_PUFF_LIFE_MS"] + kit.VAN["GO_VAN_PUFF_LIFE_SPREAD_MS"])
        self.assertEqual(lives, (500, 800))

    def test_puffs_stay_on_the_van_canvas_that_the_room_check_covers(self):
        v = kit.VAN
        self.assertGreaterEqual(v["GO_VAN_PUFF_SPAWN_X"] - v["GO_VAN_PUFF_DRIFT_X"] - v["GO_VAN_PUFF_END"] // 2, 0)
        self.assertGreaterEqual(v["GO_VAN_PUFF_SPAWN_Y"] - v["GO_VAN_PUFF_DRIFT_Y"] - v["GO_VAN_PUFF_END"] // 2
                                - v["GO_VAN_PUFF_JITTER"], v["GO_VAN_OUTLINE_TOP"])
        self.assertLessEqual(v["GO_VAN_PUFF_SPAWN_Y"] + v["GO_VAN_PUFF_JITTER"] + v["GO_VAN_PUFF_START"] // 2,
                             v["GO_VAN_OUTLINE_BOTTOM"])

    def test_body_bobs_about_a_pixel_at_1080p_irregularly_and_settles(self):
        height = 52  # the shell bar van at 1080p
        bobs = [kit.van_wiggle(t, height, 1.0)[0] for t in range(0, 4000, 7)]
        self.assertEqual(set(bobs), {-1, 0, 1})
        # within the 2 canvas texels the art allows
        self.assertLessEqual(kit.VAN["GO_VAN_BOB"], 2)
        waves = [round(kit.van_wave(t), 3) for t in range(0, 2000, 50)]
        self.assertNotEqual(waves[:20], waves[20:])
        self.assertEqual({kit.van_wiggle(t, height, 0.0) for t in range(0, 2000, 7)}, {(0, 0)})

    def test_wave_wraps_on_a_whole_number_of_both_beats(self):
        # the clock wraps before the sines, so float precision never decays with uptime; the period holds whole beats of
        # both sines, so the wrap is seamless
        period = kit.VAN["GO_VAN_WAVE_MS"]
        for hz in (kit.VAN_REAL_CONSTANTS["GO_VAN_BOB_HZ_A"], kit.VAN_REAL_CONSTANTS["GO_VAN_BOB_HZ_B"]):
            self.assertAlmostEqual(hz * period / 1000.0, round(hz * period / 1000.0), places=9)
        for t in (0, 1234, 9999):
            self.assertAlmostEqual(kit.van_wave(t), kit.van_wave(t + 7 * period), places=9)
        step = abs(kit.van_wave(period - 1) - kit.van_wave(period))
        self.assertLess(step, 0.05)
        self.assertEqual(kit.van_wave(3_000_000_000 + 1234), kit.van_wave((3_000_000_000 + 1234) % period))

    def test_puff_lives_reach_the_whole_spread(self):
        lives = {kit.van_puff_life(kit.van_hash(n)) for n in range(20000)}
        low, spread = kit.VAN["GO_VAN_PUFF_LIFE_MS"], kit.VAN["GO_VAN_PUFF_LIFE_SPREAD_MS"]
        self.assertEqual(min(lives), low)
        self.assertEqual(max(lives), low + spread - 1)

    def test_dish_follows_the_bounce_late_by_a_pixel(self):
        sways = {kit.van_wiggle(t, 52, 1.0)[1] for t in range(0, 4000, 7)}
        self.assertEqual(sways, {-1, 0, 1})
        lag = kit.VAN["GO_VAN_DISH_LAG_MS"]
        for t in range(1000, 2000, 37):
            self.assertEqual(kit.van_wiggle(t, 52, 1.0)[1], math.floor(kit.van_wave(t - lag) + 0.5))


def brace_depth(text, start, end):
    """(lowest, final) brace depth of text[start:end] relative to start."""
    depth = low = 0
    for ch in text[start:end]:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            low = min(low, depth)
    return low, depth


class VanSourceTest(unittest.TestCase):
    """LoadScreen.cpp drawGoLoadBarVan: drawn by the shell load road only."""

    W3D = "Core/GameEngineDevice/Source/W3DDevice/GameClient/GUI/Gadget/W3DProgressBar.cpp"
    HEADER = "Core/GameEngine/Include/GameClient/LoadScreen.h"
    BEFORE_VAN = "554687f7b1"  # the branch before the van commits

    @classmethod
    def setUpClass(cls):
        cls.load = (kit.ROOT / "Core/GameEngine/Source/GameClient/GUI/LoadScreen.cpp").read_text("latin-1")
        start = cls.load.index("static void drawGoLoadBarVan(GameWindow *window")
        cls.van = cls.load[cls.load.index("static const Int GO_VAN_"):cls.load.index("\n}\n", start)]

    def body(self, source, signature):
        start = source.index(signature)
        return source[start:source.index("\n}\n", start)]

    def test_only_the_shell_road_draws_the_van(self):
        callers = []
        for folder in ("Core", "Generals/Code", "GeneralsMD/Code"):
            for path in (kit.ROOT / folder).rglob("*.[ch]*"):
                if path.suffix in (".cpp", ".h") and "drawGoLoadBarVan(" in path.read_text("latin-1"):
                    callers.append(path.name)
        self.assertEqual(callers, ["LoadScreen.cpp"])
        self.assertEqual(self.load.count("drawGoLoadBarVan("), 2)  # the definition and the shell road call
        shell = self.body(self.load, "static void drawGoShellLoadBar( GameWindow *window, WinInstanceData *instData )")
        self.assertIn("drawGoLoadBarVan(window, progress, place);", shell)
        for gone in ("isGoLoadBarImage", "goLoadBarVanFillEnd", "place.inside"):
            self.assertNotIn(gone, self.van)
        # the shared W3D bar draw and the header carry nothing of the van
        w3d = (kit.ROOT / self.W3D).read_text("latin-1")
        header = (kit.ROOT / self.HEADER).read_text("latin-1")
        for text in (w3d, header):
            for gone in ("isGoLoadBarImage", "drawGoLoadBarVan", "goLoadBarVanFillEnd", "LoadScreen.h\""):
                self.assertNotIn(gone, text)

    def test_shared_bar_draw_is_as_before_the_van(self):
        for path in (self.W3D, self.HEADER):
            before = subprocess.run(["git", "show", f"{self.BEFORE_VAN}:{path}"], cwd=kit.ROOT, capture_output=True)
            if before.returncode != 0:
                self.skipTest("no git history")
            self.assertEqual((kit.ROOT / path).read_bytes(), before.stdout, path)

    def test_van_needs_every_layer_before_it_takes_its_place(self):
        place = self.body(self.load, "static GoVanPlace placeGoVan(")
        for name in ('"GOVanBody"', '"GOVanDish"', "GO_VAN_WHEEL_IMAGES[wheel]", "goVanStripIsClear(window, strip)"):
            self.assertIn(name, place)
        table = re.search(r"static const char \*const GO_VAN_WHEEL_IMAGES\[GO_VAN_WHEEL_FRAMES\] = \{([^}]*)\};", self.van)
        self.assertEqual(re.findall(r'"([^"]+)"', table.group(1)), [f"GOVanWheels{k}" for k in range(kit.VAN_WHEEL_FRAMES)])
        self.assertIn("findImageByName(GO_VAN_WHEEL_IMAGES[state->frame])", self.van)
        self.assertNotIn(".format(", self.van)
        self.assertNotIn("AsciiString", self.van)
        # position from progress, as van_layout
        self.assertIn("const Int bumper = first + (last - first) * progress / 100;", place)

    def test_wave_wraps_on_its_period_and_puff_lives_reach_the_whole_spread(self):
        self.assertIn("const double t = (now % GO_VAN_WAVE_MS) / 1000.0;", self.van)
        self.assertIn("(hash >> 20) % GO_VAN_PUFF_LIFE_SPREAD_MS", self.van)

    def test_constants_match_the_model(self):
        ints = dict(re.findall(r"static const (?:Int|UnsignedInt) (GO_VAN_\w+) = (\d+);", self.van))
        self.assertEqual({k: int(v) for k, v in ints.items()}, kit.VAN_CONSTANTS)
        reals = dict(re.findall(r"static const Real (GO_VAN_\w+) = ([\d.]+)f;", self.van))
        self.assertEqual({k: float(v) for k, v in reals.items()}, kit.VAN_REAL_CONSTANTS)

    def test_van_draws_layers_with_the_image_calls_and_no_new_render_state(self):
        for name in ("GOVanBody", "GOVanDish", "GOVanWheels0", "GOVanPuff"):
            self.assertIn(f'"{name}"', self.van)
        self.assertIn("timeGetTime()", self.van)
        self.assertIn("TheDisplay->drawImage(", self.van)
        for banned in ("setClipRegion", "enableClipping", "DRAW_IMAGE_ADDITIVE", "new ", "malloc", "std::vector"):
            self.assertNotIn(banned, self.van)
        self.assertIn("GoVanPuff puffs[GO_VAN_PUFFS];", self.van)
        self.assertIn("static GoVanState s_goVan;", self.van)
        order = [self.van.index(s) for s in ("drawImage(puffImage", "drawImage(body", "drawImage(dish", "drawImage(wheels")]
        self.assertEqual(order, sorted(order))


class VanInMatchTest(unittest.TestCase):
    """The van rides the shell load road only, and no in-match layout uses the GO bar art."""

    def test_no_layout_names_a_van_or_road_image(self):
        for path in list((kit.DATA / "Window").rglob("*.wnd")) + list(themes.THEME_DIR.rglob("*.wnd")):
            text = path.read_bytes().decode("latin-1")
            self.assertNotIn("GOVan", text, path.name)
            self.assertNotIn("GORoad", text, path.name)

    def test_only_load_screens_and_the_download_menu_use_the_go_bar_fill(self):
        users = set()
        for root in (kit.DATA / "Window", themes.THEME_DIR / "Window"):
            for path in root.rglob("*.wnd"):
                if "LoadingBar_Progress" in path.read_bytes().decode("latin-1"):
                    users.add(path.name)
        self.assertEqual(users, {"ShellGameLoadScreen.wnd", "SinglePlayerLoadScreen.wnd", "MultiplayerLoadScreen.wnd",
                                 "GameSpyLoadScreen.wnd", "ChallengeLoadScreen.wnd", "DownloadMenu.wnd"})

    def test_only_the_load_screen_code_names_the_van_or_the_road(self):
        for folder in ("Core", "Generals/Code", "GeneralsMD/Code"):
            for path in (kit.ROOT / folder).rglob("*.[ch]*"):
                if path.suffix not in (".cpp", ".h"):
                    continue
                text = path.read_text("latin-1")
                if '"LoadingBar_Progress"' in text or '"GOVan' in text or '"GORoad' in text:
                    self.assertEqual(path.name, "LoadScreen.cpp")


class LoadSplashTest(unittest.TestCase):
    """The Minecraft style splash the logo's radar van says in a speech bubble (LoadScreen.cpp GO_LOAD_SPLASHES,
    drawGoLogoBubble from ShellGameLoadScreen::drawCredit)."""

    @classmethod
    def setUpClass(cls):
        cls.source = (kit.ROOT / "Core/GameEngine/Source/GameClient/GUI/LoadScreen.cpp").read_text("latin-1")
        start = cls.source.index("void ShellGameLoadScreen::initCredit()")
        cls.init_credit = cls.source[start:cls.source.index("\n}\n", start)]
        start = cls.source.index("void ShellGameLoadScreen::drawCredit()")
        cls.draw_credit = cls.source[start:cls.source.index("\n}\n", start)]
        start = cls.source.index("static void drawGoLogoBubble(")
        cls.bubble = cls.source[start:cls.source.index("\n}\n", start)]

    def test_table_holds_the_nine_lines_as_translatable_labels(self):
        table = self.source[self.source.index("static const GoLoadSplash GO_LOAD_SPLASHES[] = {"):]
        table = table[:table.index("};")]
        rows = re.findall(r'\{ "([^"]+)", L"([^"]*)" \}', table)
        self.assertEqual(rows, [(label, text) for label, text in kit.LOAD_SPLASHES])
        self.assertEqual(len(rows), 9)
        self.assertEqual([label for label, _ in rows], [f"GUI:GOLoadSplash{k}" for k in range(1, 10)])
        self.assertIn("TheGameText->FETCH_OR_SUBSTITUTE(GO_LOAD_SPLASHES[s_goSplashLine].label, "
                      "GO_LOAD_SPLASHES[s_goSplashLine].text)", self.init_credit)

    def test_line_is_picked_once_per_launch_from_the_clock_and_no_random_stream(self):
        self.assertIn("if (s_goSplashLine < 0)", self.init_credit)
        self.assertIn("s_goSplashLine = (Int)(timeGetTime() % (sizeof(GO_LOAD_SPLASHES) / sizeof(GO_LOAD_SPLASHES[0])));",
                      self.init_credit)
        for text in (self.init_credit, self.draw_credit, self.bubble):
            for banned in ("GameLogicRandomValue", "GameClientRandomValue", "GameAudioRandomValue", "rand("):
                self.assertNotIn(banned, text)
        self.assertEqual({kit.splash_pick(t) for t in range(100)}, set(range(9)))

    def test_line_exists_only_with_the_go_logo(self):
        pick = self.init_credit.index("s_goSplashLine = ")
        block = self.init_credit.rindex("if (m_creditLogo != nullptr)", 0, pick)
        low, depth = brace_depth(self.init_credit, block, pick)
        self.assertGreaterEqual(low, 0)
        self.assertGreaterEqual(depth, 1)
        self.assertIn('TheFontLibrary->getFont("Consolas", Int(GO_SPLASH_FONT_SIZE * fontScale), TRUE)', self.init_credit)
        self.assertEqual(int(re.search(r"GO_SPLASH_FONT_SIZE = (\d+);", self.source).group(1)), kit.SPLASH_FONT_SIZE)
        for gone in ("GO_SPLASH_PULSE_MS", "GO_SPLASH_WRAP_MS", "GO_SPLASH_LOGO_OVERLAP", "GO_SPLASH_MARGIN"):
            self.assertNotIn(gone, self.source)

    def test_bubble_is_said_by_the_logo_van_not_the_driving_van(self):
        # no line (no GO logo, -nologo, a later load): no bubble
        self.assertIn("if (s_goSplash == nullptr", self.bubble)
        van = self.source[self.source.index("static void drawGoLoadBarVan("):]
        van = van[:van.index("\n}\n")]
        for gone in ("Bubble", "GO_BUBBLE_TIP"):
            self.assertNotIn(gone, van)
        for gone in ("appearedAt", "drawGoVanBubble", "GO_BUBBLE_TIP_X", "GO_BUBBLE_TIP_Y"):
            self.assertNotIn(gone, self.source)
        self.assertEqual(self.source.count("drawGoLogoBubble("), 2)  # the definition and the credit call
        # drawn with the logo, from the logo's own placement, fading in from the credit's start
        call = self.draw_credit.index("drawGoLogoBubble(")
        block = self.draw_credit.rindex("if (m_creditLogo != nullptr", 0, call)
        low, depth = brace_depth(self.draw_credit, block, call)
        self.assertGreaterEqual(low, 0)
        self.assertGreaterEqual(depth, 1)
        for text in ("const Int dishX = logoLeft + GO_LOGO_DISH_X * logoWidth / m_creditLogo->getImageWidth();",
                     "const Int dishY = logoTop + GO_LOGO_DISH_Y * logoHeight / m_creditLogo->getImageHeight();",
                     "drawGoLogoBubble(dishX, dishY - REAL_TO_INT(GO_BUBBLE_TIP_GAP * unit), timeGetTime() - m_creditStartMs);"):
            self.assertIn(text, self.draw_credit)

    def test_bubble_draw_follows_the_layout_rule(self):
        for text in ("const Int lineHeight = s_goSplash->getFont()->height;",
                     "const Int corner = lineHeight * 3 / 5;",
                     "const Int tailWidth = lineHeight * 9 / 10;",
                     "const Int tailHeight = lineHeight * 3 / 4;",
                     "const Int over = lineHeight * 9 / 80;",
                     "Int height = textHeight + lineHeight / 2;",
                     "Int width = textWidth + lineHeight;",
                     "Int left = tipX - width * 3 / 4;",
                     "const Int bottom = tipY - tailHeight + over;",
                     "const Int top = bottom - height;",
                     "Int tailCenter = tipX;",
                     "if (shownMs < GO_BUBBLE_SHOW_MS)",
                     "const Int alpha = 255;"):
            self.assertIn(text, self.bubble)
        r, g, b = themes._rgb(kit.BUBBLE_TEXT)
        self.assertIn(f"GameMakeColor({r}, {g}, {b}, alpha)", self.bubble)
        for name, _, _, _ in kit.bubble_pieces():
            self.assertIn(f'"{name}"', self.bubble)
        ints = dict(re.findall(r"static const (?:Int|UnsignedInt) (GO_(?:BUBBLE|LOGO_DISH)_\w+) = (\d+);", self.source))
        self.assertEqual({k: int(v) for k, v in ints.items()}, kit.BUBBLE_CONSTANTS)
        for name, value in (("GO_BUBBLE_MARGIN", kit.BUBBLE_MARGIN_UNITS), ("GO_BUBBLE_TIP_GAP", kit.BUBBLE_TIP_GAP_UNITS),
                            ("GO_BUBBLE_WRAP", kit.BUBBLE_WRAP_UNITS)):
            self.assertEqual(float(re.search(rf"static const Real {name} = ([\d.]+)f;", self.source).group(1)), value)

    def test_line_wraps_centred_at_the_wrap_width_scaled_like_the_font(self):
        self.assertIn("s_goSplash->setWordWrap(REAL_TO_INT(GO_BUBBLE_WRAP * fontScale));", self.init_credit)
        self.assertIn("s_goSplash->setWordWrapCentered(TRUE);", self.init_credit)
        self.assertLess(self.init_credit.index("->setFont("), self.init_credit.index("->setWordWrap("))
        self.assertEqual(kit.SPLASH_FONT_SIZE, 16)

    def test_long_lines_take_two_lines_and_short_ones_one_at_every_screen(self):
        expected = {1: 2, 2: 2, 3: 1, 4: 1, 5: 2, 6: 2, 7: 1, 8: 2, 9: 2}
        for screen in SCREENS:
            counts = {k + 1: len(kit.splash_text_block(text, *screen)[0]) for k, (_, text) in enumerate(kit.LOAD_SPLASHES)}
            self.assertEqual(counts, expected, screen)
        self.assertEqual(kit.splash_text_block(kit.LOAD_SPLASHES[4][1], 1920, 1080)[0], ["What station you need,", "general?"])

    def test_extra_lines_grow_the_bubble_upward_from_a_fixed_bottom(self):
        screen = (1920, 1080)
        tip = kit.bubble_tip(*screen)
        one = kit.splash_text_block(kit.LOAD_SPLASHES[3][1], *screen)
        two = kit.splash_text_block(kit.LOAD_SPLASHES[4][1], *screen)
        b1, b2 = (kit.bubble_layout(tip, w, h, lh, *screen) for _, w, h, lh in (one, two))
        self.assertEqual(b1["bubble"][3], b2["bubble"][3])
        self.assertEqual(b1["bubble"][1] - b2["bubble"][1], two[3])
        self.assertEqual(b1["corner"], b2["corner"])  # the corners keep their size

    def test_dish_constants_are_measured_from_the_logo_art(self):
        dish = kit.logo_dish()
        self.assertEqual(dish, (kit.BUBBLE_CONSTANTS["GO_LOGO_DISH_X"], kit.BUBBLE_CONSTANTS["GO_LOGO_DISH_Y"]))
        # the logo crop is the INI's, and the rim is the logo's topmost opaque row, on the van's mast at the left
        fields = themes.mapped_image((kit.DATA / themes.LOAD_LOGO_INI).read_text("latin-1"), themes.LOAD_LOGO_IMAGE)
        left, top, right, bottom = kit.LOGO_COORDS
        self.assertEqual(fields["Coords"], f"Left:{left} Top:{top} Right:{right} Bottom:{bottom}")
        self.assertEqual((right - left, bottom - top), kit.LOGO_ASPECT)
        self.assertEqual(dish[1], 0)
        self.assertLess(dish[0], kit.LOGO_ASPECT[0] // 3)

    def bubbles(self):
        for screen in SCREENS:
            tip = kit.bubble_tip(*screen)
            for _, text in kit.LOAD_SPLASHES:
                _, w, h, line_height = kit.splash_text_block(text, *screen)
                yield screen, text, tip, kit.bubble_layout(tip, w, h, line_height, *screen)

    def test_bubble_stays_on_screen_clear_of_the_logo_and_the_website_line(self):
        def meets(a, b):
            return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]

        for screen, text, tip, b in self.bubbles():
            key = (screen, text)
            margin = int(kit.BUBBLE_MARGIN_UNITS * screen[1] / 600.0)
            left, top, right, bottom = b["bubble"]
            self.assertGreaterEqual(left, margin, key)
            self.assertLessEqual(right, screen[0] - margin, key)
            self.assertGreater(top, 0, key)
            # above the logo's topmost opaque row (the dish rim), so it covers none of the logo
            logo = kit.logo_rect(*screen)
            self.assertLessEqual(bottom, logo[1], key)
            self.assertLessEqual(b["tail"][3], logo[1], key)
            self.assertFalse(meets(b["bubble"], kit.url_rect(*screen)), key)
            self.assertFalse(meets(b["tail"], kit.url_rect(*screen)), key)
            # the text sits inside the outline, clear of the corner curves
            self.assertGreaterEqual(b["text"][0], left + b["corner"] // 3, key)
            self.assertLessEqual(b["text"][2], right - b["corner"] // 3, key)
            self.assertTrue(top < b["text"][1] and b["text"][3] < bottom, key)

    def test_tail_tip_points_at_the_measured_dish(self):
        for screen, text, tip, b in self.bubbles():
            key = (screen, text)
            dish_x, dish_y = kit.logo_dish_screen(*screen)
            tail_left, tail_top, tail_right, tail_bottom = b["tail"]
            self.assertEqual(tail_bottom, tip[1], key)
            self.assertLessEqual(abs((tail_left + tail_right) // 2 - dish_x), 1, key)
            self.assertTrue(0 < dish_y - tail_bottom <= int(kit.BUBBLE_TIP_GAP_UNITS * screen[1] / 600.0) + 1, key)
            # the tail hangs from the straight part of the bubble's bottom edge, off centre to the right
            self.assertGreaterEqual(tail_left, b["bubble"][0] + b["corner"], key)
            self.assertLessEqual(tail_right, b["bubble"][2] - b["corner"] + 1, key)
            self.assertTrue(b["bubble"][1] < tail_top <= b["bubble"][3], key)
            self.assertGreater((tail_left + tail_right) // 2, (b["bubble"][0] + b["bubble"][2]) // 2, key)

    def test_bubble_is_pulled_back_at_a_screen_edge_and_the_tail_slides(self):
        screen, text = (800, 600), kit.LOAD_SPLASHES[4][1]
        _, w, h, line_height = kit.splash_text_block(text, *screen)
        margin = int(kit.BUBBLE_MARGIN_UNITS)
        for tip_x, edge in ((screen[0] - 40, "right"), (60, "left")):
            b = kit.bubble_layout((tip_x, 300), w, h, line_height, *screen)
            left, _, right, bottom = b["bubble"]
            if edge == "right":
                self.assertEqual(right, screen[0] - margin)
            else:
                self.assertEqual(left, margin)
            self.assertNotEqual(left, tip_x - (right - left) * 3 // 4)
            centre = (b["tail"][0] + b["tail"][2]) // 2
            low, high = left + b["corner"] + (b["tail"][2] - b["tail"][0]) // 2, right - b["corner"] - (b["tail"][2] - b["tail"][0]) // 2
            self.assertEqual(centre, max(low, min(tip_x, high)))
            self.assertEqual(b["tail"][3], 300)

    def test_bubble_pops_in_whole_after_the_logo_fade_and_stays(self):
        show = kit.BUBBLE_CONSTANTS["GO_BUBBLE_SHOW_MS"]
        self.assertEqual({kit.bubble_alpha(t) for t in range(0, show, 7)}, {0})
        self.assertEqual({kit.bubble_alpha(t) for t in range(show, show + 60000, 997)}, {255})


class BubbleArtTest(unittest.TestCase):
    """go_ui_kit.bubble_pieces: the speech bubble in the van's colours, one art for both palettes."""

    @classmethod
    def setUpClass(cls):
        cls.pieces = {name: (w, h, texels) for name, w, h, texels in kit.bubble_pieces()}

    def build(self):
        return kit.build(kit.bubble_pieces(), kit.BUBBLE_TEXTURE_NAME, kit.BUBBLE_ATLAS_WIDTH, kit.BUBBLE_TITLE)

    def test_bubble_is_a_cream_9_slice_inside_the_van_outline_with_a_tail(self):
        cream, ink = q(kit.rgba(themes.PALETTE["text"])), q(kit.rgba(themes.PALETTE["ink"]))
        self.assertEqual(ink[:3], (0x0D, 0x15, 0x1B))
        self.assertEqual(set(self.pieces), {"GOBubbleTL", "GOBubbleT", "GOBubbleTR", "GOBubbleL", "GOBubbleC", "GOBubbleR",
                                            "GOBubbleBL", "GOBubbleB", "GOBubbleBR", "GOBubbleTail"})
        c, e = kit.BUBBLE_CORNER, kit.BUBBLE_EDGE
        sizes = {name: (w, h) for name, (w, h, _) in self.pieces.items()}
        for name in ("GOBubbleTL", "GOBubbleTR", "GOBubbleBL", "GOBubbleBR"):
            self.assertEqual(sizes[name], (c, c), name)
        self.assertEqual((sizes["GOBubbleT"], sizes["GOBubbleB"], sizes["GOBubbleL"], sizes["GOBubbleR"], sizes["GOBubbleC"]),
                         ((e, c), (e, c), (c, e), (c, e), (e, e)))
        line = round(kit.BUBBLE_LINE_WIDTH * kit.SCALE)

        def texel(name, y, x):
            return q(self.pieces[name][2][y][x])

        # the edges: the line on their outer side, cream inside
        self.assertEqual((texel("GOBubbleT", 0, 0), texel("GOBubbleT", line, 0)), (ink, cream))
        self.assertEqual((texel("GOBubbleB", -1, 0), texel("GOBubbleB", -1 - line, 0)), (ink, cream))
        self.assertEqual((texel("GOBubbleL", 0, 0), texel("GOBubbleL", 0, line)), (ink, cream))
        self.assertEqual((texel("GOBubbleR", 0, -1), texel("GOBubbleR", 0, -1 - line)), (ink, cream))
        self.assertEqual({q(t) for row in self.pieces["GOBubbleC"][2] for t in row}, {cream})
        # rounded outer corners, cream at the inner one
        for name, outer, inner in (("GOBubbleTL", (0, 0), (-1, -1)), ("GOBubbleTR", (0, -1), (-1, 0)),
                                   ("GOBubbleBL", (-1, 0), (0, -1)), ("GOBubbleBR", (-1, -1), (0, 0))):
            self.assertEqual(self.pieces[name][2][outer[0]][outer[1]][3], 0.0, name)
            self.assertEqual(texel(name, *inner), cream, name)
        tw, th, tail = self.pieces["GOBubbleTail"]
        self.assertEqual(q(tail[0][tw * kit.SCALE // 2]), cream)  # the top covers the bubble line
        self.assertEqual(tail[0][0][3], 1.0)
        self.assertEqual(tail[-1][0][3], 0.0)                     # narrows to the tip
        self.assertGreater(tail[-1][tw * kit.SCALE // 2][3], 0.0)

    def test_committed_bubble_files_are_current_and_retail_leaves_them_out(self):
        texture, ini = self.build()
        self.assertEqual(kit.BUBBLE_TEXTURE_PATH.read_bytes(), texture)
        self.assertEqual(kit.BUBBLE_INI_PATH.read_bytes(), ini.encode("ascii"))
        self.assertNotIn("Border", ini)
        excluded = retail_excluded_files()
        for path in (kit.BUBBLE_TEXTURE_PATH, kit.BUBBLE_INI_PATH):
            self.assertIn(path.relative_to(kit.DATA).as_posix().lower(), excluded)
        self.assertFalse((themes.THEME_DIR / kit.BUBBLE_TEXTURE_PATH.relative_to(kit.DATA)).exists())


class MapKitThemeTest(unittest.TestCase):
    """The map preview icons and favourite stars atlas, per palette."""

    @classmethod
    def setUpClass(cls):
        cls.base = {name: texels for name, _, _, texels in kit.pieces2()}
        cls.radar = {name: texels for name, _, _, texels in kit.pieces2(themes.MAP_COLORS)}

    @staticmethod
    def rgbs(texels):
        """The 8 bit colours of the drawn texels; supersampled edges keep the colour and blend only alpha."""
        return {tuple(round(c * 255) for c in t[:3]) for row in texels for t in row if t[3]}

    def test_radar_outline_is_the_field_fill_and_the_off_star_the_empty_token(self):
        outline, off = themes._rgb("#05070F"), themes._rgb("#5D6080")
        field, empty = themes._rgb(themes.PALETTE["fill-field"]), themes._rgb(themes.PALETTE["empty"])
        base = set().union(*(self.rgbs(t) for t in self.base.values()))
        radar = set().union(*(self.rgbs(t) for t in self.radar.values()))
        self.assertTrue({outline, off} <= base)
        self.assertFalse({outline, off} & radar)
        self.assertIn(field, radar)
        self.assertEqual(self.rgbs(self.radar["GOMapFavouriteOff"]), {empty})
        self.assertEqual(kit.MAP_GLYPH_OUTLINE[3], 0.85)  # the outline keeps its alpha in both palettes

    def test_radar_keeps_the_glyph_and_star_colours_and_every_shape(self):
        self.assertEqual(set(self.radar), set(self.base))
        design = {colour for _, colour in kit.MAP_ICONS.values()} | set(kit.STAR_ON)
        for name, texels in self.base.items():
            radar = self.radar[name]
            self.assertEqual([[t[3] for t in row] for row in radar], [[t[3] for t in row] for row in texels], name)
            solid = [(y, x) for y, row in enumerate(texels) for x, t in enumerate(row) if t in design]
            self.assertTrue(solid or name == "GOMapFavouriteOff", name)
            for y, x in solid:
                self.assertEqual(radar[y][x], texels[y][x], (name, y, x))

    def test_committed_radar_atlas_is_current_and_shares_the_base_ini(self):
        texture, ini = kit.build(kit.pieces2(themes.MAP_COLORS), kit.TEXTURE2_NAME, kit.ATLAS2_WIDTH, kit.TITLE2)
        self.assertEqual(kit.THEME_TEXTURE2_PATH, themes.THEME_DIR / kit.TEXTURE2_PATH.relative_to(kit.DATA))
        self.assertEqual(kit.THEME_TEXTURE2_PATH.read_bytes(), texture)
        self.assertEqual(ini.encode("ascii"), kit.INI2_PATH.read_bytes())
        self.assertFalse((themes.THEME_DIR / kit.INI2_PATH.relative_to(kit.DATA)).exists())
        self.assertNotEqual(kit.TEXTURE2_PATH.read_bytes(), texture)


if __name__ == "__main__":
    unittest.main()
