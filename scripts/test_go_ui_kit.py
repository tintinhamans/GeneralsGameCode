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


if __name__ == "__main__":
    unittest.main()
