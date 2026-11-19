#Requires -Version 7.0
# Signs the Generals Online binaries of the staged client folders with the Certum SimplySign certificate through ssign,
# like the launcher's installer/package.ps1. It signs the game exe, CrashReportHandler.exe and genonline_eos.dll.
# Files that are signed by another publisher already, such as the EOS SDK, the EAC bootstrapper and Discord RPC, stay as
# they are. Identical files of both games are signed once and copied, so the two folders share their bytes.
#
# Signing changes genonline_eos.dll, so the script then rewrites plugin.manifest of each plugin folder with the generator
# of the plugin SDK. The new manifest needs a new minisign signature by the plugin key before the game loads the plugin.
#
# Inputs: CERTUM_EMAIL (the SimplySign account), GO_PUBLISHER_CERT_SHA256 (the certificate SHA-256), ssign v0.1.10, and the
# one-time code of the SimplySign app, typed when asked. One code covers the whole batch.
[CmdletBinding(SupportsShouldProcess = $true)]
param(
   [Parameter(Mandatory = $true)][string]$StageDir,
   [string[]]$Games = @("zerohour", "generals"),
   [Parameter(Mandatory = $true)][string]$PluginSdkDir,
   [string]$Ssign = "",
   [string]$CMake = "",
   [string]$CertumEmail = "",
   [string]$PublisherCertificateSha256 = ""
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ReleaseCommon.ps1"

Assert-GameNames $Games
$StageDir = [IO.Path]::GetFullPath($StageDir)
$manifestScript = Join-Path $PluginSdkDir "cmake\GeneratePluginManifest.cmake"
if (-not (Test-Path -LiteralPath $manifestScript -PathType Leaf))
{
   throw "$manifestScript is missing. -PluginSdkDir must be the genonline-plugin-sdk checkout."
}
# The plugin manifest generator runs with cmake -P. Without cmake on PATH the copy bundled with Visual Studio serves.
if (-not $CMake)
{
   $CMake = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1 | ForEach-Object Source
}
if (-not $CMake)
{
   $CMake = Join-Path (Get-VsInstallPath "") "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
if (-not (Test-Path -LiteralPath $CMake -PathType Leaf))
{
   throw "cmake was not found. Pass -CMake."
}

if (-not $PublisherCertificateSha256)
{
   $PublisherCertificateSha256 = $env:GO_PUBLISHER_CERT_SHA256
}
if (-not $WhatIfPreference)
{
   if (-not $Ssign)
   {
      $Ssign = Get-Command ssign -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1 | ForEach-Object Source
   }
   if (-not $Ssign -or -not (Test-Path -LiteralPath $Ssign -PathType Leaf))
   {
      throw "ssign was not found. Install v0.1.10 as docs/release-v2.md describes, or pass -Ssign."
   }
   if (-not $CertumEmail)
   {
      $CertumEmail = $env:CERTUM_EMAIL
   }
   if (-not $CertumEmail)
   {
      throw "Pass -CertumEmail or set CERTUM_EMAIL (the SimplySign account)."
   }
   if ($PublisherCertificateSha256 -notmatch "^[0-9A-Fa-f]{64}$")
   {
      throw "Pass -PublisherCertificateSha256 or set GO_PUBLISHER_CERT_SHA256 (64 hex digits)."
   }
}

# Sorts every PE file of the staged folders into own files to sign and third party files to keep.
$groups = [ordered]@{}
foreach ($game in $Games)
{
   $gameDir = Join-Path $StageDir $game
   if (-not (Test-Path -LiteralPath $gameDir -PathType Container))
   {
      throw "$gameDir is missing. Stage it with StageClient.ps1."
   }
   $own = @(Get-OwnBinaries $game | ForEach-Object { Join-Path $gameDir $_ })
   foreach ($file in Get-ChildItem -LiteralPath $gameDir -Recurse -File | Where-Object { $_.Extension -in ".exe", ".dll" })
   {
      $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
      if ($own -notcontains $file.FullName)
      {
         if ($signature.Status -eq "Valid")
         {
            Write-Host "Keep     $game\$($file.FullName.Substring($gameDir.Length + 1)), signed by $($signature.SignerCertificate.GetNameInfo('SimpleName', $false))"
         }
         else
         {
            Write-Warning "$($file.FullName) is a third party file without a valid signature ($($signature.Status)). It ships unsigned."
         }
         continue
      }
      if ($signature.Status -eq "Valid")
      {
         $hash = $signature.SignerCertificate.GetCertHashString([Security.Cryptography.HashAlgorithmName]::SHA256)
         if ($PublisherCertificateSha256 -and $hash -ieq $PublisherCertificateSha256)
         {
            Write-Host "Signed   $game\$($file.FullName.Substring($gameDir.Length + 1)) already"
            continue
         }
         throw "$($file.FullName) is signed by $($signature.SignerCertificate.Subject) already. Stage the unsigned build output."
      }
      if ($signature.Status -ne "NotSigned")
      {
         throw "$($file.FullName) has a broken signature ($($signature.Status)). Stage it again."
      }
      $content = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
      if (-not $groups.Contains($content))
      {
         $groups[$content] = [Collections.Generic.List[string]]::new()
      }
      $groups[$content].Add($file.FullName)
   }
}

foreach ($group in $groups.Values)
{
   Write-Host "Sign     $($group[0].Substring($StageDir.Length + 1))"
   for ($i = 1; $i -lt $group.Count; $i++)
   {
      Write-Host "  copy   $($group[$i].Substring($StageDir.Length + 1))"
   }
}

if ($groups.Count -gt 0 -and $PSCmdlet.ShouldProcess("$($groups.Count) file(s)", "Sign with ssign"))
{
   $code = Read-Host "Certum SimplySign one-time code (6 digits)"
   if ($code -notmatch "^\s*\d{6}\s*$")
   {
      throw "The one-time code must be 6 digits."
   }
   $env:CERTUM_EMAIL = $CertumEmail
   $env:CERTUM_TOKEN = $code.Trim()
   # ssign logs in with the code at once, which expires in about 30 s, and signs every file with that login.
   try
   {
      $first = @($groups.Values | ForEach-Object { $_[0] })
      Invoke-Native "ssign" { & $Ssign -n "Generals Online" -u "https://www.playgenerals.online" @first }
   }
   finally
   {
      Remove-Item Env:CERTUM_TOKEN -ErrorAction SilentlyContinue
   }

   foreach ($group in $groups.Values)
   {
      Assert-Authenticode $group[0] $PublisherCertificateSha256
      for ($i = 1; $i -lt $group.Count; $i++)
      {
         Copy-Item -LiteralPath $group[0] -Destination $group[$i] -Force
         Assert-Authenticode $group[$i] $PublisherCertificateSha256
      }
      foreach ($file in $group)
      {
         Write-Host "Authenticode OK: $($file.Substring($StageDir.Length + 1))"
      }
   }
}

# Rewrites plugin.manifest from the files of each plugin folder, deployment.json and the signed DLL included.
$manifests = @{}
foreach ($game in $Games)
{
   $pluginDir = Join-Path $StageDir "$game\plugins\genonline_eos"
   $manifestPath = Join-Path $pluginDir "plugin.manifest"
   $version = (Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json).version
   if ($PSCmdlet.ShouldProcess($manifestPath, "Rewrite for version $version"))
   {
      Remove-Item -LiteralPath (Join-Path $pluginDir "plugin.manifest.minisig") -ErrorAction SilentlyContinue
      Invoke-Native "GeneratePluginManifest.cmake" {
         & $CMake "-DPLUGIN_DIR=$pluginDir" -DPLUGIN_NAME=genonline_eos "-DPLUGIN_VERSION=$version" -DPLUGIN_API_VERSION=1 -P $manifestScript
      }
      $manifests[$game] = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
   }
}

if ($manifests.Count -gt 0)
{
   $distinct = @($manifests.Values | Select-Object -Unique)
   Write-Host ""
   if ($distinct.Count -eq 1)
   {
      Write-Host "The plugin manifests of all games are identical. Sign one and copy plugin.manifest.minisig into every plugin folder."
   }
   else
   {
      Write-Host "The plugin manifests differ between the games. Sign each one."
   }
   Write-Host "Next: sign plugin.manifest with the plugin key (docs/release-v2.md, step 6), then run CatalogClient.ps1."
}
