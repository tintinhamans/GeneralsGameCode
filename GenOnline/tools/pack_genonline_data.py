#!/usr/bin/env python3
r"""Pack GenOnline/data/core and the data folder of a game into the baseline data pack of that game.

Usage: pack_genonline_data.py <zerohour|generals> <build> [output folder]

The result is <output folder>\genonline_<game>@<build>.gpk. A .gpk is a plain BIG under another extension.
The folder layout inside core and the game folder is the layout inside the pack (Window\..., Data\INI\..., Art\Textures\...).
A file in both folders is taken from the game folder.
Entries are sorted by path and carry no timestamps, so packing the same files again gives the same bytes.

The pack ships in the data folder of the client, next to the executable. The client mounts the files listed
in the data.include list of settings.client.json, first listed wins, or this pack alone when the list is absent.
"""
import os
import re
import struct
import sys

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
CORE = os.path.join(DATA, "core")
GAMES = ("zerohour", "generals")


def collect(roots):
    """The files under the roots as (path inside the pack, full path), in path order. A later root wins for a path both hold."""
    found = {}
    for root in roots:
        for dirpath, _, names in os.walk(root):
            for name in names:
                full = os.path.join(dirpath, name)
                rel = os.path.relpath(full, root).replace("/", "\\")
                found[rel.lower()] = (rel, full)
    return sorted(found.values(), key=lambda f: (f[0].lower(), f[0]))


def main():
    if len(sys.argv) < 3 or len(sys.argv) > 4:
        sys.exit(__doc__)
    game, build = sys.argv[1], sys.argv[2]
    out_dir = sys.argv[3] if len(sys.argv) > 3 else "."
    if game not in GAMES:
        sys.exit("game must be one of: " + ", ".join(GAMES))
    if not re.fullmatch(r"[A-Za-z0-9._-]+", build):
        sys.exit("build may only hold letters, digits, dots, underscores and hyphens")
    game_folder = os.path.join(DATA, game)
    if not os.path.isdir(game_folder):
        sys.exit("no data folder for " + game + ": " + game_folder)
    files = collect([CORE, game_folder])
    if not files:
        sys.exit("nothing to pack for " + game)
    os.makedirs(out_dir, exist_ok=True)
    pack(files, os.path.join(out_dir, "genonline_%s@%s.gpk" % (game, build)))


def pack(files, out):
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
    with open(out, "wb") as f:
        f.write(b"BIGF" + struct.pack("<I", header + len(body)) + struct.pack(">II", len(files), header) + table + body)
    print("packed %d files into %s" % (len(files), out))


main()
