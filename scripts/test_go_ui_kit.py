"""Tests for the glyph placement of scripts/go_ui_kit.py.

Usage:
    python -m unittest scripts/test_go_ui_kit.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

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

    def test_scroll_cell_highlights_are_square_and_centred(self):
        for name in self.named("GOScrollCell"):
            w, h, texels = self.pieces[name]
            x0, y0, x1, y1 = opaque_box(texels)
            colour = texels[y0][x0]
            for row in texels[y0:y1]:
                for t in row[x0:x1]:
                    self.assertEqual(t, colour, name)
            self.assertEqual((x0, y0), (w * kit.SCALE - x1, h * kit.SCALE - y1), name)

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


if __name__ == "__main__":
    unittest.main()
