"""Tests for the glyph placement and the load screen bar of scripts/go_ui_kit.py.

Usage:
    python -m unittest scripts/test_go_ui_kit.py
"""

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

    def test_fill_is_plain_bronze_and_the_track_a_subtle_fill(self):
        for name, token in (("LoadingBar_Progress", "bronze"), ("LoadingBar_DePowered", "fill-disabled")):
            _, _, texels = self.pieces[name]
            self.assertEqual({t for row in texels for t in row}, {self.colour(token)}, name)

    def test_back_is_a_square_frame_in_the_line_colour_on_the_field_fill(self):
        # the subtle fill tokens of the panels, so the bar sits in the menus like a field in both palettes
        line, ground = self.colour("line"), self.colour("fill-field")
        for name, left, right in (("LoadingBar_L", True, False), ("LoadingBar_C", False, False), ("LoadingBar_R", False, True)):
            w, h, texels = self.pieces[name]
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

    def test_bar_is_the_same_in_every_palette(self):
        # its own atlas outside the kit the palettes recolour, with no theme copy
        kit_names = {name for name, _, _, _ in kit.pieces()}
        self.assertFalse(kit_names & set(self.pieces))
        self.assertFalse((themes.THEME_DIR / kit.LOAD_BAR_TEXTURE_PATH.relative_to(kit.DATA)).exists())
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
