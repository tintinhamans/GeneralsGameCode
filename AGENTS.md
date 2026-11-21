# Generals Online client

Instructions for coding agents working on this repository. This is the Generals Online fork of
TheSuperHackers/GeneralsGameCode (Command & Conquer Generals and Zero Hour). The online layer is called GenOnline.

## Layout

- `Core/` holds code shared by both games, `Generals/` and `GeneralsMD/` (Zero Hour) hold the per-game code.
- GenOnline code lives in `Core/GameEngine/{Include,Source}/GameNetwork/GenOnline/` and its menus in
  `Core/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/GenOnline/`. Fork code inside upstream files is guarded
  by `#if defined(GENONLINE)`.
- `GenOnline/` holds the GenOnline data (`data/`, packed and mounted above the retail archives), tools (`tools/`),
  build checks and release tagging (`release/`), media build helpers (`media/`), licenses (`licenses/`), docs (`docs/`) and CMake
  logic (`cmake/`).
- `GenOnline/cmake/` holds the GenOnline build logic. `.github/workflows/genonline-*.yml` holds the GenOnline CI.

## Keep upstream files close to upstream

Upstream is merged regularly, so every line changed in an upstream-owned file is a future merge conflict.

- Put new build logic in `GenOnline/cmake/*.cmake`. An upstream `CMakeLists.txt` gets at most one include or hook line.
- Put new CI in `genonline-*.yml`. Do not edit the upstream workflows.
- Prefer new files and narrow `#if defined(GENONLINE)` blocks over rewriting upstream code.

## Naming

- Code uses `GenOnline` for classes, files and folders and `GENONLINE_` for macros and environment variables.
  New names never use `NGMP`, `NextGen` or `GO`. Some older `GO`-prefixed UI names remain.
- Public prose (READMEs, descriptions, UI text) says Generals Online.

## Settings

- GenOnline-only settings live in `client.json` (`%LOCALAPPDATA%\GeneralsOnline\client.json`).
- `Options.ini` keeps only the keys the upstream game reads.
- There is no migration code between the two. Only new clients are supported.

## Both games

GenOnline features work in Generals and Zero Hour alike. Only content that is Zero Hour data by nature, such as the
community patch, stays Zero Hour only.

## Comments

- A comment states only what the code cannot show: a non-obvious reason, an invariant, or an outside constraint.
  Do not restate the code, label sections or narrate steps.
- Write one plain present-tense sentence. No contractions, no "we" or "our", no dashes, no semicolons.
- Mark a fork change inside an upstream-owned file with `// GenOnline @<type> arcticdolphin DD/MM/YYYY <reason>`,
  where `<type>` is `feature`, `bugfix`, `tweak`, `fix` or `performance`.
- `// TheSuperHackers @...` tags belong to upstream work only. Keep existing original comments byte-identical.

## Building

- GenOnline code targets C++20, as the upstream non-VC6 builds do (`cxx_std_20` in `cmake/config-build.cmake`).
  VC6 builds stay C++98 and do not build GenOnline.
- Use the `win32-vcpkg-release` preset with MSVC from Visual Studio 18, toolset 14.44 (`vcvars32.bat -vcvars_ver=14.44`).
  The executables must run on the VC++ runtime 14.44.35211, `GenOnline/release/CheckImports.ps1` checks the imports.
- Pass `-DVCPKG_INSTALL_OPTIONS="--clean-buildtrees-after-build;--clean-packages-after-build"`.
- Build into a folder outside the repository and never share a `vcpkg_installed` folder between builds.
- Deep worktree paths can exceed MAX_PATH: map the source folder with `subst` to a free drive letter first.
- The game targets are `z_generals` (Zero Hour, `genonline_zerohour.exe`) and `g_generals` (Generals,
  `genonline_generals.exe`). `GenOnline/release/BuildRelease.ps1` builds a release.

## Commits

- Conventional Commits: `type(scope): Subject` with a capitalised subject and no body unless asked.
- `bugfix(scope)` for user-facing bugs, `fix(scope)` for code-level fixes.
- No AI attribution or Co-Authored-By lines.
