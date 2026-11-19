#Requires -Version 7.0
# Stages the v2 client folder of each game, laid out as the launcher's PATCH_FORMAT.md 3.1 describes:
#   <OutputDir>\<game>\genonline_<game>.exe             the game, with CrashReportHandler.exe, discord-rpc.dll and the license texts
#   <OutputDir>\<game>\genonline_<game>_protected.exe   the Easy Anti-Cheat bootstrapper, renamed
#   <OutputDir>\<game>\EasyAntiCheat\                   Settings.json, BootstrapperIcon.ico and the runtime files of the bootstrapper
#   <OutputDir>\<game>\plugins\genonline_eos\           the plugin with deployment.json
# The folders are ready for SignClient.ps1, which signs the binaries and rewrites the plugin manifest.
[CmdletBinding(SupportsShouldProcess = $true)]
param(
   [Parameter(Mandatory = $true)][string]$OutputDir,
   [string[]]$Games = @("zerohour", "generals"),
   [string]$BuildDir = "build\win32-vcpkg-release",
   [Parameter(Mandatory = $true)][string]$EacToolsDir,
   [Parameter(Mandatory = $true)][string]$PluginDir,
   [Parameter(Mandatory = $true)][string]$BootstrapperIcon,
   [Parameter(Mandatory = $true)][string]$ProductId,
   [Parameter(Mandatory = $true)][string]$SandboxId,
   [Parameter(Mandatory = $true)][string]$DeploymentId,
   [switch]$AllowSuffixedExe,
   [switch]$SkipNoticeCheck
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ReleaseCommon.ps1"

$root = Get-RepositoryRoot
$BuildDir = [IO.Path]::GetFullPath($BuildDir, $root)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
Assert-GameNames $Games
Assert-EosId "ProductId" $ProductId
Assert-EosId "SandboxId" $SandboxId
Assert-EosId "DeploymentId" $DeploymentId

# The EAC SDK tools folder: the bootstrapper and the runtime files it installs and loads.
$eacDist = Join-Path $EacToolsDir "dist"
$bootstrapper = Join-Path $eacDist "start_protected_game.exe"
$eacFolder = Join-Path $eacDist "EasyAntiCheat"
foreach ($required in $bootstrapper, (Join-Path $eacFolder "EasyAntiCheat_EOS_Setup.exe"), (Join-Path $eacFolder "Licenses"), (Join-Path $eacFolder "Localization"))
{
   if (-not (Test-Path -LiteralPath $required))
   {
      throw "$required is missing. -EacToolsDir must be the AntiCheatTools folder of the EOS SDK."
   }
}
foreach ($required in (Join-Path $PluginDir "genonline_eos.dll"), (Join-Path $PluginDir "plugin.manifest"), $BootstrapperIcon)
{
   if (-not (Test-Path -LiteralPath $required -PathType Leaf))
   {
      throw "$required is missing."
   }
}

if ((Test-Path -LiteralPath $OutputDir) -and @(Get-ChildItem -LiteralPath $OutputDir -Force).Count -gt 0)
{
   throw "$OutputDir is not empty. Stage each release into a new folder."
}

# The release exe of a game. A build with RTS_BUILD_OUTPUT_SUFFIX names it genonline_<game><suffix>.exe.
function Find-GameExe([string]$Game, [string]$Dir)
{
   $exe = Join-Path $Dir $ReleaseGames[$Game].Exe
   if (Test-Path -LiteralPath $exe -PathType Leaf)
   {
      return $exe
   }
   $candidates = @(Get-ChildItem -LiteralPath $Dir -Filter "genonline_$Game*.exe" -File)
   if ($AllowSuffixedExe -and $candidates.Count -eq 1)
   {
      Write-Warning "Using $($candidates[0].Name) as $($ReleaseGames[$Game].Exe). Only a test stage may do this."
      return $candidates[0].FullName
   }
   throw "$exe is missing. Build with scripts\release\BuildRelease.ps1."
}

function Write-JsonFile([string]$Path, [object]$Value)
{
   if ($PSCmdlet.ShouldProcess($Path, "Write"))
   {
      [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 4) + "`n", [Text.UTF8Encoding]::new($false))
   }
}

foreach ($game in $Games)
{
   $info = $ReleaseGames[$game]
   $buildOutput = Join-Path $BuildDir $info.BuildSubdir
   $gameDir = Join-Path $OutputDir $game
   $exe = Find-GameExe $game $buildOutput

   # The audio notice must name the release tag and the exact commit that the LGPL source offer points to.
   $notice = Get-Content -Raw -LiteralPath (Join-Path $buildOutput "audio-libraries-NOTICE.txt")
   if ($notice -notmatch "built from tag v\d{4}\.\d{1,2}\.\d+(-rc\.\d+)?, commit [0-9a-f]{40}\r?\n")
   {
      if (-not $SkipNoticeCheck)
      {
         throw "audio-libraries-NOTICE.txt of $game names no release tag and exact commit. Tag the commit with TagRelease.ps1 and build it from a clean checkout."
      }
      Write-Warning "audio-libraries-NOTICE.txt of $game names no release tag. Only a test stage may skip this check."
   }

   New-Item -ItemType Directory -Path $gameDir, (Join-Path $gameDir "EasyAntiCheat"), (Join-Path $gameDir "plugins\genonline_eos") -Force | Out-Null

   # The game, its crash handler, Discord RPC and the license texts that ship with the exe.
   Copy-Item -LiteralPath $exe -Destination (Join-Path $gameDir $info.Exe)
   foreach ($file in "CrashReportHandler.exe", "discord-rpc.dll", "discord-rpc-LICENSE.txt", "THIRD_PARTY_NOTICES.txt", "LICENSE-GPL3.txt",
      "audio-libraries-NOTICE.txt", "ffmpeg-LICENSE.txt", "openal-soft-LICENSE.txt", "openal-soft-pffft-LICENSE.txt")
   {
      Copy-Item -LiteralPath (Join-Path $buildOutput $file) -Destination $gameDir
   }
   Copy-Item -LiteralPath (Join-Path $root "GenOnline\GameData\Fonts\IBMPlex-LICENSE.txt") -Destination $gameDir

   # Easy Anti-Cheat: the bootstrapper under the name the launcher starts, and the files it needs. No splash screen.
   Copy-Item -LiteralPath $bootstrapper -Destination (Join-Path $gameDir "genonline_$($game)_protected.exe")
   Copy-Item -LiteralPath (Join-Path $eacFolder "EasyAntiCheat_EOS_Setup.exe") -Destination (Join-Path $gameDir "EasyAntiCheat")
   Copy-Item -LiteralPath (Join-Path $eacFolder "Licenses"), (Join-Path $eacFolder "Localization") -Destination (Join-Path $gameDir "EasyAntiCheat") -Recurse
   Copy-Item -LiteralPath $BootstrapperIcon -Destination (Join-Path $gameDir "EasyAntiCheat\BootstrapperIcon.ico")

   # The keys of the SDK template, without requested_splash, with the bootstrapper and its window hidden.
   Write-JsonFile (Join-Path $gameDir "EasyAntiCheat\Settings.json") ([ordered]@{
         title                      = $info.Title
         executable                 = $info.Exe
         productid                  = $ProductId.ToLowerInvariant()
         sandboxid                  = $SandboxId.ToLowerInvariant()
         deploymentid               = $DeploymentId.ToLowerInvariant()
         wait_for_game_process_exit = "false"
         hide_bootstrapper          = "true"
         hide_gui                   = "true"
         hide_logo_animation        = "true"
      })

   # The plugin as built and signed by its repository. Its manifest is rewritten by SignClient.ps1, because the
   # client refuses a plugin folder that holds a file the manifest does not list, such as deployment.json.
   Copy-Item -Path (Join-Path $PluginDir "*") -Destination (Join-Path $gameDir "plugins\genonline_eos") -Recurse
   Write-JsonFile (Join-Path $gameDir "plugins\genonline_eos\deployment.json") ([ordered]@{
         productid    = $ProductId.ToLowerInvariant()
         sandboxid    = $SandboxId.ToLowerInvariant()
         deploymentid = $DeploymentId.ToLowerInvariant()
      })

   Write-Host "Staged $gameDir"
}

if (-not $WhatIfPreference)
{
   Get-ChildItem -LiteralPath $OutputDir -Recurse -File | Sort-Object FullName | ForEach-Object {
      "{0,10}  {1}" -f $_.Length, $_.FullName.Substring($OutputDir.Length + 1)
   }
   Write-Host "Next: SignClient.ps1 -StageDir $OutputDir"
}
