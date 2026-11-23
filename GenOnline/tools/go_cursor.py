#!/usr/bin/env python3
"""Generals Online Hand cursor: the pointing hand the shell menus show over clickable controls.

The art follows the retail UI pointer Data/Cursors/sccpointer.ani: a 32x32 one frame animated cursor with a 4 bit
image in the 16 colour VGA palette, a black outline, a white body shaded towards the bottom right in silver and grey,
and the art hanging right and down from the hotspot at 13,13, here the tip of the index finger.

Outputs (GenOnline/data/core):
    Data/Cursors/GeneralsOnline_CursorHand.ani      the Windows cursor (the default CursorMode)
    Art/Textures/GeneralsOnline_CursorHand.tga      the same image for the DX8 and polygon cursor modes
    Data/INI/Mouse/GeneralsOnline_Cursor.ini        the MouseCursor Hand definition, loaded after Data/INI/Mouse.ini
    Data/INI/MappedImages/TextureSize_512/ZZ_GeneralsOnline_Cursor.INI   the polygon mode image

Usage:
    python GenOnline/tools/go_cursor.py           write the files
    python GenOnline/tools/go_cursor.py --check   exit 1 when a file is missing or out of date
"""

import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "GenOnline" / "data" / "core"

TEXTURE = "GeneralsOnline_CursorHand"
ANI = f"Data/Cursors/{TEXTURE}.ani"
TGA = f"Art/Textures/{TEXTURE}.tga"
MOUSE_INI = "Data/INI/Mouse/GeneralsOnline_Cursor.ini"
IMAGE_INI = "Data/INI/MappedImages/TextureSize_512/ZZ_GeneralsOnline_Cursor.INI"

SIZE = 32
HOTSPOT = (13, 13)  # the retail pointer's hotspot, here the fingertip

# the palette of sccpointer.ani, RGB
VGA_PALETTE = ["000000", "800000", "008000", "808000", "000080", "800080", "008080", "c0c0c0",
               "808080", "ff0000", "00ff00", "ffff00", "0000ff", "ff00ff", "00ffff", "ffffff"]
# K outline, G shadow edge, S shading next to the shadow, W body; the retail pointer uses the same four
COLOR_INDEX = {"K": 0, "G": 8, "S": 7, "W": 15}

# K outline, # body (shaded by shade()), . transparent; the fingertip is the middle of the first row's outline
HAND = [
    "......KKK.........",
    ".....K###K........",
    ".....K###K........",
    ".....K###K........",
    ".....K###KKK......",
    ".....K###K##KKK...",
    ".....K###K##K##KK.",
    ".....K###K##K##K#K",
    ".KK..K###K##K##K#K",
    "K##K.K###########K",
    "K###KK###########K",
    ".K###############K",
    ".K###############K",
    "..K##############K",
    "..K#############K.",
    "...K############K.",
    "....K##########K..",
    "....K##########K..",
    "....KKKKKKKKKKKK..",
]


def shade(art):
    """Shade the body: grey where the outline is right or below, silver one step in from that, white elsewhere."""
    h, w = len(art), len(art[0])

    def body(x, y):
        return 0 <= x < w and 0 <= y < h and art[y][x] == "#"

    out = []
    for y in range(h):
        row = ""
        for x in range(w):
            c = art[y][x]
            if c != "#":
                row += c
            elif not body(x + 1, y) or not body(x, y + 1):
                row += "G"
            elif (not body(x + 2, y) or not body(x, y + 2)) and body(x - 1, y) and body(x, y - 1):
                row += "S"
            else:
                row += "W"
        out.append(row)
    return out


def build_image():
    """Return the 32x32 cursor as rows of K/G/S/W/. with the fingertip at HOTSPOT."""
    art = shade(HAND)
    tip = (art[0].index("K") + art[0].rindex("K")) // 2
    left, top = HOTSPOT[0] - tip, HOTSPOT[1]
    if left < 0 or left + len(art[0]) > SIZE or top + len(art) > SIZE:
        raise SystemExit("the hand does not fit the cursor")
    image = [["."] * SIZE for _ in range(SIZE)]
    for y, row in enumerate(art):
        for x, c in enumerate(row):
            image[top + y][left + x] = c
    return ["".join(r) for r in image]


def cur_bytes(image):
    """One image .cur (type 2) with a 4 bit XOR bitmap and the AND mask, bottom row first like sccpointer.ani."""
    xor_stride, and_stride = SIZE // 2, SIZE // 8
    xor, mask = bytearray(), bytearray()
    for row in reversed(image):
        for x in range(0, SIZE, 2):
            hi = COLOR_INDEX.get(row[x], 0)
            lo = COLOR_INDEX.get(row[x + 1], 0)
            xor.append((hi << 4) | lo)
        bits = 0
        for x in range(SIZE):
            bits = (bits << 1) | (1 if row[x] == "." else 0)
        mask += bits.to_bytes(and_stride, "big")
    assert len(xor) == xor_stride * SIZE
    palette = b"".join(bytes.fromhex(c)[::-1] + b"\0" for c in VGA_PALETTE)
    # biSizeImage as the retail pointer writes it: the XOR and AND bitmaps
    header = struct.pack("<IiiHHIIiiII", 40, SIZE, 2 * SIZE, 1, 4, 0, len(xor) + len(mask), 0, 0, 16, 0)
    bitmap = header + palette + bytes(xor) + bytes(mask)
    entry = struct.pack("<BBBBHHII", SIZE, SIZE, 0, 0, HOTSPOT[0], HOTSPOT[1], len(bitmap), 22)
    return struct.pack("<HHH", 0, 2, 1) + entry + bitmap


def chunk(cid, payload):
    return cid + struct.pack("<I", len(payload)) + payload + (b"\0" if len(payload) & 1 else b"")


def build_ani():
    """The cursor as a one frame ACON RIFF file laid out like sccpointer.ani."""
    info = chunk(b"LIST", b"INFO" + chunk(b"INAM", b"Hand\0") + chunk(b"IART", b"Generals Online\0"))
    # cbSize, frames, steps, cx, cy, bit count, planes, jiffies per frame, AF_ICON
    anih = chunk(b"anih", struct.pack("<9I", 36, 1, 1, 0, 0, 4, 1, 10, 1))
    frames = chunk(b"LIST", b"fram" + chunk(b"icon", cur_bytes(build_image())))
    body = b"ACON" + info + anih + frames
    return b"RIFF" + struct.pack("<I", len(body)) + body


def build_tga():
    """The cursor as an uncompressed 32 bit TGA, bottom row first, alpha from the AND mask."""
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, SIZE, SIZE, 32, 0x08)
    body = bytearray()
    for row in reversed(build_image()):
        for c in row:
            if c == ".":
                body += b"\0\0\0\0"
            else:
                body += bytes.fromhex(VGA_PALETTE[COLOR_INDEX[c]])[::-1] + b"\xff"
    return header + bytes(body)


def mouse_ini_text():
    return (
        "; Generals Online: the pointing hand the shell menus show over clickable controls.\n"
        "; Generated by GenOnline/tools/go_cursor.py; edit the script, not this file.\n"
        "; Data/INI/Mouse.ini loads first, so a mod's Mouse.ini keeps every retail cursor; without this entry Hand\n"
        "; shows the Normal cursor.\n"
        "\n"
        "MouseCursor Hand\n"
        f"  Texture             = {TEXTURE}\n"
        f"  Image               = {TEXTURE}\n"
        f"  HotSpot             = X:{HOTSPOT[0]} Y:{HOTSPOT[1]}\n"
        "End\n"
    )


def image_ini_text():
    return (
        "; Generals Online: the Hand cursor image for the polygon cursor mode.\n"
        "; Generated by GenOnline/tools/go_cursor.py; edit the script, not this file.\n"
        "\n"
        f"MappedImage {TEXTURE}\n"
        f"  Texture = {TEXTURE}.tga\n"
        f"  TextureWidth = {SIZE}\n"
        f"  TextureHeight = {SIZE}\n"
        f"  Coords = Left:0 Top:0 Right:{SIZE} Bottom:{SIZE}\n"
        "  Status = NONE\n"
        "End\n"
    )


def crlf(text):
    """INI bytes with CRLF line ends, like the other GO INIs."""
    return text.replace("\n", "\r\n").encode("ascii")


def outputs():
    """(path, bytes) of every generated file."""
    return [
        (DATA / ANI, build_ani()),
        (DATA / TGA, build_tga()),
        (DATA / MOUSE_INI, crlf(mouse_ini_text())),
        (DATA / IMAGE_INI, crlf(image_ini_text())),
    ]


def check():
    """Return the generated files that are missing or differ from what the script writes."""
    return [f"out of date: {path.relative_to(ROOT).as_posix()}"
            for path, data in outputs() if not path.exists() or path.read_bytes() != data]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when a cursor file is missing or out of date")
    args = parser.parse_args()
    if args.check:
        problems = check()
        for problem in problems:
            print(problem)
        return 1 if problems else 0
    for path, data in outputs():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote {path.relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
