#Requires -Version 7.0
# Publishes the staged client folder of each game as a v2 client release and points a channel at it, with the
# launcher's patch_pack (PATCH_FORMAT.md 10). The publish folder mirrors the v2/ tree of the CDN, so UploadClient.ps1
# syncs it as it is. Run it after CatalogClient.ps1: the release pins the bytes of every file.
#
# The release and channel keys are the encrypted minisign keys of PATCH_FORMAT.md 11.1. Their passwords come from the
# environment variables -ReleasePasswordEnv and -ChannelPasswordEnv name, or patch_pack asks for them. Nothing prints them.
#
# --game-exe-crc is the game's own exe CRC (GlobalData::generateExeCRC) over the signed exe, the version number of the
# build and the retail SkirmishScripts.scb and MultiplayerScripts.scb, which Generals Online does not replace.
param(
   [Parameter(Mandatory = $true)][string]$StageDir,
   [string[]]$Games = @("zerohour", "generals"),
   [Parameter(Mandatory = $true)][string]$Id,
   [Parameter(Mandatory = $true)][string]$PublishDir,
   [Parameter(Mandatory = $true)][string]$LauncherDir,
   [ValidateSet("test", "dev")][string]$Channel = "test",
   [string]$KeyDir = (Join-Path $env:USERPROFILE ".generalsonline-signing\content"),
   [string]$ReleasePasswordEnv = "GENONLINE_CONTENT_RELEASE_PASSWORD",
   [string]$ChannelPasswordEnv = "GENONLINE_CONTENT_CHANNEL_PASSWORD",
   [string]$Created = "",
   [string]$Expires = "",
   [string]$MinLauncher = "",
   [string[]]$Pack = @(),
   [string[]]$Optional = @(),
   [hashtable]$ScriptsDir = @{},
   [hashtable]$GameExeCrc = @{},
   [string]$CdnBaseUrl = "https://cdn.generals.online",
   [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ReleaseCommon.ps1"

Assert-GameNames $Games
if ($Id -notmatch "^[A-Za-z0-9._-]+$")
{
   throw "The release id '$Id' may only hold letters, digits, dots, underscores and hyphens."
}
$StageDir = [IO.Path]::GetFullPath($StageDir)
$PublishDir = [IO.Path]::GetFullPath($PublishDir)
$releaseKey = Join-Path $KeyDir "release.key"
$channelKey = Join-Path $KeyDir "channel.key"
# The public keys the launcher trusts, so a release signed with another key fails here and not on the players' machines.
$releasePub = Join-Path $LauncherDir "installer\content-release.key.pub"
$channelPub = Join-Path $LauncherDir "installer\content-channel.key.pub"
foreach ($required in $releasePub, $channelPub, (Join-Path $LauncherDir "examples\patch_pack\v2.rs"))
{
   if (-not (Test-Path -LiteralPath $required -PathType Leaf))
   {
      throw "$required is missing. -LauncherDir must be the GenOnlineLauncher checkout."
   }
}
if (-not $DryRun)
{
   foreach ($required in $releaseKey, $channelKey)
   {
      if (-not (Test-Path -LiteralPath $required -PathType Leaf))
      {
         throw "$required is missing. Pass -KeyDir."
      }
   }
}
if (-not $Created)
{
   $Created = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
}

# Runs patch_pack from the launcher checkout. The arguments hold paths and names, never a password.
function Invoke-PatchPack([string[]]$Arguments)
{
   Write-Host "patch_pack $($Arguments -join ' ')"
   if ($DryRun)
   {
      return
   }
   Push-Location $LauncherDir
   try
   {
      Invoke-Native "patch_pack $($Arguments[0])" { cargo run --release --quiet --example patch_pack -- @Arguments }
   }
   finally
   {
      Pop-Location
   }
}

function Get-PasswordArguments([string]$Variable)
{
   if ($Variable -and [Environment]::GetEnvironmentVariable($Variable))
   {
      return @("--password-env", $Variable)
   }
   Write-Host "$Variable is not set, so patch_pack asks for the key password."
   return @()
}

foreach ($game in $Games)
{
   $gameDir = Join-Path $StageDir $game
   $exe = Join-Path $gameDir $ReleaseGames[$game].Exe
   if (-not (Test-Path -LiteralPath $exe -PathType Leaf))
   {
      throw "$exe is missing. Stage, sign and catalog the client first."
   }
   if (-not $DryRun -and -not (Test-Path -LiteralPath (Join-Path $gameDir "EasyAntiCheat\Certificates") -PathType Container))
   {
      throw "$gameDir has no Easy Anti-Cheat catalog. Run CatalogClient.ps1 first."
   }

   $crc = $GameExeCrc[$game]
   if (-not $crc)
   {
      $scripts = $ScriptsDir[$game]
      if (-not $scripts)
      {
         $scripts = Get-RetailScriptsDir $game
      }
      $crc = Get-GameExeCrc $exe $ReleaseGames[$game].Version $scripts
   }
   if ($crc -notmatch "^[0-9A-Fa-f]{8}$")
   {
      throw "The game exe CRC '$crc' of $game is not 8 hex digits."
   }
   Write-Host "Game exe CRC of $game`: $crc"

   # promote numbers the pointer after the one in the publish folder, so the folder needs the pointer players see now.
   $pointer = Join-Path $PublishDir "v2\channels\$Channel\$game.json"
   $url = "$CdnBaseUrl/v2/channels/$Channel/$game.json"
   if ($DryRun)
   {
      Write-Host "Would fetch $url when it is newer than $pointer"
   }
   else
   {
      $response = Invoke-WebRequest -Uri $url -Headers @{ "Cache-Control" = "no-cache" } -SkipHttpErrorCheck
      if ($response.StatusCode -eq 200)
      {
         $remote = [Text.Encoding]::UTF8.GetString($response.Content)
         $remoteSequence = ($remote | ConvertFrom-Json).sequence
         $localSequence = if (Test-Path -LiteralPath $pointer) { (Get-Content -Raw -LiteralPath $pointer | ConvertFrom-Json).sequence } else { 0 }
         if ($remoteSequence -gt $localSequence)
         {
            $signature = Invoke-WebRequest -Uri "$url.minisig" -Headers @{ "Cache-Control" = "no-cache" }
            New-Item -ItemType Directory -Path (Split-Path $pointer) -Force | Out-Null
            [IO.File]::WriteAllBytes($pointer, $response.Content)
            [IO.File]::WriteAllBytes("$pointer.minisig", $signature.Content)
            Write-Host "Took the published $Channel pointer of $game, sequence $remoteSequence."
         }
      }
      elseif ($response.StatusCode -ne 404)
      {
         throw "Cannot read $url (HTTP $($response.StatusCode))."
      }
   }

   $client = @("client", $gameDir, "--publish", $PublishDir, "--game", $game, "--id", $Id, "--game-exe-crc", $crc.ToUpperInvariant(),
      "--created", $Created, "--release-key", $releaseKey)
   if ($MinLauncher)
   {
      $client += @("--min-launcher", $MinLauncher)
   }
   Invoke-PatchPack ($client + (Get-PasswordArguments $ReleasePasswordEnv))

   $promote = @("promote", "--publish", $PublishDir, "--channel", $Channel, "--game", $game, "--client", $Id, "--created", $Created,
      "--release-pub", $releasePub, "--channel-key", $channelKey)
   foreach ($entry in $Pack)
   {
      $promote += @("--pack", $entry)
   }
   foreach ($entry in $Optional)
   {
      $promote += @("--optional", $entry)
   }
   if ($Expires)
   {
      $promote += @("--expires", $Expires)
   }
   Invoke-PatchPack ($promote + (Get-PasswordArguments $ChannelPasswordEnv))
}

Invoke-PatchPack @("verify", "--publish", $PublishDir, "--release-pub", $releasePub, "--channel-pub", $channelPub)
if (-not $DryRun)
{
   Write-Host "Next: UploadClient.ps1 -PublishDir $PublishDir -Channel $Channel -DryRun"
}
