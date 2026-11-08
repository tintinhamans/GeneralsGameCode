"""Tests for scripts/go_themes.py and the theme outputs of scripts/go_ui_kit.py and scripts/go_wnd_layouts.py.

Usage:
    python -m unittest scripts/test_go_themes.py
"""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_themes as themes  # noqa: E402
import go_ui_kit as kit  # noqa: E402
import go_wnd_layouts as wl  # noqa: E402
import wnd_lint as lint  # noqa: E402
from test_go_wnd_layouts import DRAW_SLOT, layout, window  # noqa: E402

RETAIL_LOGO_INI = "SCSmShellUserInterface512.INI"  # the retail file that defines GeneralsLogo


def read_tga(data):
    """Return (width, height, bits, rows of (b, g, r, a) bytes, bottom row first) of a type 2 or type 10 TGA."""
    id_len, _, kind = data[0], data[1], data[2]
    width, height, bits = struct.unpack("<HHB", data[12:17])
    texels, pos, total = [], 18 + id_len, width * height
    while len(texels) < total:
        if kind == 2:
            texels.append(data[pos:pos + 4])
            pos += 4
            continue
        head = data[pos]
        pos += 1
        count = (head & 0x7F) + 1
        if head & 0x80:
            texels += [data[pos:pos + 4]] * count
            pos += 4
        else:
            texels += [data[pos + 4 * i:pos + 4 * i + 4] for i in range(count)]
            pos += 4 * count
    return width, height, bits, [texels[y * width:(y + 1) * width] for y in range(height)]


def base_menus():
    """Committed base layouts that get a theme copy, by archive path."""
    out = {}
    for p in sorted((wl.DATA / "Window").rglob("*.wnd")):
        name = p.relative_to(wl.DATA).as_posix()
        if wl.themed(name):
            out[name] = p.read_bytes().decode("latin-1")
    return out


def shape(text):
    """Everything about a layout but its colours: per window the name, kind, rect, status, header, font and images."""
    out = []
    for win in wl.parse_windows(text).walk():
        images = [(prop, [slot[0] for slot in slots]) for prop, slots in win.draws]
        out.append((win.name, win.kind, win.rect, sorted(win.status), win.header, win.font_size, images,
                    win.centered and win.centered[0]))
    return out


class LayoutPaletteTest(unittest.TestCase):
    def test_listed_colours_map_and_alpha_stays(self):
        draw = [
            "IMAGE: NoImage, COLOR: 47 75 255 200, BORDERCOLOR: 106 123 255 77",
            "IMAGE: NoImage, COLOR: 10 20 30 255, BORDERCOLOR: 47 75 255 0",
        ] + [DRAW_SLOT] * 7
        text = layout(window("PUSHBUTTON", (0, 0, 100, 20), draw=draw))
        text = text.replace("HILITE:   128 128 255 255", "HILITE:   186 255 12 255")
        text = text.replace("DISABLED: 255 255 255 255", "DISABLED: 192 192 192 128")
        out = wl.theme_layout(wl.remap_colors(text))  # the theme recolours the final base layout
        self.assertIn("ENABLED:  242 237 224 255", out)          # white text -> text
        self.assertIn("DISABLED: 143 137 124 128", out)          # disabled grey -> legal
        self.assertIn("HILITE:   227 186 108 255", out)          # retail yellow-green hilite -> tan
        self.assertIn("ENABLEDBORDER:  191 144 72 255", out)     # retail blue line -> bronze
        self.assertIn("COLOR: 191 144 72 200, BORDERCOLOR: 227 186 108 77", out)  # kit line and hilite
        self.assertIn("COLOR: 10 20 30 255, BORDERCOLOR: 191 144 72 0", out)     # unlisted colour stays
        self.assertNotIn("47 75 255", out)

    def test_theme_colours_come_from_the_launcher_palette(self):
        palette = {v.upper() for v in themes.PALETTE.values()}
        for theme in themes.KIT_COLORS.values():
            self.assertIn(theme.upper(), palette)
        for theme in themes.LAYOUT_COLORS.values():
            self.assertIn("#%02X%02X%02X" % theme, palette)

    def test_listed_kit_colours(self):
        listed = {"#2F4BFF": "#BF9048", "#6A7BFF": "#E3BA6C", "#8C9BFF": "#F7DB9C", "#4A4E6E": "#2C3B3F",
                  "#070A18": "#141C1F", "#152078": "#223034", "#0A0A10": "#0B1114",
                  "#C9CEFF": "#F2EDE0", "#FFFFFF": "#F7DB9C", "#5D6080": "#6F7A7C"}
        for kit_color, theme in listed.items():
            self.assertEqual(themes.KIT_COLORS[kit_color], theme, kit_color)


class LayoutCopyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = base_menus()
        cls.theme = {name: wl.theme_layout(text) for name, text in cls.base.items()}

    def test_theme_copy_changes_only_colour_numbers(self):
        self.assertIn("Window/Menus/OptionsMenu.wnd", self.base)  # built from YAML
        self.assertIn("Window/Menus/MainMenu.wnd", self.base)
        for name, text in self.base.items():
            out = self.theme[name]
            self.assertEqual(shape(out), shape(text), name)
            self.assertEqual(wl.structure_counts(out), wl.structure_counts(text), name)
            strip = lambda t: wl.THEME_COLOR_RE.sub(lambda m: m["key"] + ":", t)  # noqa: E731
            self.assertEqual(strip(out), strip(text), name)
            self.assertEqual(out.count("\r\n"), text.count("\r\n"), name)
            out.encode("latin-1")

    def test_main_menu_and_options_change_colour(self):
        for name in ("Window/Menus/MainMenu.wnd", "Window/Menus/OptionsMenu.wnd"):
            self.assertNotEqual(self.theme[name], self.base[name], name)

    def test_committed_theme_copies_match_and_in_game_layouts_have_none(self):
        written = {p.relative_to(themes.THEME_DIR).as_posix(): p for p in (themes.THEME_DIR / "Window").rglob("*.wnd")}
        self.assertEqual(set(written), set(self.base))
        for name, path in written.items():
            self.assertEqual(path.read_bytes(), self.theme[name].encode("latin-1"), name)
        for name in ("Window/ControlBar.wnd", "Window/InGameChat.wnd", "Window/Menus/QuitMenu.wnd", "Window/Menus/Victorious.wnd"):
            self.assertFalse(wl.themed(name), name)
            self.assertNotIn(name, written)
        self.assertTrue(wl.themed("Window/Menus/MainMenu.wnd"))

    def test_lint_finds_nothing_new_in_theme_copies(self):
        def findings(texts):
            return sorted((f.rule, f.file, f.name, f.detail) for f in lint.lint_layouts(texts))
        self.assertEqual(findings(self.theme), findings(self.base))


class ThemeFilesTest(unittest.TestCase):
    def test_every_theme_file_has_a_base_counterpart_but_the_additions(self):
        files = [p.relative_to(themes.THEME_DIR).as_posix() for p in themes.THEME_DIR.rglob("*") if p.is_file()]
        self.assertTrue(files)
        for rel in files:
            if rel in themes.ADDITIONS:
                self.assertFalse((wl.DATA / rel).exists(), rel)
            else:
                self.assertTrue((wl.DATA / rel).is_file(), rel)
        self.assertTrue(set(themes.ADDITIONS) <= set(files))

    def test_check_passes_on_the_committed_outputs(self):
        self.assertEqual(themes.check(), [])


class KitThemeTest(unittest.TestCase):
    def test_kit_colour_maps_by_rgb_and_keeps_alpha(self):
        self.assertEqual(kit.recolor(kit.rgba("#2F4BFF", 0.82), themes.KIT_COLORS), kit.rgba("#BF9048", 0.82))
        self.assertEqual(kit.recolor(kit.TRANSPARENT, themes.KIT_COLORS), kit.TRANSPARENT)
        with self.assertRaises(SystemExit):
            kit.recolor(kit.rgba("#123456"), themes.KIT_COLORS)

    def test_theme_atlas_keeps_every_piece_and_coordinate(self):
        texture, ini = kit.build(palette=themes.KIT_COLORS)
        self.assertEqual(ini.encode("ascii"), kit.INI_PATH.read_bytes())
        self.assertFalse((themes.THEME_DIR / kit.INI_PATH.relative_to(wl.DATA)).exists())
        base = read_tga(kit.TEXTURE_PATH.read_bytes())
        theme = read_tga(texture)
        self.assertEqual(base[:3], theme[:3])
        self.assertEqual([[t[3] for t in row] for row in theme[3]], [[t[3] for t in row] for row in base[3]])
        self.assertNotEqual(theme[3], base[3])
        # a kit colour the theme build missed would leave whole piece interiors in that exact colour
        kit_rgb = {bytes.fromhex(k[5:7] + k[3:5] + k[1:3]) for k in themes.KIT_COLORS}  # TGA stores b, g, r
        self.assertFalse({t[:3] for row in theme[3] for t in row if t[3]} & kit_rgb)
        self.assertEqual(kit.THEME_TEXTURE_PATH.read_bytes(), texture)


class LogoTest(unittest.TestCase):
    def test_logo_ini_redefines_the_retail_logo_after_the_retail_file(self):
        path = themes.THEME_DIR / themes.LOGO_INI
        self.assertEqual(path.read_bytes(), themes.logo_ini_text().encode("ascii"))
        text = path.read_bytes().decode("ascii")
        self.assertIn("MappedImage GeneralsLogo\r\n", text)
        self.assertIn(f"  Texture = {themes.LOGO_TEXTURE}\r\n", text)
        self.assertIn("  Coords = Left:0 Top:0 Right:370 Bottom:120\r\n", text)
        # INI::loadDirectory loads a folder in case-insensitive name order, so the theme file must sort after retail's
        self.assertEqual(Path(themes.LOGO_INI).parent.as_posix(), "Data/INI/MappedImages/TextureSize_512")
        self.assertGreater(Path(themes.LOGO_INI).name.lower(), RETAIL_LOGO_INI.lower())

    def test_logo_texture_is_a_32_bit_power_of_two_tga_inside_the_logo_rect(self):
        width, height, bits, rows = read_tga((themes.THEME_DIR / themes.LOGO_TGA).read_bytes())
        self.assertEqual(bits, 32)
        self.assertEqual(width & (width - 1), 0)
        self.assertEqual(height & (height - 1), 0)
        self.assertEqual((width, height), themes.LOGO_TEXTURE_SIZE)
        rw, rh = (v * themes.LOGO_SCALE for v in themes.LOGO_RECT)
        top_down = rows[::-1]
        inside = [t[3] for row in top_down[:rh] for t in row[:rw]]
        outside = [t[3] for row in top_down for t in row[rw:]] + [t[3] for row in top_down[rh:] for t in row]
        self.assertGreater(max(inside), 0)
        self.assertEqual(max(outside, default=0), 0)
        # centred: the empty columns on the left and right differ by at most one texel
        columns = [x for x in range(rw) if any(top_down[y][x][3] for y in range(rh))]
        self.assertLessEqual(abs(columns[0] - (rw - 1 - columns[-1])), 1)


if __name__ == "__main__":
    unittest.main()
