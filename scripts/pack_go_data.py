#!/usr/bin/env python3
r"""Pack GeneralsOnline/GameData into the Generals Online fix pack BIG.

Usage: pack_go_data.py [output.big]

The folder layout inside GameData is the layout inside the BIG (Window\..., Data\INI\..., Art\Textures\...).
The updater ships the result as <user data>\GeneralsOnlineGameData\GeneralsOnline_Core.big.
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "GeneralsOnline", "GameData")
OUT = sys.argv[1] if len(sys.argv) > 1 else "GeneralsOnline_Core.big"


def main():
    files = []
    for dirpath, _, names in os.walk(ROOT):
        for name in names:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, ROOT).replace("/", "\\")
            files.append((rel, full))
    files.sort(key=lambda f: f[0].lower())
    if not files:
        sys.exit("nothing to pack in " + ROOT)

    names = [rel.encode("ascii") + b"\0" for rel, _ in files]
    header = 16 + sum(8 + len(n) for n in names)
    table = b""
    body = b""
    offset = header
    for (rel, full), n in zip(files, names):
        with open(full, "rb") as f:
            data = f.read()
        table += struct.pack(">II", offset, len(data)) + n
        body += data
        offset += len(data)
    # BIGF: magic, total size (little endian), file count and data start (big endian)
    with open(OUT, "wb") as f:
        f.write(b"BIGF" + struct.pack("<I", header + len(body)) + struct.pack(">II", len(files), header) + table + body)
    print("packed %d files into %s" % (len(files), OUT))


main()
