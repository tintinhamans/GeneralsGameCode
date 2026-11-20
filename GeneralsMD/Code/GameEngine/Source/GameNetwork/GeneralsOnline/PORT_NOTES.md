# Generals Online port notes

This directory holds the online-system replacement ported from
GeneralsOnlineDevelopmentTeam/GameClient ("GO"), a TheSuperHackers-derived fork that
replaced GameSpy with a modern stack (REST + WebSocket backend, Valve
GameNetworkingSockets ICE P2P transport, lobbies/matchmaking/stats).

## Provenance

- Ported from GO commit: `d7f75517dd76c3a4e3de75e988f328a6877621c5` (2026-08-24);
  re-synced to `9c1121bd5` (2026-09) including the 60 Hz simulation and engine-feel changes
  listed under "Decisions made during the port"; re-synced to `6ff404436` (2026-10-05) by
  cherry-picks, see "Not taken in the 2026-10 sync"
- GO's merge base with TheSuperHackers: `e760b3695` (2026-07-26)
- contraZH's merge base with TheSuperHackers at port time: `5943d3856` (== tsh HEAD)
- Future re-syncs: `git fetch go && git diff -w <old-go-sha>..<new-go-sha> -- <ported
  paths>`, applied per the rules below.

## Port decisions

1. Online system plus GO's engine-feel fork (60 Hz sim, frame pacing, widescreen,
   observer overlay, crash guards, stats exporter). GO's unguarded gameplay/balance
   edits, the community patch BIG and the GameMemory relocation are NOT ported.
2. GameSpy stays. Everything is gated behind the CMake option
   `RTS_BUILD_GENERALS_ONLINE` (default OFF). The OFF build must stay byte-identical in
   behavior; every in-place edit to a shared file must sit inside
   `#if defined(GENERALS_ONLINE)`.
3. Telemetry off by default: Sentry behind `GENERALS_ONLINE_SENTRY`, hardware
   fingerprinting behind `GENERALS_ONLINE_HW_FINGERPRINT`, anti-cheat behind
   `GENERALS_ONLINE_USE_PLUGINS_INTERFACE` — all undefined by default, kept in the
   protocol so official-server coordination can re-enable them.
4. Exe name stays `generalszh`; GO's rename is not taken.

## Merge rules (apply to every file brought over)

- Diff GO with `-w` always: its tree carries heavy CRLF churn and `nullptr`->`NULL`
  reversions of TSH modernization. Discard modernization-revert hunks.
- `#else` (non-GO) branches always keep contraZH's text, never GO's — GO's dead branches
  contain code that does not compile (e.g. `m_fpsAverages` in ConnectionManager.cpp).
- Every GO include added to a Core/ file must be wrapped in
  `#if defined(GENERALS_ONLINE)` and use a full `GameNetwork/GeneralsOnline/...` path
  (GO's unguarded relative includes break the Generals (vanilla) build).
- UI screens are dual-copied (GO's versions live in `.../Menus/GeneralsOnline/`), never
  merged in place: GO rewrote them with direct NGMP calls that cannot compile against
  GameSpy.
- `InGameUI.{h,cpp}` are never merged from GO — adapt GO call sites to contraZH's API.
- Blind file copies of any GO-touched gameplay file are forbidden — GO's balance changes
  are mostly unguarded.

## Decisions made during the port (deviations from upstream GO)

- **Client id**: `custom_third_party_client`, which the services map to a full game
  session, instead of the official client's `gen_online_60hz`.
- **60 Hz client**: the simulation runs at 60 Hz like the official client
  (`GENERALS_ONLINE_HIGH_FPS_LIMIT` 60). The switch
  `GENERALS_ONLINE_HIGH_FPS_SERVER` is defined by `GeneralsMD/Code/CMakeLists.txt` next to
  `GENERALS_ONLINE` while `RTS_GENERALS_ONLINE_60HZ` is ON (the default; OFF builds GO's
  30 Hz `#else` paths), not by NextGenMP_defines.h, because `WWSyncPerSecond` lives in
  WWLib's `WWCommon.h` and that static library cannot include GO headers. `GameCommon.h`
  includes NextGenMP_defines.h under `GENERALS_ONLINE` so every engine TU sees the
  HIGH_FPS macros (upstream gets the same effect from its PCH). Deviations from GO's
  60 Hz code:
  - `GameLogic::getFrameLegacy()` / `HasLegacyFrameAdvanced()` are derived from `m_frame`
    and the frame multiplier instead of two stored counters with a hardcoded `% 2`; nothing
    new is zeroed or xfer'd. The unused `getFrameLegacyLast()` is not ported.
  - `GameClient`'s legacy counter, which steps the particles, ticks once per
    `GENERALS_ONLINE_HIGH_FPS_FRAME_MULTIPLIER` logic frames. GO accrues wall-clock
    milliseconds instead, which makes the particle rate depend on each client's render rate
    and ignore network slowdowns. Keep the logic-frame tick when merging GO particle code.
  - The FramePacer render floor and `FrameMetrics::init()` seeds are guarded on
    `HIGH_FPS_SERVER` and use `LOGICFRAMES_PER_SECOND`; GO guarded the floor on plain
    `GENERALS_ONLINE`. `htree.cpp`'s static_assert accepts 30 or 60 without a GO include.
  - `Weapon.cpp` takes only the `delayToUse` clamp. GO's GattlingBuilding and QuadCannon
    name-matched rate hacks are ZH balance and are not taken.
  - `EMPUpdate.cpp` keeps `GameClientRandomValue` for the particle delay (GO switched to
    the logic RNG inside the 60 Hz guard; particle setup is client-side).
  - `ScriptEngine.cpp`: `adjustTimer` converts seconds with `BaseFps` like `setTimer`;
    GO only changed `setTimer`, which would have made "add 1 second" add 2 seconds.
  - `InGameUI.cpp` message fade: the fade amount is scaled by the frame multiplier and
    starts at the timeout. GO multiplied the timeout, which is 0 (integer math) at both
    rates, so its hunk changed nothing.
  - Not ported: GameLOD auto quality degrade (uncalled in go/main), the commented-out
    `seglinerenderer.cpp` block, `GENERALS_ONLINE_RUN_FAST` (dead in both trees).
  - Replays: GO has no rate tag. `replayMatchesGameVersion` already compares the exe CRC,
    which differs between any two builds, so a 30 Hz replay is refused by the existing
    check; no extra guard was needed.
  - Known upstream quirk left as is: `FRAME_GROUPING_CAP` is a `static int` in a header,
    so `NGMPGame.cpp`'s service-config write never reaches `Network.cpp`'s copy. The
    official client has the same behaviour, so parity wins over the fix.
- **Render cap policy**: GO's settings.json FPS limit and camera scroll speed govern an
  online match only. `NGMPGame::applyMatchRenderSettings()` snapshots Contra's values on
  first use and `restoreRenderSettings()` puts them back; `GameEngine::update()` calls one
  or the other every frame depending on whether a match is running, and the destructor
  restores too. GO applied its cap in the shell as well and set the Bool `m_useFpsLimit`
  from an integer FPS value; both are fixed here.
- **Observer overlay**: GO's `drawObserverStats` duplicates TSH's `drawPlayerInfoList`,
  so the TSH table is extended (SP, K, L, P columns, army name, red power value when
  short) instead. The notification feed is new. The army name comes from the player
  template, not GO's ZH side-string table, and GO's power-use table of ZH superweapon
  names is replaced by the command button label of the power. Milestones poll once a
  second from `update()` only while the observer bar is on, not from the draw path.
  The `GameWindowTransitionSpeedMultiplier` 1.0 to 2.5 default change that sits in the
  same OptionPreferences diff is not taken. GO's Shift+arrow font hotkeys are not taken;
  the font sizes are Options.ini keys.
- **Keyboard auto repeat dedupe** is guarded on `GENERALS_ONLINE` rather than taken
  unguarded, and GO's `KEY_REPEAT_DELAY_MSEC - KEY_REPEAT_INTERVAL_MSEC` sign flip is not
  taken.
- **StatsExporter/StatsUploader** are ported whole but every hunk outside the two new
  files is guarded on `GENERALS_ONLINE` (upstream is unguarded). GO's Player.cpp hook hunk
  also added `m_scoreKeeper.addMoneySpent(...)` for units, a scoring change; not taken.
  `-disableCommunityDataPatch` and the community patch BIG loader are not taken.
- **Widescreen**: the scaling divisors (ControlBarResizer, W3DHorizontalSlider, military
  subtitle) and the aspect-corrected camera height are in, under
  `GENERALS_ONLINE_WIDESCREEN`. GO's observer zoom clamp in `setHeightAboveGround` (max
  height forced to 500 or 1000) is not taken; the fork's lobby camera limit applies.
  GO ships no .wnd files in git; the 1280x720 layouts come from the official installer's
  `GeneralsOnlineGameData\`, which `winCreateFromScript` already prefers when ON. That
  preference also shadows the fork's script-patched `OptionsMenu.wnd`
  (`build/add_*_wnd.py`, 800x600 coordinates). Contra copies of those layouts at 1280x720
  are still to be made.
- **Crash guards taken unguarded** (pure null and bounds checks, comments trimmed):
  surfaceclass, render2dsentence, W3DTerrainTracks, W3DTreeBuffer, BaseHeightMap,
  W3DMouse (thread stopped before assets are freed, mutex held across the TheMouse check),
  FFmpegFile, FFmpegVideoPlayer, PopupReplay, ControlBar beacon template.
- **Not taken from the post-audit drift**: `GetConnectionType` to
  `GetDetailedConnectionStatus` (the vendored GNS carries GO's `GetConnectionType` vtable
  entry; the rename only matters for stock GNS), the `bittype.h` `uint32` typedef, and the
  FetchContent build migration.
- **Texture filtering kept**: `GENERALS_ONLINE_DISABLE_TEXTURE_FILTERING_AND_AA` is
  commented out and the W3DDisplay hunk that forces MSAA off was not taken - this fork
  has its own TextureFilter/AnisotropyLevel feature.
- **Sentry**: gate reuses GO's own `GENERALS_ONLINE_USE_SENTRY`, now default-off and
  extended to cover `InitSentry`'s body, `ShutdownSentry`, and the lib pragma (upstream
  ran sentry_init unconditionally in release builds).
- **Fingerprinting**: new `GENERALS_ONLINE_HW_FINGERPRINT` (default-off) gates machine
  GUID / MAC / volume serial in the auth payloads and the loaded-module report on the
  WS keepalive; fields are sent empty so the wire shape is unchanged.
- **Version/branding**: `version.cpp` product title/version hunks NOT taken (cosmetic;
  the backend gets `GENERALS_ONLINE_VERSION_STRING` directly from OnlineServices_Init).
  WinMain's `TheVersion->setVersion` GO variant IS taken (guarded) since the network
  version fields matter for matchmaking.
- **W3DView::setDefaultView**: takes GO's 4-arg signature under the macro for compile
  compatibility, but `bForceDefaultCam` is unused and GO's settings.json camera section
  (`DetermineCameraMaxHeight`, `Camera_GetMinHeight`) is not applied. The lobby's
  `max_cam_height` (host `/maxcameraheight`) IS honored, via `NGMPGame::SyncWithLobby` ->
  `GameInfo::m_maxCameraHeight` (`CH=` in the options string) ->
  `GameLogic::applyMaxCameraHeightForGame`. A lobby left at GO's default (310) plays the
  mod's own GameData `MaxCameraHeight`. Options.ini `UseCustomMaxCameraHeight` /
  `MaxCameraHeight` are personal and never affect GO games.
- **GO headers made self-sufficient**: upstream GO force-includes `PreRTS.h` into every
  TU via CMake PCH, which transitively provides `NextGenMP_defines.h`. This port keeps
  contraZH's PCH setup, so `NextGenMP_defines.h` is included explicitly where needed
  (GeneralsOnline_Settings.h, NGMPGame.h, ConnectionManager.h).
- **Fixed keys** (`af19d94ce` and earlier): GO's hardcoded `MSG_RAW_KEY_UP` block in
  CommandXlat (F11 JPEG screenshot, F10 advanced network stats, F5/Insert buddy overlay)
  is ported as meta commands instead: `TOGGLE_NETWORK_STATS`, `TAKE_SCREENSHOT_ALT`,
  `TOGGLE_BUDDY_OVERLAY` and `TOGGLE_BUDDY_OVERLAY_ALT`, with GO's keys as defaults in
  `generateMetaMap` and a new `CATEGORY_GO` that the Hotkeys screen shows as its GO
  section. They fire on press, not GO's release. All of it is under `GENERALS_ONLINE`,
  which zi_always defines for every Zero Hour target, so the meta enum agrees across libs.
  The meta translator fires only the first matching record, so the GO records are
  appended (`appendMetaMapRec`) and any other command on the same key keeps it.
  `INI::loadHotKeys` skips a CommandMap block it cannot read instead of the rest of the
  file, because the offline build reads the same Hotkeys.ini and lacks these names.
  The F10 overlay moved from InGameUI.cpp to `NGMP_OnlineServicesManager::DrawAdvancedNetworkStats`
  (OnlineServices_Init.cpp), because NetworkMesh.h's winsock include defines `min`/`max` and
  breaks `std::max` in InGameUI.cpp. `drawGameTime` still calls it as upstream, so it shows
  only while the game timer does. GO's early `return`, which hid the timer when the mesh had
  no connections, is not taken.
- `GameSpyOpenOverlay` buddy bypass is in (the GameSpy connection check is compiled out
  under `GENERALS_ONLINE`). `SetLookAtPlayer` and the StatsExporter pipeline are ported.
- Log-only hunks (NetworkLog conversions of commented DEBUG_LOGs), `isspace/isdigit`
  cast fixes, `nullptr`->`NULL` reverts, and GO's dead `#else` branches were not taken.
- **Dedicated GPU exports**: `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance`
  live in `Common/DedicatedGpuExports.h`, included once per exe, because the GO-off build and
  the tools want them too. This is a deliberate exception to port decision 2. Upstream GO
  defines them in `OnlineServices_Init.cpp`, and a re-sync must drop that block because a
  second definition fails the `win32-go` link with LNK2005.

### Not taken in the 2026-10 sync

Range `9c1121bd5..6ff404436`, GO-authored commits only. The TSH commits GO merged arrive
with contraZH's own TSH syncs. Menu edits land in the `Menus/GeneralsOnline/` copies and
LobbyUtils.cpp edits in the GO whole-file copy.

| GO commit | Change | Reason |
|---|---|---|
| `a9aaa0ac2` `8e8d1cfe5` `4426d37cd` `7f113728c` `fb6cade08` `0ee25cc24` | vcpkg static networking deps, GNS fork with native ICE and WebRTC, CI, install and patch packaging | Vendored libs kept. The vendored header has the ICE implementation switch, and the service default (2, WebRTC) matches the vendored DLL |
| `4055d287b` | Camera reset to a cached default max height | The fork never widens the max height for observer free-zoom, and the cached value goes stale when the lobby or Options camera height calls `setMaxHeightAboveGround` |
| `ba7bf541f` `e2f439772` | Camera pitch keys (PgUp/PgDn/Home, numpad), minimum match pitch, slower keyboard rotation, rotate speed option | Unguarded changes to shared controls; would change offline Contra |
| `4bbd06b70` | Thread list exit guard in Except.cpp | Builds on GO's thread list lock rework, which was never ported |
| `8657eb2ef` `158104f1b` | Process-wide UTF-8 code page, Unicode window, AsciiString/UnicodeString conversion through the code page | Global encoding change for every build. `f73b0d1bc` (lobby map names as UTF-8) is taken; its helpers check `GetACP()` |
| `c6f585fb9` | VC++ runtime check that downloads and installs the redistributable | Unguarded startup code with a "Generals Online" dialog |
| `7322f0acc` `2e3e6b072` | Frameless startup splash window and hand-over | Unguarded WinMain and W3DDisplay changes at device creation |

Adapted while picking:

- `e629eb001` unbinds through `Set_DX8_Stream_Source`/`Set_DX8_Indices` so D3D9 compiles.
- `03b457c42` guards both rider paths of the multi-addon Overlord draw.
- `01cdd0cad` adds only the clamps; the surrounding tree code keeps contraZH's text.
- `d97cfd432` guards the new `ResolveGameListLobbyID` declaration in LobbyUtils.h.
- `dabb98b81` switches the login URLs to https; the restored `LoginCode` flow uses https too.
- `d5124c304` leaves out the `/guard:cf` build flags; the plugin interface is compiled out.
- `6b4b0185c` turned `GENERALS_ONLINE_USE_PLUGINS_INTERFACE` back on in NextGenMP_defines.h and
  merged cleanly; it is commented out again. Check that define after every sync.

### Resolved: live service login (2026-10-12)

Login against `api.playgenerals.online` works end to end without the anticheat plugin.
It took the server issued login code (below) and the `custom_third_party_client` client
id. Before that, in August, `CheckLogin` succeeded but every authenticated call
(WebSocket upgrade, `ServiceConfig`, `RefreshToken`) answered `401 invalid_token`.

### Login flow: server issued login code

GO commit `27da5b89d` ("- New login flow", 2026-08-22) gets the login code from a
`GET LoginCode` request and opens the browser with it. Production serves `LoginCode`
since at least 2026-10-12, when `CheckLogin` answered a client generated code with 403,
`result: 2`. The August revert to the client generated code (`072e4fd6e`) is undone.

### Vendor DLL fix (found by crash triage)

Upstream GO's vendored `Vendor/ValveNetworkingSockets/abseil_dll.dll` (4.3 MB) does
not match the abseil the vendored `libprotobuf.dll` was built against - loading it
corrupts the heap inside protobuf's static initializer (0xc0000374 in `DllMain`,
before WinMain). GO never noticed because their POST_BUILD copies only
discord-rpc.dll; the working DLL set comes from their patcher. The vendored copy is
replaced here with the 1.8 MB `abseil_dll.dll` an official GeneralsOnline install
ships (byte-verified against `libprotobuf.dll`/`GameNetworkingSockets.dll`, which are
identical between the repo and the official install). Note the official install also
carries a 64-bit `zlib1.dll` for other tooling - do NOT take that one; the repo's
x86 `zlib1.dll` is correct for the game.

### UI phase decisions

- **Dual copies** live in `GUICallbacks/Menus/GeneralsOnline/`; CMake swaps them for the
  originals when the option is ON. Ported so far: WOLLoginMenu, WOLWelcomeMenu,
  WOLLobbyMenu, WOLGameSetupMenu, WOLMapSelectMenu, PopupHostGame, PopupJoinGame.
- **LobbyUtils.cpp** uses a whole-file dual instead of ~20 guarded hunks: the Core
  implementation is wrapped in `#if !defined(GENERALS_ONLINE)` and GO's rewritten copy
  compiles from the GO tree when ON. The shared LobbyUtils.h carries guarded enums.
- **W3DListBox** row-entry animation: when OFF, `rowDrawY` is a const alias of `drawY`
  (the one deliberate shared-text change that is not inside an `#if`; it is
  behavior-identical). GO's hardcoded green listbox background fill was NOT taken
  (visual restyle), nor were its unguarded null-check hunks in GadgetListBox.
- **GeneralsOnline_UIStubs.cpp** temporarily defines showNotificationBox,
  updateBuddyInfo and the GO-signature SetLookAtPlayer until WOLBuddyOverlay and
  PopupPlayerInfo are ported; delete the stubs with that phase.
- The GO menus' `#include "../X.h"` lines resolve through the Vendor `-I` entry
  (`Vendor/../` = the GeneralsOnline dir) - kept verbatim for diffability with GO.
- GO's menus reference only retail `.wnd` layout names; GO ships updated layouts as
  loose files under `GeneralsOnlineGameData\` which winCreateFromScript prefers when
  ON. Obtain that directory from a GO install/patcher for the full experience.
- **InGameChat gate adapted, not adopted**: an active NGMP game additionally allows
  the chat window; upstream GO allowed it ONLY then, which would have broken LAN chat
  and this fork's singleplayer chat-command window.
- **Skipped in the UI phases**: OptionsMenu hunks other than the observer font round
  trip (texture-filter guard is inert here, IP masking, GameSpy checks),
  SkirmishGameOptionsMenu's team-colored start positions (unguarded, and drops a bounds
  check), and Diplomacy's GO_REVEAL_TEAMS block (macro never defined).

## Fixes

Everything below was found after the port compiled, by running the client against the
live service. They fall into three groups, and the second one is a pattern worth
knowing before hunting the next crash.

### 1. Environment / vendored binaries

| Fix | Symptom | Cause |
|---|---|---|
| Replace vendored `abseil_dll.dll` | Process died before `WinMain` (`0xC0000374`) | The repo's abseil does not match the abseil its `libprotobuf.dll` was built against. See the vendor DLL section above. |
| Build with `/LARGEADDRESSAWARE` | Null deref in `TextureLoadTaskClass::Lock_Surfaces` during a single player load screen | 32 bit address space exhaustion, not the online code - see below. |

**The address space fix is not caused by this port, but the port provokes it.** The
crash was a null dereference in the stock WW3D texture loader: creating a 2048x2048
X8R8G8B8 surface for `ctrloadpageuserinterface.tga` (~16 MB contiguous) failed and
`Lock_Surfaces` used the result without checking it. A *full* dump (`CrashFZ`) was what
identified the cause - the process sat at **1.81 GB committed against the default 2 GB
limit**, with the remaining ~200 MB fragmented across 500+ regions, so the allocation
had nowhere to go. A mini dump would only have shown the null.

The online build links GameNetworkingSockets, protobuf, OpenSSL, libcurl and abseil,
which add real image and heap pressure to a process that was already close to the
ceiling with Contra's assets - so the online build reaches the limit sooner even though
no online code is on the stack. `/LARGEADDRESSAWARE` (in `GeneralsMD/Code/Main/
CMakeLists.txt`) raises the limit to 4 GB on 64 bit Windows and is a no-op on a 32 bit
OS. It is applied to **all** Zero Hour builds, not gated on the online option, because
the underlying pressure is the mod's asset load.

Note this raises the ceiling rather than making the loader tolerate a failed
allocation; the same line would crash the same way if 4 GB were ever exhausted. Adding
the null check as well is still open.

### 2. GameSpy singletons that GO redirects to NGMP (the recurring pattern)

**With the GO stack active, `SetUpGameSpy()` is compiled out, so `TheGameSpyInfo` and
`TheGameSpyGame` are never created.** Upstream GO rewrote every site that reads them to
use the NGMP equivalents instead. Where the port took a header change but missed the
matching body, the result is a null dereference that only appears once the UI flow
actually reaches that screen - so these surfaced one at a time, going deeper into the
online flow with each fix.

| Fix | Crash site | Redirect applied |
|---|---|---|
| Preference constructors (`Custom`, `QuickMatch`, `GameSpyMisc`, `Ignore`) | `CustomMatchPreferences::CustomMatchPreferences+0x7e` via `PopupHostGameInit` - clicking Host or Quick Match | `TheGameSpyInfo->getLocalProfileID()` -> NGMP auth user id, stored under `GeneralsOnlineData\` |
| `LoadScreen::didPlayerPreorder` | latent, would crash during an online match load | forced to `false` when GO is active |
| `GameLogic` game-info selection | `GameSpyLoadScreen::init+0xce` via `tryStartNewGame` - launching a hosted match | `TheGameInfo = TheGameSpyGame` -> `TheNGMPGame` for `GAME_INTERNET` |
| `Recorder` stats path | latent (`RTS_DEBUG` + `m_saveStats`) | same redirect, guarded on the services manager existing |
| `GameSpyLoadScreen::init` per-slot stats | `GameSpyLoadScreen::init+0x850`, same launch - the *next* null in the same function | `TheGameSpyPSMessageQueue->findPlayerStatsByID()` -> `NGMP_OnlineServices_StatsInterface::getPlayerStatsFromCache()` |

With that last one the full online path works end to end against the live service:
login -> lobby -> host -> launch -> in match.

Two details from the load screen fix worth keeping, because a straight line-for-line
port of the crashing statement would have been wrong:

- Upstream moves the stats fetch **above** the player-name block, because the name gets
  an Elo suffix in QuickMatch games (`name.format(L"%s (Elo: %d)", ...)`). The port had
  to reorder, not just guard.
- `GetAdditionalDisconnectsFromUserFile` is neither declared nor called when GO is on -
  its definition now lives only in the GO copy of `PopupPlayerInfo.cpp`.

**How to diagnose the next one.** The game writes its own dumps to
`Documents\Command and Conquer Generals Zero Hour Data\CrashDumps\` as
`CrashMZ-<date>-<commit>-pid<n>.dmp` (mini) and `CrashFZ-...` (full, ~1 GB) - *not* to
the Windows WER folder. **Do not trust the commit in that filename**: it is
`GitShortSHA1`, baked in when CMake last *configured*, so it goes stale across rebuilds
and does not identify the binary that crashed. Compare the faulting offset instead - a
crash that moved from `+0xce` to `+0x850` in the same function is a fix working, not a
fix failing. The mini dump plus the deployed `generalszh.pdb` is enough:

```
cdb.cmd -z "<CrashMZ dump>" -y "C:\Games\contra\contraprerelease;srv*<symcache>*https://msdl.microsoft.com/download/symbols"
.ecxr
kb 40
```

A null read of the form `mov eax,dword ptr [ecx] ds:002b:00000000` inside a function
whose name starts with `GameSpy` is almost certainly another instance of this pattern:
find the same function in the GO clone, take its `#if defined(GENERALS_ONLINE)` branch.

If the faulting function is *not* GameSpy related, reach for the **full** (`CrashFZ`)
dump instead. It carries the heap, so `!address -summary` shows memory pressure and
`dt` on the faulting object shows what was actually being loaded - that is how the
texture crash above was traced to address space exhaustion rather than a bad pointer.

### 3. Behavior differences from running a fork against the official service

| Fix | Reason |
|---|---|
| Report `anticheat_id = -1` (`NONE`) | Upstream's stub returns `0`, which is `GO_INTEGRATED_AC` - the client claimed to run GeneralsOnline's anticheat while running none. `IsPluginLoaded()`/`IsExternalProcessRunning()` were likewise hardcoded `true`. |
| `GENERALS_ONLINE_DISABLE_SELF_UPDATE` | The service reports "update needed" for this client, and the stock flow would download and run the official patcher over a Contra install. |
| Login link copied to clipboard, dialog reworded | `ShellExecute` opens the browser *behind* the game in exclusive fullscreen (upstream runs windowed-fullscreen, which this fork does not take), so the login appeared to fail silently. Applied at all three browser-open sites of the pre-newserver flow. |

## Triage of GO's non-online-tree changes (`git diff -w e760b3695..d7f75517d`,
## excluding `*GameNetwork/GeneralsOnline/*`; 361 files, +21522/-3744)

Categories: **A** = online-required, port (guarded). **B** = engine-feel fork, skip.
**C** = gameplay/balance ("community patch", mostly unguarded), skip. **D** = noise
(NULL-reverts, whitespace, typos), skip. **I** = infra/CI/packaging, skip.
**R** = review at the phase that touches it; default skip unless an A-hunk is found.

### A — port (Core, Phase 3)

| File | -w delta | Notes |
|---|---|---|
| Core/.../GameNetwork/Transport.{h,cpp} | 19-29 / 11-361 | dual-mode: GO abstract base under GENERALS_ONLINE, original otherwise |
| Core/.../GameNetwork/ConnectionManager.{h,cpp} | 18-0 / 277-26 | initTransport switch + guarded hunks |
| Core/.../GameNetwork/Network.cpp | 118-55 | A-hunks only; drop RUN_FAST/HIGH_FPS |
| Core/.../GameNetwork/NetworkInterface.h | 15-1 | |
| Core/.../GameNetwork/NetworkDefs.h | 26-14 | |
| Core/.../GameNetwork/FrameMetrics.{h,cpp} | 21-0 / 151-8 | |
| Core/.../GameNetwork/Connection.{h,cpp} | 4-0 / 13-1 | |
| Core/.../GameNetwork/DisconnectManager.cpp | 8-0 | |
| Core/.../GameNetwork/GameInfo.{h,cpp} | 1-1 / 2-0 | |
| Core/.../GameNetwork/NAT.cpp | 4-2 | |
| Core/.../GameNetwork/NetworkUtil.cpp, NetCommandMsg.cpp, LANAPI.cpp | tiny | verify A vs D |
| Core/.../GameSpy/PeerDefs.{h,cpp} | 2-1 / 4-0 | SetUpGameSpy bypass |
| Core/.../GameSpy/MainMenuUtils.cpp | 151-4 | Online button -> NGMP init |
| Core/.../GameSpy/LobbyUtils.{h,cpp} | 13-4 / 540-34 | lobby list plumbing |
| Core/.../GameSpyOverlay.{h,cpp} | 5-0 / 14-0 | message boxes reused by GO UI |
| Core/.../GameSpy/LadderDefs.cpp | 3-0 | |
| Core/.../GameSpy/PersistentStorage{Defs.h,Thread.h,Thread.cpp} | 5-1 / 7-0 / 13-0 | |
| Core/.../GameSpy/Thread/{PeerThread,BuddyThread,PingThread,GameResultsThread}.cpp | 2-3 each | small guards |
| Core/.../GameNetwork/DownloadManager.h | 2-0 | |

### A — port (GeneralsMD lifecycle + support, Phase 3)

| File | -w delta | Notes |
|---|---|---|
| GeneralsMD/.../Common/GameEngine.cpp (+.h) | 135-28 / 7-0 | hand merge (fork delta); skip HIGH_FPS block, keep our try/catch |
| GeneralsMD/Code/Main/WinMain.cpp | 21-3 | hand merge (fork delta) |
| GeneralsMD/.../Win32Device/.../Win32GameEngine.cpp | 9-1 | |
| GeneralsMD/.../GameNetwork/UDPTransport.{h,cpp} | new 73/413 | retail transport impl, compiled only when ON |
| GeneralsMD/.../Common/version.cpp | 10-0 | |
| GeneralsMD/.../Common/System/registry.cpp | 53-0 | guarded language fallback |
| GeneralsMD/.../Common/CommandLine.cpp | 30-0 | GO-guarded args only |
| GeneralsMD/.../Common/Recorder.cpp | 38-6 | replay metadata; review hunks |
| GeneralsMD/.../Common/GameMain.cpp | 7-1 | review, likely lifecycle |
| GeneralsMD/.../GameLogic/System/GameLogic.cpp (+GameLogic.h) | 406-152 / 40-2 | MIXED — NGMP hunks only, file also carries C |
| GeneralsMD/.../GameClient/GUI/GUICallbacks/MessageBox.cpp (+.h) | 5-0 / 2-0 | |
| GeneralsMD/.../Common/CustomMatchPreferences.h | 3-0 | |
| GeneralsMD/.../Common/MessageStream.{h,cpp} | 5-0 / 4-0 | review: likely new GO message plumbing |
| Core/GameEngineDevice W3DDisplay.cpp (ZH path), ww3d.{cpp,h} | 7 / 2-1 | |

### A — port (UI, Phases 4-5)

Dual copies into `Menus/GeneralsOnline/`: WOLLoginMenu (140-265), WOLWelcomeMenu
(178-10), WOLLobbyMenu (1175-69), WOLGameSetupMenu (1743-425), WOLMapSelectMenu (54-16),
PopupHostGame (135-5), PopupJoinGame (34-6), WOLQuickMatchMenu (704-10), WOLBuddyOverlay
(659-6), PopupPlayerInfo (248-138), ScoreScreen (325-76).

In-place guards: MainMenu.cpp (4-1), OptionsMenu.cpp (23-1), InGameChat.cpp (7-0),
Diplomacy.cpp (15-1), DownloadMenu.cpp (35-1), GUIUtil.cpp (43-3), SkirmishGameOptionsMenu
(38-9, review), PopupReplay.cpp (3-0, review), LanGameOptionsMenu (2-2, review).

Stats (Phase 5): StatsExporter.{h,cpp} (44/864 new), StatsUploader.{h,cpp} (new).

### R — review on demand (pull minimally when the compiler asks, Phase 4)

GameWindowManager.{h,cpp} (259-256; GO also touched Generals copy), GadgetListBox.{h,cpp}
(110-0 / 8-0), W3DListBox (15-10), GadgetPushButton (11-2), GameWindowManagerScript
(22-1), GadgetTextEntry.h (1-0), GameWindow.h (2-1), AnimateWindowManager (4-1),
ControlBarPopupDescription (5-1), MapUtil.{h,cpp} (2-2 / 8-3), INIMapCache (2-1),
QuotedPrintable (8-8), SubsystemInterface (8-4), LanguageFilter (2-0), GameClient.{h,cpp}
(14-2 / 33-1), GlobalData.{h,cpp} (24-0 / 62-9 — mostly B, check for GO fields),
StackDump.cpp (3-2, Sentry-adjacent), WWLib/Except.cpp (75-2, Sentry crash handler —
only under GENERALS_ONLINE_SENTRY if ever), Core/GameEngine/CMakeLists.txt (5-10 —
re-implement by hand, never apply).

### B — engine-feel fork (mostly ported in the 60 Hz phase, see the decisions above)

Still skipped: GameMemory relocation, Intro (GO logo), dx8wrapper (windowed fullscreen,
d3d8 hook loader), texture filtering/AA off, GameLOD auto degrade, Except.cpp/Sentry,
MilesAudioManager and W3DVolumetricShadow (reformat only), W3DControlBar and
ControlBarPopupDescription (TSH reverts), the observer zoom clamp in W3DView.

Original list: GameMemory relocation (all GameMemory*/MemoryInit moves, Generals+MD), FramePacer,
FrameRateLimit, GameLOD.{h,cpp}, GameDefines.h, GameCommon.h, ReplaySimulation,
Intro.{h,cpp}, LoadScreen, CommandXlat, MetaEvent, Keyboard, W3DMouse, W3DView,
dx8wrapper, surfaceclass, render2dsentence, seglinerenderer, W3DVolumetricShadow,
W3DTerrainTracks, W3DTreeBuffer, W3DTankTruckDraw, W3DTruckDraw, W3DControlBar,
W3DHorizontalSlider, MilesAudioManager, FFmpeg*, GameAudio, ParticleSys,
ArchiveFileSystem.{h,cpp}, UserPreferences, OptionPreferences.{h,cpp}, View.h,
RadiusDecal, Drawable.cpp, ControlBar/ControlBarScheme/ControlBarResizer, Shell.cpp
(pure noise), Radar.cpp, Eva.cpp.

### C — skip (gameplay/balance, mostly unguarded)

All GameLogic/Object, GameLogic/AI, ScriptEngine bulk (ScriptEngine.cpp 341-248,
ScriptActions, ScriptConditions, Scripts), Weapon.cpp, EMPUpdate, TurretAI,
HelicopterSlowDeathUpdate, NeutronMissileUpdate, DeployStyleAIUpdate(+h), DeletionUpdate
(+h), JetSlowDeathBehavior, SlavedUpdate, MissileLauncherBuildingUpdate, PhysicsUpdate,
ParachuteContain, SpecialPowerModule, ObjectCreationList, Object.cpp, AI.cpp, FXList,
RankInfo, CrateSystem, Science, SidesList, Team, Player, CampaignManager, StateMachine,
SwayClientUpdate, ExperienceTracker.h, PerfTimer.h, Overridable.h,
ParkingPlaceBehavior.h, AcademyStats.h, GameStateMap/GameState, INIWebpageURL,
Compression (NoxCompress, EAC), AsciiString, INI.cpp, Debug.cpp.

### Skip wholesale

- All of `Generals/Code/**` (GO is ZH-only; vanilla Generals keeps GameSpy always).
- Infra: .github/workflows/*, resources/dockerbuild-msvc/*, scripts/*, PatchNotes/*,
  MakePatch.ps1, CMakePresets.json hunks, RTS.RC, Generals.ico, .gitignore,
  .editorconfig.
- Vendored-but-dead NAT libs: `Vendor/{miniupnpc,libplum,libnatpmp}` (35 files — not in
  GO's source lists, never included).
- GO's `GeneralsMD/Code/GameEngine/CMakeLists.txt` diff (86-7) and Main/CMakeLists.txt
  rename — contraZH's CMake is hand-edited instead.
