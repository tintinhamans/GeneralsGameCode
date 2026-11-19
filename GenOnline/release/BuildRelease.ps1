#Requires -Version 7.0
# Builds both games for a release with the pinned MSVC toolset, then checks the exes against the pinned VC++ runtime.
# Run it on the tagged commit (GenOnline/release/GenOnline/release/TagRelease.ps1): the build names the tag in audio-libraries-NOTICE.txt.
# The win32-vcpkg-release preset refuses any other compiler, and this script opens the toolset itself.
param(
   [string]$BuildDir = "build\win32-vcpkg-release",
   [string]$Toolset = "14.44",
   [string]$VsPath = "",
   [string]$RedistDir = "",
   [switch]$AllowUntagged,
   # Empty follows the release tag. An untagged test build passes test to reach the test services.
   [ValidateSet("", "live", "test", "dev")]
   [string]$ServicesEnvironment = ""
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ReleaseCommon.ps1"

$root = Get-RepositoryRoot
Set-Location $root
$BuildDir = [IO.Path]::GetFullPath($BuildDir, $root)

git diff --quiet HEAD --
if ($LASTEXITCODE -ne 0)
{
   throw "The checkout has uncommitted changes. A release build must match its tag."
}
$tags = @(git tag --points-at HEAD | Where-Object { $_ -match "^v\d{4}\.\d{1,2}\.\d+(-rc\.\d+)?$" })
if ($tags.Count -eq 0 -and -not $AllowUntagged)
{
   throw "HEAD carries no release tag. Tag it with GenOnline/release/GenOnline/release/TagRelease.ps1 first, or pass -AllowUntagged for a test build."
}
if (-not $env:VCPKG_ROOT)
{
   throw "VCPKG_ROOT is not set. The win32-vcpkg presets need it."
}

# Loads the environment of the x86 tools of the pinned toolset into this session.
$vcvars = Join-Path (Get-VsInstallPath $VsPath) "VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf))
{
   throw "$vcvars does not exist."
}
$environment = cmd /d /c "`"$vcvars`" -vcvars_ver=$Toolset >nul 2>&1 && set"
if ($LASTEXITCODE -ne 0)
{
   throw "vcvarsamd64_x86.bat -vcvars_ver=$Toolset failed. Install the MSVC v143 $Toolset build tools in the Visual Studio Installer."
}
foreach ($line in $environment)
{
   if ($line -match "^([^=]+)=(.*)$")
   {
      Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
   }
}
$cl = (Get-Command cl.exe -CommandType Application | Select-Object -First 1).Source
if ($cl -notmatch "\\VC\\Tools\\MSVC\\$([regex]::Escape($Toolset))\.")
{
   throw "cl.exe is $cl, not the MSVC $Toolset toolset."
}
Write-Host "Compiler: $cl"

# Each build keeps its own vcpkg buildtrees and cleans them, so no two builds share one.
$vcpkgOptions = "--x-buildtrees-root=$BuildDir/vcpkg-buildtrees;--clean-buildtrees-after-build;--clean-packages-after-build"
# The value is always passed so a reused build folder does not keep the environment of an earlier build.
Invoke-Native "Configure" { cmake --preset win32-vcpkg-release -B $BuildDir "-DVCPKG_INSTALL_OPTIONS=$vcpkgOptions" "-DGENONLINE_ENVIRONMENT=$ServicesEnvironment" }
Invoke-Native "Build" { cmake --build $BuildDir --config Release --target z_generals g_generals }

foreach ($game in $ReleaseGames.Keys)
{
   $exe = Join-Path $BuildDir (Join-Path $ReleaseGames[$game].BuildSubdir $ReleaseGames[$game].Exe)
   if (-not (Test-Path -LiteralPath $exe -PathType Leaf))
   {
      throw "The build did not produce $exe."
   }
   Write-Host "Built $exe"
}

& "$PSScriptRoot\CheckImports.ps1" -BuildDir $BuildDir -RedistDir $RedistDir -Toolset $Toolset -VsPath $VsPath
