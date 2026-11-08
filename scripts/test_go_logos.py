"""Tests for scripts/go_logos.py, the sign-in provider logo atlas.

Usage:
    python -m unittest scripts/test_go_logos.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_logos  # noqa: E402
import go_themes  # noqa: E402

NAMES = ("GOLogoDiscord", "GOLogoSteam", "GOLogoGameReplays")


def opaque_box(rows, left, top, size):
    """(x0, y0, x1, y1) of the texels with alpha in the size x size square at left, top, or None."""
    xs, ys = [], []
    for y in range(top, top + size):
        for x in range(left, left + size):
            if rows[y][x][3]:
                xs.append(x)
                ys.append(y)
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1) if xs else None


class LogoLayoutTest(unittest.TestCase):
    def test_one_cell_per_provider_in_the_sign_in_button_order(self):
        self.assertEqual(tuple(name for name, _ in go_logos.LOGOS), NAMES)
        self.assertEqual(go_logos.CELL, go_logos.LOGO_SIZE + 2 * go_logos.PAD)
        self.assertEqual(go_logos.ATLAS_SIZE[1], go_logos.CELL)
        self.assertLessEqual(len(NAMES) * go_logos.CELL, go_logos.ATLAS_SIZE[0])

    def test_sources_live_next_to_the_script(self):
        for _, source in go_logos.LOGOS:
            self.assertEqual(source.parent, Path(go_logos.__file__).resolve().parent / "logos")
            self.assertTrue(source.exists(), source)

    def test_ini_points_each_logo_at_its_square(self):
        text = go_logos.ini_text()
        for i, name in enumerate(NAMES):
            fields = go_themes.mapped_image(text, name)
            left = i * go_logos.CELL + go_logos.PAD
            self.assertEqual(fields["Texture"], go_logos.TEXTURE_NAME)
            self.assertEqual((fields["TextureWidth"], fields["TextureHeight"]), tuple(str(v) for v in go_logos.ATLAS_SIZE))
            self.assertEqual(fields["Coords"], f"Left:{left} Top:{go_logos.PAD} Right:{left + go_logos.LOGO_SIZE} Bottom:{go_logos.PAD + go_logos.LOGO_SIZE}")
        self.assertIn("\r\n", text)

    def test_fit_scales_the_png_into_the_square_keeping_its_aspect(self):
        width, height, rows = go_logos.fit(*go_themes.read_png(go_logos.LOGOS[2][1].read_bytes())[:3])
        self.assertEqual(height, go_logos.LOGO_SIZE)
        self.assertEqual(width, round(116 * go_logos.LOGO_SIZE / 120))
        self.assertEqual((len(rows), len(rows[0])), (height, width))

    def test_fit_averages_premultiplied_so_clear_texels_add_no_colour(self):
        red, clear = (255, 0, 0, 255), (0, 255, 0, 0)
        width, height, rows = go_logos.fit(2, 2, [[red, clear], [clear, red]], 1)
        self.assertEqual((width, height), (1, 1))
        r, g, b, a = rows[0][0]
        self.assertEqual((r, g, b), (255, 0, 0))
        self.assertEqual(a, 128)


class CommittedAtlasTest(unittest.TestCase):
    def setUp(self):
        self.width, self.height, self.rows = go_themes.read_tga(go_logos.TEXTURE_PATH.read_bytes())

    def test_committed_ini_matches_the_script(self):
        self.assertEqual(go_logos.INI_PATH.read_bytes(), go_logos.ini_text().encode("ascii"))

    def test_atlas_size(self):
        self.assertEqual((self.width, self.height), go_logos.ATLAS_SIZE)

    def test_every_logo_has_ink_inside_its_square_and_a_clear_pad(self):
        for i, name in enumerate(NAMES):
            left = i * go_logos.CELL
            box = opaque_box(self.rows, left, 0, go_logos.CELL)
            self.assertIsNotNone(box, name)
            x0, y0, x1, y1 = box
            inner = (left + go_logos.PAD, go_logos.PAD, left + go_logos.PAD + go_logos.LOGO_SIZE, go_logos.PAD + go_logos.LOGO_SIZE)
            self.assertTrue(inner[0] <= x0 and inner[1] <= y0 and x1 <= inner[2] and y1 <= inner[3], f"{name} {box}")
            # the marks fill their square: the larger side reaches most of it
            self.assertGreaterEqual(max(x1 - x0, y1 - y0), go_logos.LOGO_SIZE * 3 // 4, f"{name} {box}")

    def test_discord_and_steam_marks_are_white(self):
        for i in (0, 1):
            left = i * go_logos.CELL
            ink = [t for row in self.rows for t in row[left:left + go_logos.CELL] if t[3] == 255]
            self.assertTrue(ink, NAMES[i])
            self.assertTrue(all(t[:3] == (255, 255, 255) for t in ink), NAMES[i])

    def test_gamereplays_keeps_its_colours(self):
        left = 2 * go_logos.CELL
        ink = {t[:3] for row in self.rows for t in row[left:left + go_logos.CELL] if t[3] == 255}
        self.assertGreater(len(ink), 8)


if __name__ == "__main__":
    unittest.main()
