#Requires -Version 7.0
# Uploads a v2 publish folder to the Cloudflare R2 bucket behind cdn.generals.online with the AWS CLI, in the order of
# PATCH_FORMAT.md 4.1: the objects, then the release manifests, then the channel pointers last, so no pointer ever names
# a release or an object that is not there yet.
#
# Objects and release manifests are immutable: a key that exists is never written again. An existing object is skipped,
# and an existing release manifest must hold the same bytes. A pointer replaces the published one only with a higher
# sequence. -DryRun lists what would be uploaded and contacts nothing.
#
# The Cloudflare account id comes from -AccountId or R2_ACCOUNT_ID and is never printed.
param(
   [Parameter(Mandatory = $true)][string]$PublishDir,
   [ValidateSet("test", "dev")][string]$Channel = "test",
   [string[]]$Games = @("zerohour", "generals"),
   [string]$AccountId = "",
   [string]$Bucket = "online-generals-cdn",
   [string]$AwsProfile = "generals-online-r2",
   [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ReleaseCommon.ps1"

Assert-GameNames $Games
$PublishDir = [IO.Path]::GetFullPath($PublishDir)
$v2 = Join-Path $PublishDir "v2"
if (-not (Test-Path -LiteralPath $v2 -PathType Container))
{
   throw "$v2 is missing. Run PackClient.ps1 first."
}
if (-not $AccountId)
{
   $AccountId = $env:R2_ACCOUNT_ID
}
if (-not $DryRun -and $AccountId -notmatch "^[0-9a-fA-F]{32}$")
{
   throw "Pass -AccountId or set R2_ACCOUNT_ID (the 32 hex digit Cloudflare account id of the bucket)."
}
$awsCommon = @("--endpoint-url", "https://$AccountId.r2.cloudflarestorage.com", "--profile", $AwsProfile, "--region", "auto")

# Hides the account id in any text the AWS CLI returns.
function Hide-Account([string]$Text)
{
   if ($AccountId)
   {
      return $Text.Replace($AccountId, "<account>")
   }
   return $Text
}

function Get-Key([string]$Path)
{
   return $Path.Substring($PublishDir.Length + 1).Replace("\", "/")
}

# True when the key exists in the bucket, false on 404, and an error on anything else.
function Test-RemoteKey([string]$Key)
{
   $head = aws s3api head-object --bucket $Bucket --key $Key @awsCommon 2>&1
   if ($LASTEXITCODE -eq 0)
   {
      return $true
   }
   if ($head -match "\(404\)|Not Found")
   {
      return $false
   }
   throw (Hide-Account "Cannot check $Key in $Bucket (aws profile '$AwsProfile'):`n$($head -join "`n")")
}

function Get-RemoteFile([string]$Key)
{
   $file = Join-Path ([IO.Path]::GetTempPath()) ("genonline-upload-" + [guid]::NewGuid().ToString("N"))
   $output = aws s3 cp "s3://$Bucket/$Key" $file @awsCommon --only-show-errors 2>&1
   if ($LASTEXITCODE -ne 0)
   {
      throw (Hide-Account "Cannot download ${Key}:`n$($output -join "`n")")
   }
   return $file
}

function Send-File([string]$Path, [string]$Key, [string]$Cache, [string]$Type)
{
   $output = aws s3 cp $Path "s3://$Bucket/$Key" @awsCommon --cache-control $Cache --content-type $Type --only-show-errors 2>&1
   if ($LASTEXITCODE -ne 0)
   {
      throw (Hide-Account "Upload of $Key failed:`n$($output -join "`n")")
   }
   Write-Host "Uploaded $Key"
}

$immutable = "public, max-age=31536000, immutable"
$noCache = "no-cache, no-store, must-revalidate, no-transform"

function Get-ContentType([string]$Path)
{
   switch ([IO.Path]::GetExtension($Path))
   {
      ".zst" { return "application/zstd" }
      ".json" { return "application/json" }
      default { return "text/plain" }
   }
}

# 1. Objects, then 2. release manifests with their signatures.
foreach ($folder in "objects", "releases")
{
   $root = Join-Path $v2 $folder
   if (-not (Test-Path -LiteralPath $root -PathType Container))
   {
      throw "$root is missing."
   }
   $uploaded = 0
   $kept = 0
   foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName)
   {
      $key = Get-Key $file.FullName
      if ($DryRun)
      {
         Write-Host "Would upload $key ($($file.Length) bytes, immutable) unless it exists"
         continue
      }
      if (Test-RemoteKey $key)
      {
         if ($folder -eq "releases")
         {
            $remote = Get-RemoteFile $key
            try
            {
               if ((Get-FileHash -LiteralPath $remote).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash)
               {
                  throw "$key exists in $Bucket with other bytes. A release id is never published twice."
               }
            }
            finally
            {
               Remove-Item -LiteralPath $remote -ErrorAction SilentlyContinue
            }
         }
         $kept += 1
         continue
      }
      Send-File $file.FullName $key $immutable (Get-ContentType $file.Name)
      $uploaded += 1
   }
   if (-not $DryRun)
   {
      Write-Host "$folder`: $uploaded uploaded, $kept present already"
   }
}

# 3. The pointers of the channel, last. The signature follows its pointer.
foreach ($game in $Games)
{
   $pointer = Join-Path $v2 "channels\$Channel\$game.json"
   if (-not (Test-Path -LiteralPath $pointer -PathType Leaf) -or -not (Test-Path -LiteralPath "$pointer.minisig" -PathType Leaf))
   {
      throw "$pointer or its .minisig is missing. Run PackClient.ps1 for $game."
   }
   $key = Get-Key $pointer
   $sequence = (Get-Content -Raw -LiteralPath $pointer | ConvertFrom-Json).sequence
   if ($DryRun)
   {
      Write-Host "Would upload $key and $key.minisig (sequence $sequence, no-cache) when the published sequence is lower"
      continue
   }
   if (Test-RemoteKey $key)
   {
      $remote = Get-RemoteFile $key
      try
      {
         $same = (Get-FileHash -LiteralPath $remote).Hash -eq (Get-FileHash -LiteralPath $pointer).Hash
         $remoteSequence = (Get-Content -Raw -LiteralPath $remote | ConvertFrom-Json).sequence
      }
      finally
      {
         Remove-Item -LiteralPath $remote -ErrorAction SilentlyContinue
      }
      if ($same)
      {
         Write-Host "$key is published already"
         continue
      }
      if ($sequence -le $remoteSequence)
      {
         throw "$key has sequence $sequence, not above the published $remoteSequence. Promote again from a publish folder that holds the published pointer."
      }
   }
   Send-File $pointer $key $noCache "application/json"
   Send-File "$pointer.minisig" "$key.minisig" $noCache "text/plain"
}
