#!/usr/bin/env python3
r"""Pack GenOnline/GameData into the Generals Online fix pack BIG.

Usage: pack_go_data.py [output.big]

The folder layout inside GameData is the layout inside the BIG (Window\..., Data\INI\..., Art\Textures\...).
The updater ships the result as <user data>\GeneralsOnlineGameData\GeneralsOnline_Core.big.

GameData\Themes\<name> holds the files of one interface theme at the paths of the files they replace. They stay out of the core
archive: each theme is packed into its own GeneralsOnline_Theme_<name>.big in a Themes folder next to the core archive, which the
client mounts above the core archive when the player picks that theme.

The client mounts it through the generals_online layer of data_packs in settings.json. From the top, the lookup is: loose files in the
game folder, -mod and -modDir, the BIGs in the game folders that are not retail, then the layers user, generals_online and community_patch,
then the retail archives. Every layer has enabled and include_paths, which are mounted in list order (the first one wins). An entry is
game:<path>, user:<path> or an absolute path: a BIG, a folder of BIGs or a *.big pattern.

    "data_packs": {
        "user": { "enabled": true, "include_paths": [], "exclude_paths": [] },
        "generals_online": { "enabled": true, "include_paths": [ "user:GeneralsOnlineGameData/GeneralsOnline_Core.big" ] },
        "community_patch": { "enabled": true, "include_paths": [ "user:GeneralsOnlineGameData/500_900_CommunityPatch_CoreINI.big" ] }
    }

user.exclude_paths leaves things out: loose files in the game folder (game:Data/INI/*), the BIGs in the game folders that are not retail
(game:340_ControlBarPro*.big, game:ZH_Generals/*.big) and the include_paths of the user layer itself. * matches any text including folders,
a trailing slash is a folder. Retail archives are never excluded. user.enabled false only turns off user.include_paths.

The old data_packs.use_community_data_patch switch is read as community_patch.enabled when community_patch is missing.
-disableCommunityDataPatch skips the community_patch layer for one launch.
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "GenOnline", "GameData")
OUT = sys.argv[1] if len(sys.argv) > 1 else "GeneralsOnline_Core.big"
THEMES = "Themes"


def collect(root, skip=None):
    """The files under root as (path inside the BIG, full path), in path order; the top folder named skip is left out."""
    files = []
    for dirpath, dirnames, names in os.walk(root):
        if skip is not None and os.path.normcase(os.path.abspath(dirpath)) == os.path.normcase(os.path.abspath(root)):
            dirnames[:] = [d for d in dirnames if d.lower() != skip.lower()]
        for name in names:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root).replace("/", "\\")
            files.append((rel, full))
    files.sort(key=lambda f: f[0].lower())
    return files


def main():
    files = collect(ROOT, THEMES)
    if not files:
        sys.exit("nothing to pack in " + ROOT)
    pack(files, OUT)

    themes_root = os.path.join(ROOT, THEMES)
    if not os.path.isdir(themes_root):
        return
    theme_dir = os.path.join(os.path.dirname(os.path.abspath(OUT)), THEMES)
    for theme in sorted(os.listdir(themes_root), key=str.lower):
        theme_files = collect(os.path.join(themes_root, theme))
        if not theme_files:
            continue
        os.makedirs(theme_dir, exist_ok=True)
        pack(theme_files, os.path.join(theme_dir, "GeneralsOnline_Theme_%s.big" % theme))


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
