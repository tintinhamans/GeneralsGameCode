# Generals Game Patch localization

Text-only localization bundled into the game, compiled per language at build time by
`cmake/StageEmbeddedAssets.cmake` (`str2csf --column`) into `Data/Languages/<code>/generals.csf`
of the embedded archive. Both files are byte-exact copies of the upstream ones (UTF-8, LF, no BOM).

Source: https://github.com/TheSuperHackers/GeneralsGamePatch (branch `main`, commit
`6c7f40d6af6a4babb77a33a4502ec7656446c13e`, 2026-09-18, fetched 2026-10-01).

| File | Upstream path | Last changed upstream | Languages |
|------|---------------|-----------------------|-----------|
| `generals.str` | `Patch104pZH/GameFilesEdited/Data/generals.str` | `b8f68f905e16fdaae2beda7dbea56ce753e1ab3c` (2025-04-15) | US DE FR ES IT KO ZH BP PL RU AR |
| `uk/generals.str` | `Patch104pZH/Design/Scripts/str/data/ukrainian_zh_by_yarpik_windstalker_edit_2/generals.str` | `72347612780c65590a3c8a0b7675deb2af5bad3f` (2024-09-27) | Ukrainian (single language, compiled as `uk`) |

The Ukrainian table (by yarpik, edited by windstalker; second revision) has 6447 labels against 6564 in
the multi-language file, so the newest labels show the installed language's text until upstream catches up.
It is the latest of the three Ukrainian revisions upstream; the older ones and the dev-comment English
variant are not bundled.

Licence: the repository is GPL-3.0 (`LICENSE.txt`) with the Electronic Arts terms in `TERMS.txt`
(no commercial profit, respect the rights of others, age-rating limits, no EA endorsement).

EA has not endorsed and does not support this product.

The per-language legacy font settings (`Language.ini`) live in `Assets/Game/Data/Languages/<code>/`.
