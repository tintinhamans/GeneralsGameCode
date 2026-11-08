"""Tests for scripts/go_cursor.py, its committed outputs and the Hand cursor in the engine sources.

Usage:
    python -m unittest scripts/test_go_cursor.py
"""

import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import go_cursor as cursor  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MOUSE_H = ROOT / "Core" / "GameEngine" / "Include" / "GameClient" / "Mouse.h"
MOUSE_CPP = ROOT / "Core" / "GameEngine" / "Source" / "GameClient" / "Input" / "Mouse.cpp"


def chunks(data, start, end):
    """Yield (id, payload, offset) of the RIFF chunks between start and end."""
    pos = start
    while pos + 8 <= end:
        cid = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        yield cid, data[pos + 8:pos + 8 + size], pos
        pos += 8 + size + (size & 1)


def parse_ani(data):
    """Return (anih fields, list of (hotspot, width, height, bits, xor rows, and rows)) of an ACON RIFF file."""
    assert data[:4] == b"RIFF" and data[8:12] == b"ACON", "not a RIFF ACON file"
    assert struct.unpack("<I", data[4:8])[0] == len(data) - 8, "RIFF size does not match the file"
    anih, frames = None, []
    for cid, payload, _ in chunks(data, 12, len(data)):
        if cid == b"anih":
            anih = struct.unpack("<9I", payload)
        elif cid == b"LIST" and payload[:4] == b"fram":
            for fid, icon, _ in chunks(payload, 4, len(payload)):
                assert fid == b"icon"
                reserved, kind, count = struct.unpack("<HHH", icon[:6])
                assert (reserved, kind, count) == (0, 2, 1), "frame is not a one image .cur"
                w, h, _, _, hx, hy, size, offset = struct.unpack("<BBBBHHII", icon[6:22])
                assert offset + size == len(icon)
                header = struct.unpack("<IiiHHIIiiII", icon[offset:offset + 40])
                assert header[0] == 40 and (header[1], header[2]) == (w, 2 * h)
                frames.append(((hx, hy), w, h, header[4], icon[offset + 40:]))
    return anih, frames


def enum_cursors(text):
    """Return the MouseCursor enumerators from FIRST_CURSOR's successor to NUM_MOUSE_CURSORS, ifdef blocks kept."""
    body = re.search(r"enum MouseCursor\s*\{(.*?)\};", text, re.S).group(1)
    names = []
    for line in body.splitlines():
        line = line.split("//")[0].strip()
        if line.startswith("#"):
            names.append(line)
            continue
        for token in filter(None, (t.strip() for t in line.split(","))):
            name = token.split("=")[0].strip()
            if name and name not in ("INVALID_MOUSE_CURSOR", "FIRST_CURSOR", "NUM_MOUSE_CURSORS"):
                names.append(name)
    return names


def ini_names(text):
    """Return the CursorININames entries, ifdef lines kept."""
    body = re.search(r"CursorININames\[NUM_MOUSE_CURSORS\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    names = []
    for line in body.splitlines():
        line = line.split("//")[0].strip()
        if line.startswith("#"):
            names.append(line)
        names += re.findall(r'"([^"]*)"', line)
    return names


class AniTest(unittest.TestCase):
    def setUp(self):
        self.data = cursor.build_ani()

    def test_structure_matches_retail_pointer(self):
        anih, frames = parse_ani(self.data)
        # cbSize, frames, steps, cx, cy, bit count, planes, jiffies, flags (AF_ICON) as in sccpointer.ani
        self.assertEqual(anih, (36, 1, 1, 0, 0, 4, 1, 10, 1))
        self.assertEqual(len(frames), 1)
        hotspot, w, h, bits, _ = frames[0]
        self.assertEqual((w, h, bits), (32, 32, 4))
        self.assertEqual(hotspot, cursor.HOTSPOT)

    def test_hotspot_is_the_fingertip(self):
        image = cursor.build_image()
        x, y = cursor.HOTSPOT
        self.assertEqual(image[y][x], "K", "the hotspot must sit on the fingertip outline")
        # nothing is drawn above the fingertip row, and the hotspot is the middle of that row's outline
        self.assertTrue(all(c == "." for row in image[:y] for c in row))
        tip = image[y].strip(".")
        self.assertEqual(set(tip), {"K"})
        self.assertEqual(x, (image[y].index("K") + image[y].rindex("K")) // 2)

    def test_palette_is_the_retail_vga_palette(self):
        icon = next(p for cid, p, _ in chunks(self.data, 12, len(self.data)) if cid == b"LIST" and p[:4] == b"fram")
        bmp = icon[4 + 8 + 22:]
        palette = [bmp[40 + 4 * i:43 + 4 * i][::-1].hex() for i in range(16)]
        self.assertEqual(palette, cursor.VGA_PALETTE)

    def test_tga_matches_the_ani_pixels(self):
        tga = cursor.build_tga()
        self.assertEqual(tga[2], 2)
        self.assertEqual(struct.unpack("<HHBB", tga[12:18]), (32, 32, 32, 0x08))
        image = cursor.build_image()
        body = tga[18:]
        for y in range(32):
            for x in range(32):
                b, g, r, a = body[((31 - y) * 32 + x) * 4:((31 - y) * 32 + x) * 4 + 4]
                if image[y][x] == ".":
                    self.assertEqual(a, 0)
                else:
                    self.assertEqual(a, 255)
                    self.assertEqual(bytes((r, g, b)).hex(), cursor.VGA_PALETTE[cursor.COLOR_INDEX[image[y][x]]])

    def test_build_is_deterministic(self):
        self.assertEqual(cursor.build_ani(), cursor.build_ani())
        self.assertEqual(cursor.build_tga(), cursor.build_tga())

    def test_check_passes_on_committed_files(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts" / "go_cursor.py"), "--check"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_check_reports_missing_and_stale_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            saved = cursor.DATA, cursor.ROOT
            cursor.DATA = cursor.ROOT = Path(tmp)
            try:
                self.assertEqual(len(cursor.check()), 4)
                for path, data in cursor.outputs():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
                self.assertEqual(cursor.check(), [])
                (Path(tmp) / cursor.ANI).write_bytes(b"RIFF")
                self.assertEqual(cursor.check(), [f"out of date: {cursor.ANI}"])
            finally:
                cursor.DATA, cursor.ROOT = saved

    def test_mouse_ini_defines_hand(self):
        text = (cursor.DATA / cursor.MOUSE_INI).read_text("ascii")
        self.assertRegex(text, r"(?m)^MouseCursor Hand$")
        self.assertIn(f"Texture             = {cursor.TEXTURE}", text)
        self.assertIn(f"HotSpot             = X:{cursor.HOTSPOT[0]} Y:{cursor.HOTSPOT[1]}", text)


class EnumTest(unittest.TestCase):
    def test_hand_is_appended_and_names_stay_aligned(self):
        enum = enum_cursors(MOUSE_H.read_text("latin-1"))
        names = ini_names(MOUSE_CPP.read_text("latin-1"))
        self.assertEqual(enum[-1], "HAND")
        self.assertEqual(names[-1], "Hand")
        self.assertEqual(len(enum), len(names), "enum and CursorININames differ in length")
        for e, n in zip(enum, names):
            if e.startswith("#") or n.startswith("#"):
                self.assertEqual(e, n, "ifdef blocks differ")
        # the retail indices before Hand stay unchanged
        plain = [n for n in names if not n.startswith("#")]
        self.assertEqual(plain[:4], ["None", "Normal", "Arrow", "Scroll"])
        self.assertEqual(plain[-2:], ["ParticleUplinkCannon", "Hand"])


if __name__ == "__main__":
    unittest.main()
