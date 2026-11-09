"""Tests for the GO UI colour table, GenOnline/GameData/Data/INI/GOUIColors.ini and its Radar copy.

The engine reads the table in Core/GameEngine/Source/Common/GOUIColors.cpp; the Radar copy replaces the base file by
path when the Radar palette is mounted, and the Retail style leaves the table out.

Usage:
    python -m unittest scripts/test_go_colors.py
"""

import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_themes as themes  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "GenOnline" / "GameData"
SIGNAL_INI = DATA / "Data" / "INI" / "GOUIColors.ini"
RADAR_INI = DATA / "Themes" / "Radar" / "Data" / "INI" / "GOUIColors.ini"
SOURCE = ROOT / "Core" / "GameEngine" / "Source" / "Common" / "GOUIColors.cpp"
HEADER = ROOT / "Core" / "GameEngine" / "Include" / "Common" / "GOUIColors.h"
ARCHIVE_SOURCE = ROOT / "Core" / "GameEngine" / "Source" / "Common" / "System" / "ArchiveFileSystem.cpp"

# roles that override the OnlineChatColors of multiplayer.ini: the Signal table leaves them out, so the base look keeps
# whatever multiplayer.ini (retail or a data pack) sets
CHAT_ROLES = {"ChatDefault", "PlayerNormal", "LobbyRow", "Buddy"}

# roles drawn as text, which need 4.5:1 on the field fill
TEXT_ROLES = {"Text", "Muted", "Disabled", "Help", "Hilite", "Notice", "Error", "Success", "Admin", "Offline",
              "LobbyJoinable", "LobbyNotJoinable", "LobbyAltRow", "LobbyAlmostFull"} | CHAT_ROLES

# the colours each role's sites draw today, without the table: a set when the sites disagree
TODAY = {
    "Text": {"#FFFFFF"},                    # RoomsInterface.h default lines, MapSelectList names, TOS, ProxyLogin
    "Muted": {"#C0C0C0"},                   # system and inform lines, map details, QuickMatch countdown
    "Disabled": {"#7F7F7F"},                # name change lines
    "Help": {"#7F7F7F"},                    # /help lines
    "Hilite": {"#FFFF00"},                  # /net lines
    "Line": {"#2F4BFF"},                    # retail blue layout borders at the kit line weight
    "Notice": {"#FFC20F", "#FFC219", "#FFBE00"},
    "Error": {"#FF5E5E", "#FF0000", "#FF5050", "#FF6060", "#FF6464"},
    "Success": {"#00FF00"},
    "Admin": {"#00A2E8"},
    "Offline": {"#648296"},
    "TooltipFill": {"#141414"},             # retail Mouse.ini TooltipBackgroundColor
    "TooltipBorder": {"#3C3C9B"},           # retail Mouse.ini TooltipBorderColor
    "TooltipText": {"#DCDCDC"},             # retail Mouse.ini TooltipTextColor
    "TooltipSweep": {"#FFFF00"},            # retail Mouse.ini TooltipHighlightColor
    "FieldFill": {"#05070F"},               # open combo drop-down backing
    "LobbyJoinable": {"#14B1FF"},           # lobby rows that hold a buddy
    "LobbyNotJoinable": {"#006282"},
    "LobbyAltRow": {"#BFC6C9"},
    "LobbyAlmostFull": {"#10AD90"},
    "ConnectionSpinner": {"#2F37A8"},
}

# the Signal values the colour audit changed: one value per duplicated role, and three readability or kit fixes
SIGNAL_FIXES = {
    "Notice": "#FFC20F",
    "Error": "#FF5E5E",
    "TooltipBorder": "#2F4BFF",
    "ConnectionSpinner": "#2F4BFF",
    "LobbyNotJoinable": "#3A88AD",
}

# the Radar palette token of each role
RADAR_TOKENS = {
    "Text": "text", "Muted": "muted", "Disabled": "legal", "Help": "legal", "Hilite": "tan", "Line": "bronze",
    "Notice": "tan", "Error": "danger", "Success": "green", "Admin": "sand", "Offline": "empty",
    "TooltipFill": "fill-block", "TooltipBorder": "bronze", "TooltipText": "text", "TooltipSweep": "tan",
    "FieldFill": "fill-field", "LobbyJoinable": "tan", "LobbyNotJoinable": "bronze", "LobbyAltRow": "muted",
    "LobbyAlmostFull": "green", "ConnectionSpinner": "bronze",
    "ChatDefault": "text", "PlayerNormal": "text", "LobbyRow": "text", "Buddy": "tan",
}


def parse_table(path):
    """{role: "#RRGGBB"} of the GOUIColors block, alpha must be 255 or left out."""
    colours = {}
    inside = False
    for raw in path.read_text("latin-1").splitlines():
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        if not inside:
            if line.split()[0].lower() == "gouicolors":
                inside = True
            continue
        if line.lower() == "end":
            inside = False
            continue
        name, _, value = line.partition("=")
        parts = dict(p.split(":") for p in value.split())
        if parts.get("A", "255") != "255":
            raise AssertionError(f"{path.name}: {name.strip()} is not opaque")
        colours[name.strip()] = "#%02X%02X%02X" % (int(parts["R"]), int(parts["G"]), int(parts["B"]))
    return colours


def code_roles():
    """The role names of the engine's table, in GOUIColors.cpp."""
    source = SOURCE.read_text("latin-1")
    body = source[source.index("s_roleNames[]"):]
    return re.findall(r'"([^"]+)"', body[:body.index("};")])


def enum_name(role):
    """GOUIColorRole enumerator suffix of a role key: LobbyNotJoinable -> LOBBY_NOT_JOINABLE."""
    return re.sub(r"(?<!^)(?=[A-Z])", "_", role).upper()


def luminance(hex_colour):
    channels = []
    for i in (1, 3, 5):
        c = int(hex_colour[i:i + 2], 16) / 255.0
        channels.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2]


def contrast(a, b):
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


class GOUIColorsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.signal = parse_table(SIGNAL_INI)
        cls.radar = parse_table(RADAR_INI)
        cls.roles = code_roles()

    def test_code_roles_match_the_test_tables(self):
        self.assertEqual(set(self.roles), set(RADAR_TOKENS))
        self.assertEqual(set(self.roles) - CHAT_ROLES, set(TODAY))
        self.assertEqual(len(self.roles), len(set(self.roles)))

    def test_role_keys_follow_the_enum_order(self):
        header = HEADER.read_text("latin-1")
        enum = re.findall(r"^\s*GOUICOLOR_(\w+)", header[header.index("enum GOUIColorRole"):], re.M)
        self.assertEqual(enum[-1], "COUNT")
        self.assertEqual(enum[:-1], [enum_name(role) for role in self.roles])

    def test_every_role_is_read_by_a_site(self):
        sources = [p for d in ("Core", "GeneralsMD/Code") for p in (ROOT / d).rglob("*.[ch]*")
                   if p.suffix in (".cpp", ".h") and p.stem != "GOUIColors"]
        text = "\n".join(p.read_text("latin-1") for p in sources)
        for role in self.roles:
            self.assertIn(f"GOUICOLOR_{enum_name(role)}", text, role)

    def test_every_role_is_present_in_both_tables(self):
        self.assertEqual(set(self.radar), set(self.roles))
        self.assertEqual(set(self.signal), set(self.roles) - CHAT_ROLES)

    def test_signal_leaves_the_chat_colours_to_multiplayer_ini(self):
        self.assertFalse(CHAT_ROLES & set(self.signal))

    def test_radar_values_are_palette_tokens(self):
        for role, token in RADAR_TOKENS.items():
            self.assertEqual(self.radar[role], themes.PALETTE[token].upper(), role)

    def test_signal_keeps_todays_colours_but_the_audit_fixes(self):
        for role, today in TODAY.items():
            expected = SIGNAL_FIXES.get(role)
            if expected is None:
                self.assertEqual(len(today), 1, role)
                expected = next(iter(today))
            self.assertEqual(self.signal[role], expected, role)

    def test_fixed_values_are_one_of_todays_or_the_kit_line(self):
        for role, value in SIGNAL_FIXES.items():
            if len(TODAY[role]) > 1:
                self.assertIn(value, TODAY[role], role)
            else:
                self.assertNotIn(value, TODAY[role], role)

    def test_text_roles_read_on_the_field_fill(self):
        for name, table in (("Signal", self.signal), ("Radar", self.radar)):
            for role in TEXT_ROLES & set(table):
                for ground in (table["FieldFill"], "#000000"):
                    self.assertGreaterEqual(contrast(table[role], ground), 4.5, f"{name} {role} on {ground}")

    def test_tooltip_text_reads_on_the_tooltip_fill(self):
        for name, table in (("Signal", self.signal), ("Radar", self.radar)):
            for role in ("TooltipText", "TooltipSweep"):
                self.assertGreaterEqual(contrast(table[role], table["TooltipFill"]), 4.5, f"{name} {role}")

    def test_retail_style_leaves_the_table_out(self):
        source = ARCHIVE_SOURCE.read_text("latin-1")
        body = source[source.index("static Bool isNotGenOnlineInterfaceFile"):]
        start = body.index("files[] = {")
        excluded = set(re.findall(r'"([^"]+)"', body[start:body.index("};", start)]))
        self.assertIn(SIGNAL_INI.relative_to(DATA).as_posix().lower(), excluded)


if __name__ == "__main__":
    unittest.main()
