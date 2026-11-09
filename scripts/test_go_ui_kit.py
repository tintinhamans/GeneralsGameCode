"""Tests for the glyph placement and the load screen bar of scripts/go_ui_kit.py.

Usage:
    python -m unittest scripts/test_go_ui_kit.py
"""

import inspect
import math
import re
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


def shell_bar_window(screen_width, screen_height):
    """Screen rect of ShellGameLoadScreen.wnd:ProgressLoad (324 576 to 472 592 of 800x600), as parseScreenRect scales it."""
    def scale(value, numerator, denominator):
        return (value * numerator + denominator // 2) // denominator

    x, y = scale(324, screen_width, 800), scale(576, screen_height, 600)
    return x, y, scale(472 - 324, screen_width, 800), scale(592 - 576, screen_height, 600)


class ShellLoadBarTest(unittest.TestCase):
    """LoadScreen.cpp drawGoShellLoadBar: the kit frame over the window, the bronze fill inset by the same gap all round."""

    SCREENS = ((800, 600), (1024, 768), (1280, 720), (1366, 768), (1600, 900), (1920, 1080), (2000, 1125), (2560, 1080),
               (2560, 1440), (3440, 1440), (3840, 2160))

    @classmethod
    def setUpClass(cls):
        cls.source = (kit.ROOT / "Core/GameEngine/Source/GameClient/GUI/LoadScreen.cpp").read_text("latin-1")
        start = cls.source.index("static void drawGoShellLoadBar( GameWindow *window, WinInstanceData *instData )")
        cls.draw = cls.source[start:cls.source.index("\n}\n", start)]

    def test_padding_is_the_same_on_every_side_at_every_resolution(self):
        for screen in self.SCREENS:
            x, y, w, h = shell_bar_window(*screen)
            for percent in (0, 1, 50, 99, 100):
                line, (left, top, right, bottom) = kit.shell_load_bar_layout(x, y, w, h, screen[1], percent)
                self.assertGreaterEqual(line, 1, screen)
                self.assertEqual(top - y, y + h - bottom, (screen, percent))
                self.assertEqual(left - x, top - y, (screen, percent))
                self.assertGreater(bottom - top, 0, screen)
            # full: the fill ends as far from the right edge as it starts from the left one, two lines in
            line, (left, top, right, bottom) = kit.shell_load_bar_layout(x, y, w, h, screen[1], 100)
            self.assertEqual(left - x, x + w - right, screen)
            self.assertEqual(left - x, 2 * line, screen)
            # empty: nothing drawn; half: half the inside, to the pixel
            self.assertEqual(kit.shell_load_bar_layout(x, y, w, h, screen[1], 0)[1][2], left)
            self.assertEqual(kit.shell_load_bar_layout(x, y, w, h, screen[1], 50)[1][2] - left, (right - left) * 50 // 100)

    def test_frame_line_is_the_one_the_three_piece_bar_draws(self):
        for _, screen_height in self.SCREENS:
            border = math.ceil(kit.LINE_LAYOUT * screen_height / 600.0 - 0.01)
            self.assertEqual(kit.shell_load_bar_layout(0, 0, 300, 30, screen_height, 100)[0], max(1, border))
        three_piece = (kit.ROOT / "Core/GameEngine/Source/GameClient/GUI/GameWindowGlobal.cpp").read_text("latin-1")
        self.assertIn("Int borderHeight = REAL_TO_INT_CEIL( left->getBorder() * bands.layoutScale - 0.01f );", three_piece)
        self.assertIn("bands.layoutScale = (Real)TheDisplay->getHeight() / 600.0f;", three_piece)

    def test_draw_follows_the_layout_rule(self):
        self.assertIn("TheWindowManager->winDrawThreePieceImage(left, center, nullptr, right, x, y, width, height);", self.draw)
        self.assertIn("Int line = REAL_TO_INT_CEIL(left->getBorder() * TheDisplay->getHeight() / 600.0f - 0.01f);", self.draw)
        self.assertIn("const Int inset = 2 * line;", self.draw)
        self.assertIn("const Int fillWidth = width - 2 * inset;", self.draw)
        self.assertIn("const Int fillHeight = height - 2 * inset;", self.draw)
        self.assertIn("const Int drawWidth = fillWidth * progress / 100;", self.draw)
        # the fill runs to its end, or to the rear of a van driving inside the bar (goLoadBarVanFillEnd)
        self.assertIn("Int fillStop = x + inset + drawWidth;", self.draw)
        self.assertIn("TheDisplay->drawImage(fill, x + inset, y + inset, fillStop, y + inset + fillHeight);", self.draw)
        # one stretched fill, no tiles: tiles of the fill height leave a remainder on the right
        self.assertNotIn("for (", self.draw)

    def test_shell_bar_takes_the_draw_only_with_the_go_bar_art(self):
        source = self.source[self.source.index("void ShellGameLoadScreen::init( GameInfo *game )"):]
        init = source[:source.index("\n}\n")]
        self.assertRegex(init, r"if \(findGoLoadBarImage\(\) != nullptr\)\s*\{\s*"
                               r"m_progressBar->winSetDrawFunc\(drawGoShellLoadBar\);\s*\}")


class LoadScreenPlayerBarTest(unittest.TestCase):
    """LoadScreen.cpp: every bar it colours per player or faction takes the bronze bar under the GO bar art check."""

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

    def test_player_bars_take_the_bronze_image_under_the_kit_check_and_the_house_image_otherwise(self):
        sites = [m.start() for m in re.finditer(r'"LoadingBar_ProgressCenter%d"', self.source)]
        self.assertEqual(len(sites), 2)  # MultiPlayerLoadScreen and GameSpyLoadScreen
        for site in sites:
            block = self.source[site:self.source.index("winSetEnabledImage( 6, houseImage );", site)]
            # the retail house image lookup stays, and the kit image replaces it only when the check finds it
            self.assertIn("findImageByName(imageName)", block)
            self.assertRegex(block, r"if \(const Image \*kitImage = findGoLoadBarImage\(\)\)\s*\{\s*houseImage = kitImage;\s*\}")
            # the Generals half has no GO bar art and keeps its house colour bar
            generals = self.source.rindex("#if RTS_GENERALS", 0, site)
            self.assertIn("GadgetProgressBarSetEnabledBarColor(m_progressBars[netSlot],houseColor );",
                          self.source[generals:site])

    def test_campaign_bar_takes_the_bronze_image_under_the_kit_check(self):
        start = self.source.index('findImageByName("LoadingBar_ProgressCenter1")')
        block = self.source[start:self.source.index("TheGameLODManager", start)]
        self.assertRegex(block, r"if \(const Image \*goBarImage = findGoLoadBarImage\(\)\)\s*\{\s*"
                                r"m_progressBar->winSetEnabledImage\( 6, goBarImage \);\s*\}")

    def test_map_transfer_bar_takes_the_bronze_colour_under_the_kit_check(self):
        # drawn in colour, not images: the house colour stays without the GO bar art
        init = self.body("void MapTransferLoadScreen::init( GameInfo *game )")
        house = "GadgetProgressBarSetEnabledBarColor(m_progressBars[netSlot], houseColor );"
        self.assertIn(house, init)
        r, g, b = themes._rgb(themes.PALETTE["bronze"])
        self.assertRegex(init[init.index(house):], rf"if \({re.escape(self.KIT_CHECK)} != nullptr\)\s*\{{\s*"
                                                   rf"GadgetProgressBarSetEnabledBarColor\(m_progressBars\[netSlot\], "
                                                   rf"GameMakeColor\({r}, {g}, {b}, 255\) \);\s*\}}")

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
    def scale(value, numerator, denominator):
        return (value * numerator + denominator // 2) // denominator

    x0, y0, x1, y1 = win.rect
    x, y = scale(x0, screen[0], 800), scale(y0, screen[1], 600)
    return x, y, x + scale(x1 - x0, screen[0], 800), y + scale(y1 - y0, screen[1], 600)


def strip_clear(root, bar, screen, strip):
    """drawGoLoadBarVan's room check: the strip lies in the bar's top window and no shown window that draws (not
    SEE_THRU) and does not hold the bar meets it."""
    top = screen_rect(root, screen)
    if strip[0] < top[0] or strip[1] < top[1] or strip[2] > top[2] or strip[3] > top[3]:
        return False
    bar_rect = screen_rect(bar, screen)

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


def go_bar_layouts():
    """{layout name: (top window, [progress bars], framed)} of the layouts that use the GO bar art."""
    import go_wnd_layouts as wl
    out = {}
    for path in sorted((kit.DATA / "Window").rglob("*.wnd")):
        text = path.read_bytes().decode("latin-1")
        if "IMAGE: LoadingBar_Progress" not in text:
            continue
        root = wl.parse_windows(text)
        bars = [w for w in root.walk() if w.kind == "PROGRESSBAR"]
        out[path.stem] = (root.children[0], bars, "IMAGE: LoadingBar_L" in text)
    return out


def w3d_bar(bar_rect, framed, percent):
    """W3DGadgetProgressBarImageDraw's van arguments: the road (the frame, or the fill strip of a frameless bar), the fill
    area 10 in from the sides and 5 from the top and bottom, and the end of the whole fill tiles."""
    x0, y0, x1, y1 = bar_rect
    fill = (x0 + 10, y0 + 5, x1 - 10, y1 - 5)
    tile = max(1, (3 * (fill[3] - fill[1]) + 5) // 11)  # LoadingBar_Progress is 3x11
    end = fill[0] + tile * (((x1 - x0 - 20) * percent // 100) // tile)
    return (bar_rect if framed else fill), fill, end


def w3d_drawn_end(fill, end, stop):
    """Where W3DGadgetProgressBarImageDraw ends its fill tiles once goLoadBarVanFillEnd stops the fill before its end."""
    if stop >= end:
        return end
    tile = max(1, (3 * (fill[3] - fill[1]) + 5) // 11)
    return fill[0] + tile * ((stop - fill[0]) // tile)


def van_bars(layouts, screen, percent):
    """Every GO bar art bar of the layouts at a screen size: (layout, bar, rect, road, fill, fill end, van layout)."""
    for name, (root, bars, framed) in layouts.items():
        for bar in bars:
            rect = screen_rect(bar, screen)
            if name == "ShellGameLoadScreen":
                # drawGoShellLoadBar: the frame is the road, the whole inside the fill area
                args = (rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1], screen[1])
                road, fill = rect, kit.shell_load_bar_layout(*args, 100)[1]
                end = kit.shell_load_bar_layout(*args, percent)[1][2]
            else:
                road, fill, end = w3d_bar(rect, framed, percent)
            layout = kit.van_layout(road, fill, end, lambda strip, r=root, b=bar: strip_clear(r, b, screen, strip))
            yield name, bar, rect, road, fill, end, layout


class VanPlacementTest(unittest.TestCase):
    """go_ui_kit.van_layout, the rule drawGoLoadBarVan follows, over the GO bar art layouts."""

    SCREENS = ((800, 600), (1366, 768), (1600, 900), (1920, 1080), (2560, 1440), (3840, 2160))
    # above: room over the bar; inside: the row above (or a label) sits right over it
    MODES = {"ShellGameLoadScreen": "above", "SinglePlayerLoadScreen": "above", "ChallengeLoadScreen": "above",
             "MultiplayerLoadScreen": "inside", "GameSpyLoadScreen": "inside", "DownloadMenu": "inside"}

    @classmethod
    def setUpClass(cls):
        cls.layouts = go_bar_layouts()

    def test_the_go_bar_art_layouts_are_the_load_screens_and_the_download_bar(self):
        self.assertEqual(set(self.layouts), set(self.MODES))

    def test_each_layout_takes_its_mode_at_1080p(self):
        for name, bar, *_, layout in van_bars(self.layouts, (1920, 1080), 50):
            self.assertEqual(layout[0], self.MODES[name], (name, bar.name))

    def test_van_stands_on_the_bar_or_fits_the_fill_and_never_meets_another_window(self):
        for screen in self.SCREENS:
            for percent in (0, 50, 100):
                for name, bar, rect, road, fill, end, layout in van_bars(self.layouts, screen, percent):
                    if layout is None:
                        # no room above and a fill under the minimum height: no van at all
                        self.assertLess(fill[3] - fill[1], kit.VAN["GO_VAN_MIN_HEIGHT"], (name, screen))
                        continue
                    mode, height, (x0, y0, x1, y1) = layout
                    s = lambda v, h=height: kit.van_scale(v, h)  # noqa: E731
                    self.assertGreaterEqual(height, kit.VAN["GO_VAN_MIN_HEIGHT"])
                    if mode == "above":
                        # tyre bottoms on the bar's top edge, about 1.6 to 2 times the bar tall
                        self.assertEqual(y0 + s(kit.VAN["GO_VAN_TYRE_BOTTOM"]), road[1], (name, screen))
                        bar_height = road[3] - road[1]
                        self.assertTrue(height == kit.VAN["GO_VAN_MIN_HEIGHT"] or 1.6 * bar_height <= height <= 2 * bar_height,
                                        (name, screen, height, bar_height))
                        self.assertTrue(strip_clear(self.layouts[name][0], bar, screen, kit.van_above(road)[1]))
                    else:
                        # the outline rows fill the fill area, inside the bar window
                        self.assertEqual(height, fill[3] - fill[1])
                        # the art offsets round one by one, so the outline top may land a pixel off the fill top
                        self.assertLessEqual(abs(y0 + s(kit.VAN["GO_VAN_OUTLINE_TOP"]) - fill[1]), 1)
                        self.assertLessEqual(y0 + s(kit.VAN["GO_VAN_OUTLINE_BOTTOM"]), rect[3])

    def test_bumper_rides_the_fill_end_and_stays_on_the_bar(self):
        for screen in self.SCREENS:
            for percent in (0, 1, 10, 50, 90, 99, 100):
                for name, bar, rect, road, fill, end, layout in van_bars(self.layouts, screen, percent):
                    if layout is None:
                        continue
                    mode, height, (x0, _, _, _) = layout
                    s = lambda v, h=height: kit.van_scale(v, h)  # noqa: E731
                    if mode == "inside":
                        continue  # test_inside_the_van_drives_on_the_track_ahead_of_the_fill
                    left, right = road[0], road[2]
                    bumper = x0 + s(kit.VAN["GO_VAN_BUMPER"])
                    rear_tyre = bumper - s(kit.VAN["GO_VAN_BUMPER"] - kit.VAN["GO_VAN_REAR_TYRE"])
                    outline_right = bumper + s(kit.VAN["GO_VAN_OUTLINE_RIGHT"] - kit.VAN["GO_VAN_BUMPER"])
                    self.assertLessEqual(outline_right, right, (name, screen, percent))
                    self.assertGreaterEqual(rear_tyre, left, (name, screen, percent))
                    if rear_tyre > left and outline_right < right:
                        self.assertEqual(bumper, end, (name, screen, percent))
                    if percent == 0:
                        self.assertEqual(rear_tyre, left, (name, screen))
                    # above the bar the fill runs to its own end under the van
                    self.assertEqual(kit.van_fill_end(layout, end), end)

    def test_inside_the_van_drives_on_the_track_ahead_of_the_fill(self):
        """Inside, the van's rear sits at the fill end on the dark track and the fill stops at the van's rear. In the GO
        layouts with the legal text hidden only the W3D drawn bars go inside; VanShellFirstLoadTest covers the legal text."""
        seen = set()
        for screen in self.SCREENS:
            for percent in (0, 1, 10, 50, 90, 99, 100):
                for name, bar, rect, road, fill, end, layout in van_bars(self.layouts, screen, percent):
                    if layout is None or layout[0] != "inside":
                        continue
                    seen.add(name)
                    _, height, (x0, _, _, _) = layout
                    s = lambda v, h=height: kit.van_scale(v, h)  # noqa: E731
                    rear = x0 + s(kit.VAN["GO_VAN_OUTLINE_LEFT"])
                    outline_right = x0 + s(kit.VAN["GO_VAN_OUTLINE_RIGHT"])
                    drawn = w3d_drawn_end(fill, end, kit.van_fill_end(layout, end))
                    key = (name, screen, percent)
                    self.assertLessEqual(outline_right, fill[2], key)
                    self.assertLessEqual(drawn, rear, key)  # no fill under the van
                    if outline_right < fill[2]:
                        self.assertEqual(rear, end, key)
                        self.assertEqual(drawn, end, key)
                    else:
                        # clamped at the right end: the fill runs up to its rear, to the last whole tile
                        tile = max(1, (3 * (fill[3] - fill[1]) + 5) // 11)
                        self.assertGreater(drawn, rear - tile, key)
                    if percent == 0:
                        self.assertEqual(rear, fill[0], key)
                        self.assertEqual(drawn, fill[0], key)
        self.assertEqual(seen, {"MultiplayerLoadScreen", "GameSpyLoadScreen", "DownloadMenu"})

    def test_without_a_van_inside_the_fill_is_untouched(self):
        self.assertEqual(kit.van_fill_end(None, 123), 123)
        self.assertEqual(kit.van_fill_end(("above", 52, (0, 0, 90, 55)), 123), 123)


class VanMotionTest(unittest.TestCase):
    """go_ui_kit.VanBar and the wiggle: what moves, how fast, and when it parks."""

    LAYOUT = ("above", 52, (100, 100, 190, 155))

    def drive(self, rate, until, start=1000):
        """Run a bar whose fill end advances rate pixels per second; returns the bar and the spawn times."""
        bar = kit.VanBar(start, 0, 100)
        spawns = []
        for now in range(start, until, 16):
            fill_end = 100 + int(rate * (now - start) / 1000)
            before = list(bar.puffs)
            bar.step(now, fill_end - 100, fill_end, self.LAYOUT)
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
            bar.step(now, bar.progress, bar.fill_end, self.LAYOUT)
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
    """LoadScreen.cpp drawGoLoadBarVan and its two callers."""

    @classmethod
    def setUpClass(cls):
        cls.load = (kit.ROOT / "Core/GameEngine/Source/GameClient/GUI/LoadScreen.cpp").read_text("latin-1")
        cls.w3d = (kit.ROOT / "Core/GameEngineDevice/Source/W3DDevice/GameClient/GUI/Gadget/W3DProgressBar.cpp").read_text("latin-1")
        cls.header = (kit.ROOT / "Core/GameEngine/Include/GameClient/LoadScreen.h").read_text("latin-1")
        start = cls.load.index("void drawGoLoadBarVan(GameWindow *window")
        cls.van = cls.load[cls.load.index("static const Int GO_VAN_"):cls.load.index("\n}\n", start)]

    def body(self, source, signature):
        start = source.index(signature)
        return source[start:source.index("\n}\n", start)]

    def assert_guarded_call(self, body, guard):
        self.assertEqual(body.count("drawGoLoadBarVan("), 1)
        call = body.index("drawGoLoadBarVan(")
        check = body.rindex(guard, 0, call)
        low, depth = brace_depth(body, check, call)
        self.assertGreaterEqual(low, 0)
        self.assertGreaterEqual(depth, 1)

    def test_go_art_check_is_the_bar_texture(self):
        check = self.body(self.load, "Bool isGoLoadBarImage(const Image *image)")
        self.assertIn(f'image->getFilename().compareNoCase("{kit.LOAD_BAR_TEXTURE_NAME}") == 0', check)
        self.assertIn("image != nullptr", check)
        self.assertIn("Bool isGoLoadBarImage(const Image *image);", self.header)
        self.assertIn("void drawGoLoadBarVan(GameWindow *window, Int progress, const IRegion2D &road, const IRegion2D &fill, "
                      "Int fillEnd);", self.header)

    def test_van_is_drawn_only_behind_the_go_art_check_by_both_bar_draws(self):
        self.assert_guarded_call(self.body(self.w3d, "void W3DGadgetProgressBarImageDraw( GameWindow *window, WinInstanceData *instData )"),
                                 "if( goVan )")
        shell = self.body(self.load, "static void drawGoShellLoadBar( GameWindow *window, WinInstanceData *instData )")
        self.assertIn("const Bool goVan = isGoLoadBarImage(fill);", shell)
        self.assert_guarded_call(shell, "if (goVan)")
        self.assertNotIn("drawGoLoadBarVan(", self.body(self.w3d, "void W3DGadgetProgressBarImageDrawA("))
        callers = []
        for folder in ("Core", "Generals/Code", "GeneralsMD/Code"):
            for path in (kit.ROOT / folder).rglob("*.cpp"):
                if "drawGoLoadBarVan(" in path.read_text("latin-1"):
                    callers.append(path.name)
        self.assertEqual(sorted(callers), ["LoadScreen.cpp", "W3DProgressBar.cpp"])
        self.assertEqual(self.load.count("drawGoLoadBarVan("), 2)  # the definition and the shell bar call

    def test_w3d_draw_passes_the_frame_or_the_fill_strip_and_the_tile_end(self):
        draw = self.body(self.w3d, "void W3DGadgetProgressBarImageDraw( GameWindow *window, WinInstanceData *instData )")
        self.assertIn("const Bool goVan = isGoLoadBarImage( barCenter );", draw)
        self.assertIn("const Int fillEnd = origin.x + 10 + barCenterWidth * pieces;", draw)
        self.assertIn("if( isGoLoadBarImage( backLeft ) )", draw)
        self.assertIn("drawGoLoadBarVan( window, progress, vanRoad, vanFill, fillEnd );", draw)
        self.assertIn('#include "GameClient/LoadScreen.h"', self.w3d)

    def test_w3d_draw_stops_the_fill_tiles_at_a_van_inside_the_bar_before_drawing_them(self):
        draw = self.body(self.w3d, "void W3DGadgetProgressBarImageDraw( GameWindow *window, WinInstanceData *instData )")
        stop = draw.index("goLoadBarVanFillEnd( window, vanRoad, vanFill, fillEnd )")
        self.assertLess(stop, draw.index("TheWindowManager->winDrawImage( barCenter,"))
        low, depth = brace_depth(draw, draw.rindex("if( goVan )", 0, stop), stop)
        self.assertGreaterEqual(low, 0)
        self.assertGreaterEqual(depth, 1)
        self.assertRegex(draw[stop:], r"^goLoadBarVanFillEnd\( window, vanRoad, vanFill, fillEnd \);\s*"
                                      r"if\( fillStop < fillEnd \)\s*\{\s*"
                                      r"pieces = \( fillStop - vanFill\.lo\.x \) / barCenterWidth;\s*"
                                      r"if\( pieces < 0 \)\s*\{\s*pieces = 0;\s*\}\s*\}")
        self.assertIn("Int goLoadBarVanFillEnd(GameWindow *window, const IRegion2D &road, const IRegion2D &fill, Int fillEnd);",
                      self.header)
        callers = [p.name for folder in ("Core", "Generals/Code", "GeneralsMD/Code") for p in (kit.ROOT / folder).rglob("*.cpp")
                   if "goLoadBarVanFillEnd(" in p.read_text("latin-1")]
        self.assertEqual(sorted(callers), ["LoadScreen.cpp", "W3DProgressBar.cpp"])
        self.assertEqual(self.load.count("goLoadBarVanFillEnd("), 2)  # the definition and the shell bar draw
        # the van rear the fill stops at, from the shared placement
        fill_end = self.body(self.load, "Int goLoadBarVanFillEnd(GameWindow *window")
        self.assertIn("place.x0 + goVanScale(GO_VAN_OUTLINE_LEFT, place.height)", fill_end)
        self.assertIn("!place.inside", fill_end)

    def test_shell_draw_stops_its_fill_at_a_van_inside_the_bar_before_drawing_it(self):
        shell = self.body(self.load, "static void drawGoShellLoadBar( GameWindow *window, WinInstanceData *instData )")
        stop = shell.index("fillStop = goLoadBarVanFillEnd(window, road, fillArea, fillStop);")
        self.assertLess(stop, shell.index("TheDisplay->drawImage(fill,"))
        low, depth = brace_depth(shell, shell.rindex("if (goVan)", 0, stop), stop)
        self.assertGreaterEqual(low, 0)
        self.assertGreaterEqual(depth, 1)
        self.assertIn("drawGoLoadBarVan(window, progress, road, fillArea, x + inset + drawWidth);", shell)

    def test_van_needs_every_layer_before_it_takes_its_place(self):
        place = self.body(self.load, "static GoVanPlace placeGoVan(")
        for name in ('"GOVanBody"', '"GOVanDish"', "GO_VAN_WHEEL_IMAGES[wheel]"):
            self.assertIn(name, place)
        table = re.search(r"static const char \*const GO_VAN_WHEEL_IMAGES\[GO_VAN_WHEEL_FRAMES\] = \{([^}]*)\};", self.van)
        self.assertEqual(re.findall(r'"([^"]+)"', table.group(1)), [f"GOVanWheels{k}" for k in range(kit.VAN_WHEEL_FRAMES)])
        # one lookup by a fixed name per frame, no string built while drawing
        self.assertIn("findImageByName(GO_VAN_WHEEL_IMAGES[state->frame])", self.van)
        self.assertNotIn(".format(", self.van)
        self.assertNotIn("AsciiString", self.van)

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
        self.assertIn("static GoVanState s_goVans[GO_VAN_BARS];", self.van)
        # puffs first, then body, dish and wheels
        order = [self.van.index(s) for s in ("drawImage(puffImage", "drawImage(body", "drawImage(dish", "drawImage(wheels")]
        self.assertEqual(order, sorted(order))


class VanShellFirstLoadTest(unittest.TestCase):
    """The first shell load shows the legal text, which the layout puts right over the bar; with the GO logo in place of
    the title art, ShellGameLoadScreen::initCredit moves it under the website line so the van keeps its place above."""

    SCREENS = ((800, 600), (1366, 768), (1920, 1080), (2560, 1440), (3840, 2160))
    LEGAL = "ShellGameLoadScreen.wnd:StaticTextLegal"

    @classmethod
    def setUpClass(cls):
        cls.source = (kit.ROOT / "Core/GameEngine/Source/GameClient/GUI/LoadScreen.cpp").read_text("latin-1")
        start = cls.source.index("void ShellGameLoadScreen::initCredit()")
        cls.init_credit = cls.source[start:cls.source.index("\n}\n", start)]

    def constant(self, name):
        return float(re.search(rf"static const Real {name} = ([\d.]+)f;", self.source).group(1))

    def shell(self, legal_top=None):
        """The shell layout with the legal text shown, at its layout place or moved to legal_top (800x600 units)."""
        root, bars, _ = go_bar_layouts()["ShellGameLoadScreen"]
        legal = next(w for w in root.walk() if w.name == self.LEGAL)
        legal.status = legal.status - {"HIDDEN"}
        if legal_top is not None:
            x0, y0, x1, y1 = legal.rect
            legal.rect = (x0, int(legal_top), x1, int(legal_top) + y1 - y0)
        return root, bars[0], legal

    def mode(self, root, bar, screen):
        rect = screen_rect(bar, screen)
        args = (rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1], screen[1])
        fill = kit.shell_load_bar_layout(*args, 100)[1]
        layout = kit.van_layout(rect, fill, fill[0], lambda strip: strip_clear(root, bar, screen, strip))
        return layout and layout[0]

    def test_legal_text_at_its_layout_place_takes_the_room_above_the_bar(self):
        root, bar, legal = self.shell()
        self.assertNotIn("HIDDEN", legal.status)
        self.assertNotEqual(self.mode(root, bar, (1920, 1080)), "above")

    def test_first_load_with_the_legal_text_moved_keeps_the_van_above_the_bar(self):
        root, bar, _ = self.shell(self.constant("CREDIT_LEGAL_TOP"))
        for screen in self.SCREENS:
            self.assertEqual(self.mode(root, bar, screen), "above", screen)

    def test_legal_text_sits_under_the_website_line(self):
        legal_top, url_top = self.constant("CREDIT_LEGAL_TOP"), self.constant("CREDIT_URL_TOP")
        self.assertGreaterEqual(legal_top, url_top + 16)  # the 14 point website line and a gap
        _, _, legal = self.shell(legal_top)
        self.assertLess(legal.rect[3], 576 - 40)  # well clear of the bar at 576

    def test_initcredit_moves_the_legal_text_only_with_the_go_logo(self):
        move = self.init_credit.index(f'nameToKey("{self.LEGAL}")')
        block_start = self.init_credit.rindex("if (m_creditLogo != nullptr)", 0, move)
        low, depth = brace_depth(self.init_credit, block_start, move)
        self.assertGreaterEqual(low, 0)
        self.assertGreaterEqual(depth, 1)
        self.assertIn("legal->winSetPosition(legalX, REAL_TO_INT(CREDIT_LEGAL_TOP * screenHeight / 600.0f));",
                      self.init_credit)


class VanInMatchTest(unittest.TestCase):
    """The van rides the GO bar art only, and no in-match layout uses that art."""

    def test_no_layout_names_a_van_image(self):
        for path in list((kit.DATA / "Window").rglob("*.wnd")) + list(themes.THEME_DIR.rglob("*.wnd")):
            self.assertNotIn("GOVan", path.read_bytes().decode("latin-1"), path.name)

    def test_only_load_screens_and_the_download_menu_use_the_go_bar_fill(self):
        users = set()
        for root in (kit.DATA / "Window", themes.THEME_DIR / "Window"):
            for path in root.rglob("*.wnd"):
                if "LoadingBar_Progress" in path.read_bytes().decode("latin-1"):
                    users.add(path.name)
        self.assertEqual(users, {"ShellGameLoadScreen.wnd", "SinglePlayerLoadScreen.wnd", "MultiplayerLoadScreen.wnd",
                                 "GameSpyLoadScreen.wnd", "ChallengeLoadScreen.wnd", "DownloadMenu.wnd"})

    def test_only_the_load_screen_code_sets_the_go_bar_fill_or_names_the_van(self):
        for folder in ("Core", "Generals/Code", "GeneralsMD/Code"):
            for path in (kit.ROOT / folder).rglob("*.[ch]*"):
                if path.suffix not in (".cpp", ".h"):
                    continue
                text = path.read_text("latin-1")
                if '"LoadingBar_Progress"' in text or '"GOVan' in text:
                    self.assertEqual(path.name, "LoadScreen.cpp")



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
