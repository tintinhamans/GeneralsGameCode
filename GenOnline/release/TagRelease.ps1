# Tags the checked out commit as a client release and pushes the tag. Run it before the release build:
# the build reads the tag for its version and names it in audio-libraries-NOTICE.txt, which the release packaging requires.
param(
   [Parameter(Mandatory = $true)][string]$Version,
   [string]$Remote = "",
   [switch]$NoPush
)

$ErrorActionPreference = "Stop"

# A release candidate for the test channel carries -rc.N, the release that goes live has none.
if ($Version -notmatch "^\d{4}\.([1-9]|1[0-2])\.\d+(-rc\.[1-9]\d*)?$")
{
   throw "Version $Version is not YYYY.M.PATCH or YYYY.M.PATCH-rc.N, for example 2026.11.1 or 2026.11.1-rc.1."
}
$tag = "v$Version"

git diff --quiet HEAD --
if ($LASTEXITCODE -ne 0)
{
   throw "The checkout has uncommitted changes. A release tag must name a commit that matches the build."
}

git rev-parse --verify --quiet "refs/tags/$tag" | Out-Null
if ($LASTEXITCODE -eq 0)
{
   throw "Tag $tag exists already."
}

# The remote defaults to the one that holds the public source the notices name.
if ($Remote -eq "")
{
   $Remote = git remote -v | Where-Object { $_ -match "github\.com[:/]community-outpost/genonline-client(\.git)? \(push\)$" } | ForEach-Object { ($_ -split "\s+")[0] } | Select-Object -First 1
   if (-not $Remote)
   {
      throw "No remote points at community-outpost/genonline-client. Name one with -Remote."
   }
}

if (-not $NoPush)
{
   git ls-remote --exit-code --tags $Remote "refs/tags/$tag" | Out-Null
   if ($LASTEXITCODE -eq 0)
   {
      throw "Tag $tag exists already on $Remote."
   }
}

$commit = git rev-parse HEAD
git tag -a $tag -m "GenOnline client $Version" $commit
if ($LASTEXITCODE -ne 0)
{
   throw "Creating tag $tag failed."
}
Write-Host "Tagged $commit as $tag."

if (-not $NoPush)
{
   git push $Remote "refs/tags/$tag"
   if ($LASTEXITCODE -ne 0)
   {
      throw "Pushing tag $tag to $Remote failed. The local tag stays."
   }
   Write-Host "Pushed $tag to $Remote."
}
