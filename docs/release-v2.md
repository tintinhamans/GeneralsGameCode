# Releasing the client on the v2 test channel

This runbook publishes a signed client release of both games on the launcher's v2 **test** channel, hosted in the R2
bucket `online-generals-cdn` behind `https://cdn.generals.online`. Live stays on the legacy content format and its
`MakePatch.ps1` path. The scripts live in `scripts/release/` and need PowerShell 7 (`pwsh`).

Each step names its script. Every script that changes files or the bucket has a `-WhatIf` or `-DryRun` mode that
needs no secret. Run it first.

## What you need

| Input | Where it comes from | Used by |
|---|---|---|
| Visual Studio 18 with the MSVC v143 **14.44** build tools | Visual Studio Installer, individual components | step 2 |
| `VCPKG_ROOT` | your vcpkg checkout | step 2 |
| EOS product id, sandbox id and deployment id of the test deployment | Epic Developer Portal, Product Settings, SDK Download & Credentials | steps 4 and 7 |
| Easy Anti-Cheat SDK tools folder (`AntiCheatTools`, with `dist\` and `devtools\`) | the EOS SDK download, Anti-Cheat tools | steps 4 and 7 |
| Signed `genonline_eos` plugin folder | the plugin release build, for example `D:\gb\genonline-eos-signed-4325591\genonline_eos` | step 4 |
| `BootstrapperIcon.ico` | the Generals Online icon, for example the launcher's `installer\generals-online.ico` | step 4 |
| `genonline-plugin-sdk` checkout | holds `cmake\GeneratePluginManifest.cmake` | step 5 |
| ssign v0.1.10 on `PATH` | <https://github.com/Le-Syl21/ssign/releases>, check the SHA-256 the launcher's `installer/README.md` lists | step 5 |
| `CERTUM_EMAIL`, `GO_PUBLISHER_CERT_SHA256` | the SimplySign account and the SHA-256 of the Certum certificate | steps 5 and 7 |
| SimplySign one-time code | the SimplySign phone app, typed when step 5 asks | step 5 |
| Plugin signing key | 1Password item "GenOnline plugin signing key (current)" | step 6 |
| Plugin public key | `D:\keys\genonline-plugins\genonline-plugins-current.pub` | step 7 |
| minisign and the 1Password CLI `op` | `winget install jedisct1.minisign` and `winget install AgileBits.1Password.CLI` | steps 6 and 7 |
| Content release and channel keys | `%USERPROFILE%\.generalsonline-signing\content\release.key` and `channel.key` | step 8 |
| Their passwords | `GENONLINE_CONTENT_RELEASE_PASSWORD` and `GENONLINE_CONTENT_CHANNEL_PASSWORD`, or typed when patch_pack asks | step 8 |
| GenOnlineLauncher checkout with the v2 publisher | `examples/patch_pack`, `installer/content-*.key.pub` | step 8 |
| AWS CLI profile `generals-online-r2` and `R2_ACCOUNT_ID` | the R2 access key of the bucket and the Cloudflare account id | step 9 |

## Steps

### 1. Tag

```powershell
pwsh -File TagRelease.ps1 -Version 2026.11.0
```

The build names the tag and commit in `audio-libraries-NOTICE.txt`, and step 4 refuses a build without it.

### 2. Build both games with the 14.44 toolset

```powershell
pwsh -File scripts\release\BuildRelease.ps1
```

The script opens `vcvarsamd64_x86.bat -vcvars_ver=14.44`, configures the `win32-vcpkg-release` preset and builds
`z_generals` and `g_generals` into `build\win32-vcpkg-release`. The preset loads `cmake/require-msvc-toolset.cmake`,
which fails the configure step for any compiler other than cl 19.44. Pass `-BuildDir` to build elsewhere.

### 3. Check the imports against the pinned runtime

`BuildRelease.ps1` runs it at the end. To run it alone:

```powershell
pwsh -File scripts\release\CheckImports.ps1 -BuildDir build\win32-vcpkg-release
```

Every function the exes and DLLs import from `MSVCP140`, `MSVCP140_ATOMIC_WAIT`, `VCRUNTIME140` and the other runtime
DLLs must be exported by the redistributable in
`C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.44.35112\x86\Microsoft.VC143.CRT`, the 14.44.35211
runtime players have. The check also reads the runtime version the Zero Hour VC++ runtime check was built to require and
fails when it is newer than 14.44.35211. To run dumpbin by hand in Git Bash, set `MSYS2_ARG_CONV_EXCL='*'` first, or
Git Bash rewrites its `/imports` option into a path.

### 4. Stage the client folders

```powershell
pwsh -File scripts\release\StageClient.ps1 -OutputDir D:\gb\release-2026.11.0\stage `
   -EacToolsDir <AntiCheatTools> -PluginDir <signed genonline_eos folder> -BootstrapperIcon <icon> `
   -ProductId <id> -SandboxId <id> -DeploymentId <id>
```

Each game gets `stage\<game>\` as the launcher's PATCH_FORMAT.md 3.1 describes: `genonline_<game>.exe`,
`CrashReportHandler.exe`, `discord-rpc.dll`, the license texts, `genonline_<game>_protected.exe` (the EAC bootstrapper),
`EasyAntiCheat\` with `Settings.json`, `BootstrapperIcon.ico`, `EasyAntiCheat_EOS_Setup.exe`, `Licenses\` and
`Localization\`, and `plugins\genonline_eos\` with `deployment.json`. `Settings.json` names the game exe, carries the
three ids, hides the bootstrapper, its window and the logo animation, and requests no splash screen.

### 5. Sign (Authenticode)

```powershell
pwsh -File scripts\release\SignClient.ps1 -StageDir D:\gb\release-2026.11.0\stage -PluginSdkDir <genonline-plugin-sdk> -WhatIf
pwsh -File scripts\release\SignClient.ps1 -StageDir D:\gb\release-2026.11.0\stage -PluginSdkDir <genonline-plugin-sdk>
```

The script signs `genonline_<game>.exe`, `CrashReportHandler.exe` and `genonline_eos.dll` with ssign, all in one login:
type the 6-digit SimplySign code when it asks, it expires in about 30 s. Files signed by another publisher (the EOS SDK,
the EAC bootstrapper and setup, Discord RPC) stay as they are. Each signature must be valid, timestamped and by the
certificate `GO_PUBLISHER_CERT_SHA256` names. The script then rewrites `plugin.manifest` of each plugin folder, because
the signed DLL and `deployment.json` change what it must list, and deletes the stale `plugin.manifest.minisig`.

### 6. Sign the plugin manifest again (minisign)

The game refuses a plugin folder whose `plugin.manifest.minisig` does not verify, or that holds a file the manifest does
not list. When step 5 reports identical manifests, sign one and copy the signature:

1. Make a new folder `<scratch>\genonline_eos\` and copy `stage\zerohour\plugins\genonline_eos\plugin.manifest` into it.
2. Copy `D:\gb\genonline-eos-signed-4325591\sign.ps1` into `<scratch>\` and run `pwsh -File <scratch>\sign.ps1 -Vault <vault>`.
   It reads the item "GenOnline plugin signing key (current)" with `op`, signs `plugin.manifest` and verifies it with
   the current public key.
3. Copy `<scratch>\genonline_eos\plugin.manifest.minisig` into `plugins\genonline_eos\` of every staged game.

### 7. Easy Anti-Cheat integrity catalog

```powershell
pwsh -File scripts\release\CatalogClient.ps1 -StageDir D:\gb\release-2026.11.0\stage -EacToolsDir <AntiCheatTools> `
   -ProductId <id> -PluginPublicKey D:\keys\genonline-plugins\genonline-plugins-current.pub
```

The script checks the Authenticode signatures and the plugin manifest signature, then runs
`devtools\anticheat_integritytool.exe -productid <id> -inkey base_private.key -incert base_public.cer -target_game_dir <game folder> anticheat_integritytool.cfg`
and `anticheat_integritytool.exe -productid <id> verify <game folder>`. The catalog lands in
`EasyAntiCheat\Certificates\`. No file of the folder may change after this step.

The script runs the tool with a copy of the SDK configuration that sets `crash_reporter_name` to
`CrashReportHandler.exe`, so Easy Anti-Cheat accepts the game's crash handler. `-CrashReporterName ""` keeps the
template unchanged.

### 8. Pack and promote on the test channel

```powershell
pwsh -File scripts\release\PackClient.ps1 -StageDir D:\gb\release-2026.11.0\stage -Id 2026.11.0 `
   -PublishDir D:\gb\genonline-v2-publish -LauncherDir <GenOnlineLauncher> -DryRun
pwsh -File scripts\release\PackClient.ps1 -StageDir D:\gb\release-2026.11.0\stage -Id 2026.11.0 `
   -PublishDir D:\gb\genonline-v2-publish -LauncherDir <GenOnlineLauncher>
```

For each game the script computes the game exe CRC, runs `patch_pack client`, takes the published test pointer from
the CDN when it is newer than the one in the publish folder, and runs `patch_pack promote --channel test`. It ends with
`patch_pack verify`. Keep the publish folder between releases. Official packs go in with `-Pack generals_online=<id>`.

### 9. Upload

```powershell
pwsh -File scripts\release\UploadClient.ps1 -PublishDir D:\gb\genonline-v2-publish -DryRun
pwsh -File scripts\release\UploadClient.ps1 -PublishDir D:\gb\genonline-v2-publish
```

The order is `v2/objects`, then `v2/releases`, then `v2/channels/test/<game>.json` and its `.minisig` last. An object
that exists is skipped, an existing release manifest must hold the same bytes, and a pointer only replaces a published
one with a higher sequence.

## Known gaps

- `patch_pack client` admits only client scope files (code, `plugins\`, anti-cheat files). The license texts at the root
  of the client folder are outside it, so step 8 fails until the launcher's `packs::client_scope` admits them.
- `GeneralsMD/Code/Main/CMakeLists.txt` stamps the VC++ runtime check with the default redistributable of the Visual
  Studio install (14.51 on VS 18), not the one of the 14.44 toolset. Step 3 fails until that is fixed.
- No code reads `deployment.json` yet. The plugin has its ids compiled in.
