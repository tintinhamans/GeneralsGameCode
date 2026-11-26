#Requires -Version 7.0
# Checks that the release exes run on the pinned VC++ runtime. Every function an exe or DLL imports from a runtime DLL,
# delay loaded imports included, must be exported by that DLL in the bundled redistributable. A newer toolset can import
# functions that only a newer runtime exports, and the loader then refuses to start the game.
param(
   [string]$BuildDir = "",
   [string[]]$Files = @(),
   [string]$RedistDir = "",
   [string]$MaxRuntime = "14.44.35211",
   [string]$Toolset = "14.44",
   [string]$VsPath = ""
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ReleaseCommon.ps1"

if (-not $RedistDir)
{
   $RedistDir = $DefaultRedistDir
}
if (-not (Test-Path -LiteralPath $RedistDir -PathType Container))
{
   throw "The redistributable folder $RedistDir does not exist. Pass -RedistDir."
}

# The release exes and the DLLs beside them, from the build folder of each game.
$targets = [Collections.Generic.List[string]]::new()
foreach ($file in $Files)
{
   $targets.Add([IO.Path]::GetFullPath($file))
}
if ($BuildDir)
{
   foreach ($game in $ReleaseGames.Keys)
   {
      $dir = Join-Path $BuildDir $ReleaseGames[$game].BuildSubdir
      if (Test-Path -LiteralPath $dir -PathType Container)
      {
         Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Extension -in ".exe", ".dll" } | ForEach-Object { $targets.Add($_.FullName) }
      }
   }
}
if ($targets.Count -eq 0)
{
   throw "Nothing to check. Pass -BuildDir or -Files."
}

$dumpbin = Get-Dumpbin $VsPath $Toolset

# The exported names of each runtime DLL, keyed by the lowercase file name.
$exports = @{}
foreach ($dll in Get-ChildItem -LiteralPath $RedistDir -Filter *.dll)
{
   $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
   foreach ($line in & $dumpbin /nologo /exports $dll.FullName)
   {
      # ordinal hint RVA name, or ordinal hint name for a forwarded export
      if ($line -match "^\s+\d+\s+[0-9A-F]+\s+(?:[0-9A-F]{8}\s+)?(\S+)")
      {
         $names.Add($Matches[1]) | Out-Null
      }
   }
   if ($LASTEXITCODE -ne 0 -or $names.Count -eq 0)
   {
      throw "dumpbin /exports $($dll.Name) listed no exports."
   }
   $exports[$dll.Name.ToLowerInvariant()] = $names
}

$failures = [Collections.Generic.List[string]]::new()
foreach ($target in $targets)
{
   $lines = & $dumpbin /nologo /imports $target
   if ($LASTEXITCODE -ne 0)
   {
      throw "dumpbin /imports $target failed."
   }

   $dll = $null
   $checked = 0
   foreach ($line in $lines)
   {
      if ($line -match "^    (\S+\.dll)$")
      {
         $dll = $Matches[1].ToLowerInvariant()
         if (-not $exports.ContainsKey($dll))
         {
            $dll = $null
         }
         continue
      }
      # A section heading such as Summary ends the list of the current DLL.
      if ($line -match "^  \S")
      {
         $dll = $null
         continue
      }
      if ($null -eq $dll -or $line -match "Characteristics$")
      {
         continue
      }
      if ($line -match "^\s+Ordinal\s+(\d+)$")
      {
         $failures.Add("$(Split-Path -Leaf $target) imports ordinal $($Matches[1]) of $dll, which cannot be checked by name.")
         continue
      }
      # hint name for a plain import, address hint name for a delay loaded one
      if ($line -match "^\s+(?:[0-9A-F]{8}\s+)?[0-9A-F]+\s+(\S+)$")
      {
         $checked += 1
         if (-not $exports[$dll].Contains($Matches[1]))
         {
            $failures.Add("$(Split-Path -Leaf $target) imports $($Matches[1]) from $dll, which the runtime in $RedistDir does not export.")
         }
      }
   }
   Write-Host "Checked $checked runtime imports of $(Split-Path -Leaf $target)."
}

if ($failures.Count -gt 0)
{
   $failures | ForEach-Object { Write-Host $_ }
   throw "$($failures.Count) import check failure(s)."
}
Write-Host "All runtime imports are exported by $RedistDir."
