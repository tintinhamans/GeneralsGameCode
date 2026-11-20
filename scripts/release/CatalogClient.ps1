#Requires -Version 7.0
# Writes the Easy Anti-Cheat integrity catalog of each staged client folder with anticheat_integritytool.exe of the EOS
# SDK tools, then verifies it. Run it after SignClient.ps1 and after plugin.manifest is signed again: the catalog hashes
# the final bytes, so any later change to a file breaks the anti-cheat start.
#
# The tool signs the catalog with base_private.key next to it. The script passes the path of that key and never reads it.
[CmdletBinding(SupportsShouldProcess = $true)]
param(
   [Parameter(Mandatory = $true)][string]$StageDir,
   [string[]]$Games = @("zerohour", "generals"),
   [Parameter(Mandatory = $true)][string]$EacToolsDir,
   [Parameter(Mandatory = $true)][string]$ProductId,
   [Parameter(Mandatory = $true)][string]$PluginPublicKey,
   [string]$ConfigFile = "",
   [string]$CrashReporterName = "CrashReportHandler.exe",
   [string]$Minisign = "",
   [string]$PublisherCertificateSha256 = ""
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ReleaseCommon.ps1"

Assert-GameNames $Games
Assert-EosId "ProductId" $ProductId
$StageDir = [IO.Path]::GetFullPath($StageDir)
$devtools = Join-Path $EacToolsDir "devtools"
$tool = Join-Path $devtools "anticheat_integritytool.exe"
$key = Join-Path $devtools "base_private.key"
$certificate = Join-Path $devtools "base_public.cer"
$templateConfig = if ($ConfigFile) { $ConfigFile } else { Join-Path $devtools "anticheat_integritytool.cfg" }
foreach ($required in $tool, $key, $certificate, $templateConfig, $PluginPublicKey)
{
   if (-not (Test-Path -LiteralPath $required -PathType Leaf))
   {
      throw "$required is missing."
   }
}

# The SDK template leaves the crash reporter out. The game's own crash handler must be named, or Easy Anti-Cheat
# treats it as a foreign process inside the protected game.
$ConfigFile = Join-Path ([IO.Path]::GetTempPath()) ("genonline-integrity-" + [guid]::NewGuid().ToString("N") + ".cfg")
$configText = [IO.File]::ReadAllText($templateConfig)
$pattern = '/\*\s*crash_reporter_name\s*=\s*"[^"]*"\s*;\s*\*/'
if ($CrashReporterName)
{
   if ($configText -notmatch $pattern)
   {
      throw "$templateConfig has no crash_reporter_name line to set."
   }
   $configText = [regex]::Replace($configText, $pattern, "crash_reporter_name = `"$CrashReporterName`";")
}
[IO.File]::WriteAllText($ConfigFile, $configText)
if (-not $Minisign)
{
   $Minisign = Get-Command minisign -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1 | ForEach-Object Source
}
if (-not $Minisign -or -not (Test-Path -LiteralPath $Minisign -PathType Leaf))
{
   throw "minisign was not found. Install it (winget install jedisct1.minisign) or pass -Minisign."
}
if (-not $PublisherCertificateSha256)
{
   $PublisherCertificateSha256 = $env:GO_PUBLISHER_CERT_SHA256
}
if (-not $WhatIfPreference -and $PublisherCertificateSha256 -notmatch "^[0-9A-Fa-f]{64}$")
{
   throw "Pass -PublisherCertificateSha256 or set GO_PUBLISHER_CERT_SHA256 (64 hex digits)."
}

foreach ($game in $Games)
{
   $gameDir = Join-Path $StageDir $game
   if (-not (Test-Path -LiteralPath $gameDir -PathType Container))
   {
      throw "$gameDir is missing. Stage it with StageClient.ps1."
   }

   # The catalog must hash the final files: the signed binaries and the plugin manifest that matches them.
   if (-not $WhatIfPreference)
   {
      foreach ($file in Get-OwnBinaries $game)
      {
         Assert-Authenticode (Join-Path $gameDir $file) $PublisherCertificateSha256
      }
   }
   $pluginDir = Join-Path $gameDir "plugins\genonline_eos"
   & $Minisign -V -q -p $PluginPublicKey -m (Join-Path $pluginDir "plugin.manifest")
   if ($LASTEXITCODE -ne 0)
   {
      $message = "plugin.manifest of $game does not verify with $PluginPublicKey. Sign the rewritten manifest first."
      if ($WhatIfPreference)
      {
         Write-Warning $message
      }
      else
      {
         throw $message
      }
   }

   $certificates = Join-Path $gameDir "EasyAntiCheat\Certificates"
   if (Test-Path -LiteralPath $certificates)
   {
      throw "$certificates exists already. Remove it to write the catalog again."
   }

   if ($PSCmdlet.ShouldProcess($gameDir, "Write the integrity catalog"))
   {
      $before = @(Get-ChildItem -LiteralPath $gameDir -Recurse -File | ForEach-Object FullName)
      Push-Location $devtools
      try
      {
         Invoke-Native "anticheat_integritytool for $game" {
            & $tool -productid $ProductId.ToLowerInvariant() -inkey $key -incert $certificate -target_game_dir $gameDir $ConfigFile
         }
         Invoke-Native "anticheat_integritytool verify for $game" {
            & $tool -productid $ProductId.ToLowerInvariant() verify $gameDir
         }
      }
      finally
      {
         Pop-Location
      }
      Get-ChildItem -LiteralPath $gameDir -Recurse -File | Where-Object { $before -notcontains $_.FullName } | ForEach-Object {
         Write-Host "Catalog  $game\$($_.FullName.Substring($gameDir.Length + 1))"
      }
   }
}

Remove-Item -LiteralPath $ConfigFile -Force -ErrorAction SilentlyContinue
