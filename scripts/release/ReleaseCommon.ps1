# Shared helpers of the release scripts in this folder. The scripts dot-source this file.

# The two games of a release. Version is TheVersion->getVersionNumber() of the build, which feeds the game exe CRC.
$ReleaseGames = [ordered]@{
   zerohour = @{ Exe = "genonline_zerohour.exe"; BuildSubdir = "GeneralsMD\Release"; Version = 0x00010004; Title = "Generals Online Zero Hour" }
   generals = @{ Exe = "genonline_generals.exe"; BuildSubdir = "Generals\Release"; Version = 0x00010008; Title = "Generals Online Generals" }
}

# The runtime files of this redistributable define what the release exes may import.
$DefaultRedistDir = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.44.35112\x86\Microsoft.VC143.CRT"

# Files of a staged client folder that the Generals Online certificate signs, relative to the folder.
function Get-OwnBinaries([string]$Game)
{
   return @($ReleaseGames[$Game].Exe, "CrashReportHandler.exe", "plugins\genonline_eos\genonline_eos.dll")
}

function Invoke-Native([string]$What, [scriptblock]$Command)
{
   & $Command
   if ($LASTEXITCODE -ne 0)
   {
      throw "$What failed with exit code $LASTEXITCODE."
   }
}

function Get-RepositoryRoot
{
   $root = git -C $PSScriptRoot rev-parse --show-toplevel
   if ($LASTEXITCODE -ne 0)
   {
      throw "$PSScriptRoot is not inside a git checkout."
   }
   return [IO.Path]::GetFullPath($root)
}

function Assert-GameNames([string[]]$Games)
{
   foreach ($game in $Games)
   {
      if (-not $ReleaseGames.Contains($game))
      {
         throw "Unknown game '$game'. Use zerohour or generals."
      }
   }
}

function Assert-EosId([string]$Name, [string]$Value)
{
   if ($Value -notmatch "^[0-9a-fA-F]{32}$")
   {
      throw "$Name must be the 32 hex digits the Epic developer portal shows."
   }
}

# The installation folder of the newest Visual Studio with the C++ x86 tools.
function Get-VsInstallPath([string]$VsPath)
{
   if ($VsPath)
   {
      return $VsPath
   }
   $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
   if (Test-Path -LiteralPath $vswhere -PathType Leaf)
   {
      $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
      if ($LASTEXITCODE -eq 0 -and $found)
      {
         return ($found | Select-Object -First 1)
      }
   }
   throw "No Visual Studio with the C++ tools was found. Pass -VsPath."
}

# dumpbin of the pinned toolset, or of the newest one when the pinned toolset is missing.
function Get-Dumpbin([string]$VsPath, [string]$Toolset)
{
   $msvc = Join-Path (Get-VsInstallPath $VsPath) "VC\Tools\MSVC"
   $dirs = @(Get-ChildItem -LiteralPath $msvc -Directory | Where-Object { $_.Name -like "$Toolset.*" } | Sort-Object Name -Descending)
   if ($dirs.Count -eq 0)
   {
      $dirs = @(Get-ChildItem -LiteralPath $msvc -Directory | Sort-Object Name -Descending)
   }
   foreach ($dir in $dirs)
   {
      $dumpbin = Join-Path $dir.FullName "bin\Hostx64\x86\dumpbin.exe"
      if (Test-Path -LiteralPath $dumpbin -PathType Leaf)
      {
         return $dumpbin
      }
   }
   throw "dumpbin.exe was not found below $msvc."
}

# Throws unless the file carries a valid, timestamped Authenticode signature by the pinned certificate.
function Assert-Authenticode([string]$File, [string]$CertificateSha256)
{
   $signature = Get-AuthenticodeSignature -LiteralPath $File
   if ($signature.Status -ne "Valid" -or $null -eq $signature.SignerCertificate)
   {
      throw "$File has no valid Authenticode signature ($($signature.Status): $($signature.StatusMessage))."
   }
   $hash = $signature.SignerCertificate.GetCertHashString([Security.Cryptography.HashAlgorithmName]::SHA256)
   if ($hash -ine $CertificateSha256)
   {
      throw "$File is signed by $($signature.SignerCertificate.Subject) (SHA-256 $hash), not the pinned publisher certificate."
   }
   if ($null -eq $signature.TimeStamperCertificate)
   {
      throw "$File carries no timestamp, so its signature would expire with the certificate."
   }
}

# The game exe CRC of GlobalData::generateExeCRC: the exe bytes, the version number and the two retail multiplayer scripts,
# summed with the rotate and add CRC of Common/crc.h.
function Get-GameExeCrc([string]$Exe, [uint32]$Version, [string]$ScriptsDir)
{
   if (-not ("GenOnlineRelease.GameCrc" -as [type]))
   {
      Add-Type -TypeDefinition @"
namespace GenOnlineRelease
{
   public static class GameCrc
   {
      public static uint Add(uint crc, byte[] data)
      {
         foreach (byte b in data)
         {
            crc = unchecked((crc << 1) + b + (crc >> 31));
         }
         return crc;
      }
   }
}
"@
   }
   $crc = [GenOnlineRelease.GameCrc]::Add(0, [IO.File]::ReadAllBytes($Exe))
   $crc = [GenOnlineRelease.GameCrc]::Add($crc, [BitConverter]::GetBytes($Version))
   foreach ($script in "SkirmishScripts.scb", "MultiplayerScripts.scb")
   {
      $path = Join-Path $ScriptsDir $script
      if (-not (Test-Path -LiteralPath $path -PathType Leaf))
      {
         throw "$path is missing. The game exe CRC needs the retail $script."
      }
      $crc = [GenOnlineRelease.GameCrc]::Add($crc, [IO.File]::ReadAllBytes($path))
   }
   return "{0:X8}" -f $crc
}

# The retail Data\Scripts folder of a game, read from the install path the game registers.
function Get-RetailScriptsDir([string]$Game)
{
   $key = if ($Game -eq "zerohour") { "SOFTWARE\Electronic Arts\EA Games\Command and Conquer Generals Zero Hour" } else { "SOFTWARE\Electronic Arts\EA Games\Generals" }
   foreach ($view in "Registry32", "Registry64")
   {
      $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey("LocalMachine", $view)
      $entry = $base.OpenSubKey($key)
      if ($entry -and $entry.GetValue("InstallPath"))
      {
         return Join-Path $entry.GetValue("InstallPath") "Data\Scripts"
      }
   }
   throw "No retail $Game install is registered. Pass the folder that holds SkirmishScripts.scb and MultiplayerScripts.scb."
}
