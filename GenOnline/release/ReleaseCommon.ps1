# Shared helpers of the release scripts in this folder. The scripts dot-source this file.

# The two games of a release.
$ReleaseGames = [ordered]@{
   zerohour = @{ Exe = "genonline_zerohour.exe"; BuildSubdir = "GeneralsMD\Release" }
   generals = @{ Exe = "genonline_generals.exe"; BuildSubdir = "Generals\Release" }
}

# The runtime files of this redistributable define what the release exes may import.
$DefaultRedistDir = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.44.35112\x86\Microsoft.VC143.CRT"

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
