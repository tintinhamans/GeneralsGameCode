# Renames the Generals Online naming families to GENONLINE_ (macros) and GenOnline (classes, files, folders).
# Re-runnable on any later tree: run from the repository root, without arguments for a dry run, with --apply to write.
# String literals stay untouched (log tags, file name prefixes, environment variables, server ids); comment tags such
# as NGMP_NOTE stay too because only the exact identifiers below are renamed.
import argparse
import os
import re
import subprocess
import sys
from collections import Counter

FOLDERS = ['Core/GameEngine/Include/GameNetwork/GeneralsOnline', 'Core/GameEngine/Source/GameNetwork/GeneralsOnline']
NEW_FOLDER_NAME = 'GenOnline'

FILES = {
    'NGMP_include.h': 'GenOnlineInclude.h',
    'NGMP_interfaces.h': 'GenOnlineInterfaces.h',
    'NGMP_types.h': 'GenOnlineTypes.h',
    'NGMP_Helpers.cpp': 'GenOnlineHelpers.cpp',
    'NGMPGame.h': 'GenOnlineGame.h',
    'NGMPGame.cpp': 'GenOnlineGame.cpp',
    'NextGenMP_defines.h': 'GenOnlineDefines.h',
    'NextGenTransport.h': 'GenOnlineTransport.h',
    'NextGenTransport.cpp': 'GenOnlineTransport.cpp',
    'GeneralsOnline_Settings.h': 'GenOnlineSettings.h',
    'GeneralsOnline_Settings.cpp': 'GenOnlineSettings.cpp',
}
for _name in ['Init', 'Auth', 'LobbyInterface', 'Moderation', 'RoomsInterface', 'StatsInterface', 'SocialInterface',
              'MatchmakingInterface']:
    for _ext in ('.h', '.cpp'):
        FILES['OnlineServices_' + _name + _ext] = 'GenOnline' + _name + _ext
FILES_LOWER = {k.lower(): v for k, v in FILES.items()}

IDENTIFIERS = {
    'GENERALS_ONLINE': 'GENONLINE',
    'GO_REVEAL_TEAMS': 'GENONLINE_REVEAL_TEAMS',
    'GO_VCREDIST_MAJOR': 'GENONLINE_VCREDIST_MAJOR',
    'GO_VCREDIST_MINOR': 'GENONLINE_VCREDIST_MINOR',
    'GO_VCREDIST_BUILD': 'GENONLINE_VCREDIST_BUILD',
    'NGMP_OnlineServicesManager': 'GenOnlineServicesManager',
    'NGMPGame': 'GenOnlineGame',
    'NGMPGameSlot': 'GenOnlineGameSlot',
    'TheNGMPGame': 'TheGenOnlineGame',
    'NGMP_RoomInfo': 'GenOnlineRoomInfo',
    'NGMP_WOLLoginMenu_LoginCallback': 'GenOnline_WOLLoginMenu_LoginCallback',
    'NGMP_WOLLobbyMenu_JoinLobbyCallback': 'GenOnline_WOLLobbyMenu_JoinLobbyCallback',
    'NGMP_WOLLobbyMenu_CreateLobbyCallback': 'GenOnline_WOLLobbyMenu_CreateLobbyCallback',
    'NextGenTransport': 'GenOnlineTransport',
    'GeneralsOnlineDiscordRPC': 'GenOnlineDiscordRPC',
    'GeneralsOnlineData': 'GenOnlineData',
    'GeneralsOnlineGameData': 'GenOnlineGameData',
    'GeneralsOnlineCrashData': 'GenOnlineCrashData',
    'NEXTGEN_TRANSPORT_NAME': 'GENONLINE_TRANSPORT_NAME',
    'RegisterNextGenTransport': 'RegisterGenOnlineTransport',
    'createNextGenTransport': 'createGenOnlineTransport',
}
for _name in ['Lobby', 'Auth', 'Social', 'Stats', 'Rooms', 'Matchmaking']:
    IDENTIFIERS['NGMP_OnlineServices_' + _name + 'Interface'] = 'GenOnline' + _name + 'Interface'

SOURCE_EXT = ('.h', '.hpp', '.cpp', '.c', '.inl')
SKIP_PARTS = ('/Vendor/', '/json.hpp')
WORD = re.compile(r'\b[A-Za-z_][A-Za-z0-9_]*\b')
INCLUDE = re.compile(r'^(\s*#\s*include\s*")([^"]+)(")', re.M)
OLD_FAMILIES = re.compile(r'\b(GENERALS_ONLINE\w*|GO_[A-Z0-9_]+|NGMP\w*|NEXTGEN\w*|\w*NextGen\w*|GeneralsOnline\w*)\b')

counts = Counter()


def map_identifier(m):
    word = m.group(0)
    if word in IDENTIFIERS:
        new = IDENTIFIERS[word]
    elif word.startswith('GENERALS_ONLINE_'):
        new = 'GENONLINE_' + word[len('GENERALS_ONLINE_'):]
    else:
        return word
    counts[word] += 1
    return new


def map_path(path):
    parts = path.split('/')
    out = []
    for i, part in enumerate(parts):
        if part == 'GeneralsOnline' and i + 1 < len(parts):
            part = NEW_FOLDER_NAME
        elif i == len(parts) - 1 and part.lower() in FILES_LOWER:
            part = FILES_LOWER[part.lower()]
        out.append(part)
    new = '/'.join(out)
    if new != path:
        counts['path ' + path] += 1
    return new


def split_code(text):
    # Yields (is_string, chunk); comments count as code so identifier references in them stay accurate.
    i, n, start = 0, len(text), 0
    while i < n:
        c = text[i]
        if c == '/' and text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c == '/' and text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == 'R' and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            k = text.find('(', i + 2)
            delim = text[i + 2:k]
            j = text.find(')' + delim + '"', k)
            yield False, text[start:i]
            end = n if j < 0 else j + len(delim) + 2
            yield True, text[i:end]
            i = start = end
        elif c in '"\'':
            yield False, text[start:i]
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            yield True, text[i:j + 1]
            i = start = j + 1
        else:
            i += 1
    yield False, text[start:]


def rename_source(text):
    # Include paths first, then identifiers outside string literals.
    text = INCLUDE.sub(lambda m: m.group(1) + map_path(m.group(2)) + m.group(3), text)
    return ''.join(chunk if is_string else WORD.sub(map_identifier, chunk) for is_string, chunk in split_code(text))


def rename_cmake(text):
    text = re.sub(r'[A-Za-z0-9_${}./-]*GeneralsOnline/[A-Za-z0-9_./-]*', lambda m: map_path(m.group(0)), text)
    return WORD.sub(map_identifier, text)


def tracked(root):
    out = subprocess.run(['git', '-C', root, 'ls-files'], capture_output=True, text=True, check=True).stdout
    return out.splitlines()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apply', action='store_true', help='write the changes and move the files')
    a = ap.parse_args()
    root = os.getcwd()
    changed = []
    for rel in tracked(root):
        if any(s in '/' + rel for s in SKIP_PARTS):
            continue
        name = os.path.basename(rel)
        if rel.endswith(SOURCE_EXT):
            fn = rename_source
        elif name == 'CMakeLists.txt' or rel.endswith('.cmake'):
            fn = rename_cmake
        else:
            continue
        path = os.path.join(root, rel)
        with open(path, newline='', encoding='utf-8', errors='surrogateescape') as f:
            old = f.read()
        new = fn(old)
        if new != old:
            changed.append(rel)
            if a.apply:
                with open(path, 'w', newline='', encoding='utf-8', errors='surrogateescape') as f:
                    f.write(new)
    moves = []
    for folder in FOLDERS:
        for rel in tracked(root):
            if rel.startswith(folder + '/'):
                rest = rel[len(folder) + 1:]
                base = os.path.basename(rest)
                if '/' not in rest and base in FILES:
                    rest = FILES[base]
                moves.append((rel, os.path.dirname(folder) + '/' + NEW_FOLDER_NAME + '/' + rest))
    print('%d files with renamed text, %d files moved' % (len(changed), len(moves)))
    for k, v in sorted(counts.items(), key=lambda kv: -kv[1]):
        print('%6d %s' % (v, k))
    if a.apply:
        for src, dst in moves:
            os.makedirs(os.path.join(root, os.path.dirname(dst)), exist_ok=True)
            subprocess.run(['git', '-C', root, 'mv', src, dst], check=True)
        left = Counter()
        for rel in tracked(root):
            if rel.endswith(SOURCE_EXT) and not any(s in '/' + rel for s in SKIP_PARTS):
                with open(os.path.join(root, rel), encoding='utf-8', errors='surrogateescape') as f:
                    for is_string, chunk in split_code(f.read()):
                        if not is_string:
                            left.update(m.group(1) for m in OLD_FAMILIES.finditer(chunk))
        print('old-family identifiers left outside strings (comment tags and names outside the mapping):')
        for k, v in left.most_common(40):
            print('%6d %s' % (v, k))


if __name__ == '__main__':
    sys.exit(main())
