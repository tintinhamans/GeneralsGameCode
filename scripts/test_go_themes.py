"""Tests for scripts/go_themes.py and the theme outputs of scripts/go_ui_kit.py and scripts/go_wnd_layouts.py.

Usage:
    python -m unittest scripts/test_go_themes.py
"""

import colorsys
import re
import struct
import sys
import tempfile
import unittest
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_themes as themes  # noqa: E402
import go_ui_kit as kit  # noqa: E402
import go_wnd_layouts as wl  # noqa: E402
import wnd_lint as lint  # noqa: E402
from test_go_wnd_layouts import DRAW_SLOT, layout, window  # noqa: E402

RETAIL_LOGO_INI = "SCSmShellUserInterface512.INI"  # the retail file that defines GeneralsLogo
RETAIL_RULER_INI = "HandCreatedMappedImages.INI"  # the retail file in MappedImages/HandCreated that defines MainMenuRuler
FILL_PROPS = ("ENABLEDDRAWDATA", "DISABLEDDRAWDATA", "HILITEDRAWDATA")  # the window fill slots, not gadget parts
HAS_RETAIL = all((themes.RETAIL_DIR / big).is_file() for big in (themes.RETAIL_INI_BIG, themes.RETAIL_TEXTURE_BIG))


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

    def test_radar_window_fills_take_the_fill_of_their_base_class_and_keep_alpha(self):
        seen, changed = Counter(), Counter()
        for name, text in self.base.items():
            theme = (themes.THEME_DIR / name).read_bytes().decode("latin-1")
            classes = {(win.name, tuple(win.rect)): token for win, token, slots in wl.fill_classes(text)}
            for base_win, theme_win in zip(wl.parse_windows(text).walk(), wl.parse_windows(theme).walk()):
                token = classes.get((base_win.name, tuple(base_win.rect or ())))
                if token is None:
                    continue
                seen[token] += 1
                fill = themes._rgb(themes.RADAR_FILLS[token])
                for (prop, slots), (_, theme_slots) in zip(base_win.draws, theme_win.draws):
                    color, theme_color = slots[0][1], theme_slots[0][1]
                    self.assertEqual(theme_color[3], color[3], (name, base_win.name, prop))  # retail alpha
                    retail_black = int(color[3]) > 0 and max(int(v) for v in color[:3]) <= wl.NEAR_BLACK
                    if retail_black and prop in FILL_PROPS:
                        self.assertEqual(tuple(int(v) for v in theme_color[:3]), fill, (name, base_win.name, prop))
                        changed[name] += 1
        self.assertEqual(set(seen), set(themes.RADAR_FILLS))
        self.assertGreater(sum(seen.values()), 150)
        self.assertGreater(seen["panel"], 50)
        self.assertIn("Window/Menus/OptionsMenu.wnd", changed)  # built from YAML

    def test_radar_fills_of_the_sign_in_panel_and_options(self):
        def enabled(name, window):
            text = (themes.THEME_DIR / name).read_bytes().decode("latin-1")
            win = next(w for w in wl.parse_windows(text).walk() if w.name.endswith(":" + window))
            return [int(v) for v in dict(win.draws)["ENABLEDDRAWDATA"][0][1]]
        fill = {token: list(themes._rgb(hex_color)) for token, hex_color in themes.RADAR_FILLS.items()}
        self.assertEqual(enabled("Window/Menus/GameSpyLoginProfile.wnd", "GadgetParent"), fill["panel"] + [129])
        self.assertEqual(enabled("Window/Menus/OptionsMenu.wnd", "OptionsMenuParent"), fill["panel"] + [255])
        self.assertEqual(enabled("Window/Menus/OptionsMenu.wnd", "OptionsMenuParentOld"), fill["subpanel"] + [175])
        self.assertEqual(enabled("Window/Menus/OptionsMenu.wnd", "VideoParent"), fill["deep"] + [255])
        self.assertEqual(enabled("Window/Menus/LanLobbyMenu.wnd", "ListboxGames"), fill["field"] + [126])

    def test_radar_copies_keep_no_retail_black_fill(self):
        for name in self.base:
            theme = (themes.THEME_DIR / name).read_bytes().decode("latin-1")
            self.assertEqual([w.name for w, token, slots in wl.fill_classes(theme)], [], name)


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

    def test_both_logo_inis_keep_the_logo_aspect(self):
        self.assertEqual(themes.KEEP_ASPECT, "KEEP_ASPECT")
        radar = dict(mapped_images(themes.logo_ini_text())["GeneralsLogo"])
        self.assertEqual(radar["Status"], themes.KEEP_ASPECT)
        committed = dict(mapped_images((wl.DATA / themes.BASE_LOGO_INI).read_bytes().decode("ascii"))["GeneralsLogo"])
        self.assertEqual(committed["Status"], themes.KEEP_ASPECT)

    def test_base_logo_ini_is_the_retail_definition_but_its_status(self):
        retail = {"Texture": "SCSmShellUserInterface512_001.tga", "TextureWidth": "512", "TextureHeight": "512",
                  "Coords": "Left:1 Top:1 Right:371 Bottom:121", "Status": "NONE"}
        out = themes.base_logo_ini_text(retail)
        self.assertTrue(out.endswith("End\r\n"))
        self.assertNotIn("\n", out.replace("\r\n", ""))
        expected = [(k, themes.KEEP_ASPECT if k == "Status" else v) for k, v in retail.items()]
        self.assertEqual(mapped_images(out), {"GeneralsLogo": expected})

    def test_logo_inis_load_after_the_retail_file_and_the_radar_one_last(self):
        base, radar = Path(themes.BASE_LOGO_INI), Path(themes.LOGO_INI)
        self.assertEqual(base.parent.as_posix(), "Data/INI/MappedImages/TextureSize_512")
        self.assertEqual(radar.parent, base.parent)
        self.assertEqual(Path(themes.LOGO_RETAIL_INI), base.parent / RETAIL_LOGO_INI)
        names = [RETAIL_LOGO_INI, base.name, radar.name]
        self.assertEqual(sorted(names, key=str.lower), names)
        self.assertTrue((wl.DATA / themes.BASE_LOGO_INI).is_file())
        self.assertFalse((themes.THEME_DIR / themes.BASE_LOGO_INI).exists())

    def test_keep_aspect_is_an_engine_image_status(self):
        header = (wl.ROOT / "Core/GameEngine/Include/GameClient/Image.h").read_text("latin-1")
        names = re.findall(r'"(\w+)"', re.search(r"imageStatusNames\[\]\s*=\s*\{(.*?)\};", header, re.S).group(1))
        bit = int(re.search(r"IMAGE_STATUS_KEEP_ASPECT\s*=\s*(0x[0-9A-Fa-f]+)", header).group(1), 16)
        # INI::parseBitString32 sets bit 1 << (index of the name)
        self.assertEqual(1 << names.index(themes.KEEP_ASPECT), bit)
        self.assertEqual(names[:2], ["ROTATED_90_CLOCKWISE", "RAW_TEXTURE"])

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


class LoadLogoTest(unittest.TestCase):
    def test_load_logo_texture_is_the_theme_logo_in_base_data(self):
        self.assertEqual((wl.DATA / themes.LOAD_LOGO_TGA).read_bytes(), (themes.THEME_DIR / themes.LOGO_TGA).read_bytes())
        self.assertNotEqual(themes.LOAD_LOGO_TGA, themes.LOGO_TGA)
        self.assertFalse((themes.THEME_DIR / themes.LOAD_LOGO_TGA).exists())
        self.assertFalse((themes.THEME_DIR / themes.LOAD_LOGO_INI).exists())

    def test_load_logo_ini_crops_the_image_to_the_logo_ink(self):
        data = (themes.THEME_DIR / themes.LOGO_TGA).read_bytes()
        text = themes.load_logo_ini_text(data)
        self.assertEqual((wl.DATA / themes.LOAD_LOGO_INI).read_bytes(), text.encode("ascii"))
        fields = dict(mapped_images(text)[themes.LOAD_LOGO_IMAGE])
        self.assertEqual(themes.LOAD_LOGO_IMAGE, "GOLoadLogo")
        self.assertEqual(fields["Texture"], themes.LOAD_LOGO_TEXTURE)
        tw, th = (v // themes.LOGO_SCALE for v in themes.LOGO_TEXTURE_SIZE)
        self.assertEqual((fields["TextureWidth"], fields["TextureHeight"]), (str(tw), str(th)))
        width, height, _, rows = read_tga(data)
        top_down = rows[::-1]
        xs = [x for x in range(width) if any(row[x][3] for row in top_down)]
        ys = [y for y in range(height) if any(t[3] for t in top_down[y])]
        s = themes.LOGO_SCALE
        coords = "Left:%d Top:%d Right:%d Bottom:%d" % (xs[0] // s, ys[0] // s, -(-(xs[-1] + 1) // s), -(-(ys[-1] + 1) // s))
        self.assertEqual(fields["Coords"], coords)
        self.assertNotIn("Border", fields)

    def test_load_logo_mounts_in_every_style(self):
        # GO core data: neither under Themes nor in the files the Retail style leaves out
        from test_go_ui_kit import retail_excluded_files
        excluded = retail_excluded_files()
        for rel in (themes.LOAD_LOGO_TGA, themes.LOAD_LOGO_INI):
            self.assertNotIn(rel.lower(), excluded)
            self.assertFalse(rel.lower().startswith(("themes/", "fonts/", "window/")))

    def test_the_load_screen_draws_the_load_logo_image(self):
        source = (wl.ROOT / "Core/GameEngine/Source/GameClient/GUI/LoadScreen.cpp").read_text("latin-1")
        self.assertIn(f'"{themes.LOAD_LOGO_IMAGE}"', source)



def is_retail_blue(r, g, b):
    """True for a visibly blue texel: hue between azure and violet with some saturation and brightness."""
    h, sat, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    return 210 <= h * 360 <= 270 and sat > 0.2 and v > 0.05


def mapped_images(text):
    """Return {name: [(key, value), ...]} of the MappedImage blocks of an INI text, fields in file order."""
    out, current = {}, None
    for line in text.splitlines():
        line = line.split(";")[0].strip()
        if line.startswith("MappedImage "):
            current = out.setdefault(line.split()[1], [])
        elif line == "End":
            current = None
        elif current is not None and "=" in line:
            key, value = (part.strip() for part in line.split("=", 1))
            current.append((key, value))
    return out


class RulerRecolourTest(unittest.TestCase):
    def test_retail_tones_land_on_launcher_tokens_and_alpha_stays(self):
        P = themes.PALETTE
        cases = {(0, 0, 0, 0): (P["ground"], 0), (20, 24, 77, 104): (P["line"], 104),
                 (70, 79, 191, 255): (P["bronze"], 255), (53, 67, 231, 128): (P["tan"], 128),
                 (177, 183, 255, 255): (P["sand"], 255)}
        for texel, (hex_color, alpha) in cases.items():
            self.assertEqual(themes.recolor_ruler([[texel]]), [[themes._rgb(hex_color) + (alpha,)]], texel)

    def test_read_tga_reads_rle_bottom_up_and_raw_top_down(self):
        atlas = [[(1.0, 0.0, 0.0, 1.0), (0.0, 0.0, 1.0, 0.5)], [(0.0, 1.0, 0.0, 0.0), (1.0, 1.0, 1.0, 1.0)]]
        expected = [[(255, 0, 0, 255), (0, 0, 255, 128)], [(0, 255, 0, 0), (255, 255, 255, 255)]]
        self.assertEqual(themes.read_tga(kit.tga_bytes(atlas)), (2, 2, expected))
        raw = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 2, 2, 32, 0x28)
        raw += b"".join(bytes((b, g, r, a)) for row in expected for r, g, b, a in row)
        self.assertEqual(themes.read_tga(raw), (2, 2, expected))

    def test_check_says_so_when_the_retail_archives_are_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            problems = themes.check(Path(tmp))
        self.assertTrue(any("retail" in p and "not found" in p for p in problems), problems)


@unittest.skipUnless(HAS_RETAIL, "the retail Zero Hour archives are not installed")
class RulerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.retail_ini = mapped_images(themes.read_big_entry(themes.RETAIL_DIR / themes.RETAIL_INI_BIG,
                                                              themes.RULER_RETAIL_INI).decode("latin-1"))
        retail_texture = dict(cls.retail_ini[themes.RULER_IMAGE])["Texture"]
        cls.retail = themes.read_tga(themes.read_big_entry(themes.RETAIL_DIR / themes.RETAIL_TEXTURE_BIG,
                                                           f"Art/Textures/{retail_texture}"))
        cls.theme = themes.read_tga((themes.THEME_DIR / themes.RULER_TGA).read_bytes())

    def test_ruler_ini_redefines_only_the_ruler_with_retail_fields_after_the_retail_file(self):
        path = themes.THEME_DIR / themes.RULER_INI
        self.assertEqual(path.read_bytes(), themes.ruler_ini_text(dict(self.retail_ini[themes.RULER_IMAGE])).encode("ascii"))
        theme = mapped_images(path.read_bytes().decode("ascii"))
        self.assertEqual(list(theme), ["MainMenuRuler"])
        retail = self.retail_ini["MainMenuRuler"]
        expected = [(k, themes.RULER_TEXTURE if k == "Texture" else v) for k, v in retail]
        self.assertEqual(theme["MainMenuRuler"], expected)  # same size texture, so coordinates stay 1:1
        self.assertIn(("Coords", "Left:0 Top:0 Right:800 Bottom:600"), theme["MainMenuRuler"])
        self.assertEqual((int(dict(retail)["TextureWidth"]), int(dict(retail)["TextureHeight"])), self.theme[:2])
        # HandCreated loads after TextureSize_512, and INI::loadDirectory sorts a folder case-insensitively
        self.assertEqual(Path(themes.RULER_INI).parent.as_posix(), "Data/INI/MappedImages/HandCreated")
        self.assertEqual(Path(themes.RULER_RETAIL_INI).parent.as_posix(), "Data/INI/MappedImages/HandCreated")
        self.assertGreater(Path(themes.RULER_INI).name.lower(), RETAIL_RULER_INI.lower())

    def test_ruler_texture_keeps_the_retail_size_and_alpha(self):
        self.assertEqual(self.theme[:2], self.retail[:2])
        self.assertEqual([[t[3] for t in row] for row in self.theme[2]], [[t[3] for t in row] for row in self.retail[2]])
        self.assertEqual(struct.unpack("<B", (themes.THEME_DIR / themes.RULER_TGA).read_bytes()[16:17]), (32,))

    def test_base_logo_ini_matches_the_retail_logo_but_its_status(self):
        retail = mapped_images(themes.read_big_entry(themes.RETAIL_DIR / themes.RETAIL_INI_BIG,
                                                     themes.LOGO_RETAIL_INI).decode("latin-1"))["GeneralsLogo"]
        base = mapped_images((wl.DATA / themes.BASE_LOGO_INI).read_bytes().decode("ascii"))
        self.assertEqual(list(base), ["GeneralsLogo"])
        self.assertEqual([k for k, v in base["GeneralsLogo"]], [k for k, v in retail])
        self.assertEqual([kv for kv in base["GeneralsLogo"] if kv[0] != "Status"], [kv for kv in retail if kv[0] != "Status"])
        self.assertIn(("Texture", "SCSmShellUserInterface512_001.tga"), base["GeneralsLogo"])
        self.assertIn(("Coords", "Left:1 Top:1 Right:371 Bottom:121"), base["GeneralsLogo"])

    def test_no_retail_blue_remains(self):
        def blue(rows):
            return sum(1 for row in rows for r, g, b, a in row if a and is_retail_blue(r, g, b))
        self.assertGreater(blue(self.retail[2]), 50000)  # the detector sees the retail art as blue
        self.assertEqual(blue(self.theme[2]), 0)


if __name__ == "__main__":
    unittest.main()
