# Uploads the release build's exe and PDB to GlitchTip, so crash reports from that build show function names and lines.
# Run it after the release build and before MakePatch.ps1, for every build that players get.
# GlitchTip's API is not public. Open it first with: kubectl -n glitchtip port-forward svc/glitchtip 8000:8000
# The auth token comes from SENTRY_AUTH_TOKEN and needs the project:write scope. The 1Password item
# "glitchtip release symbols token" in OutpostInfra holds one.
# This speaks GlitchTip's upload API itself because glitchtip-cli 1.0.0 refuses chunk uploads through the port-forward.
param(
   [string]$Url = "http://localhost:8000",
   [string]$Org = "generals-online",
   [string]$Project = "genonline-client",
   [string]$BuildDir = "build\win32-vcpkg\GeneralsMD\Release"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Net.Http

if (-not $env:SENTRY_AUTH_TOKEN)
{
   throw "SENTRY_AUTH_TOKEN is not set. Read it from the 1Password item `"glitchtip release symbols token`" first."
}

# The crash reports only name the game exe, so its exe and PDB are the files GlitchTip needs.
# GlitchTip matches the PDB to crashes by the debug id the exe carries, so both must come from the build that ships.
$files = @("$BuildDir\genonline_zerohour.exe", "$BuildDir\genonline_zerohour.pdb") | ForEach-Object { (Resolve-Path $_).Path }

$Url = $Url.TrimEnd("/")
$client = New-Object System.Net.Http.HttpClient
$client.Timeout = [TimeSpan]::FromMinutes(10)
$client.DefaultRequestHeaders.Authorization = New-Object System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", $env:SENTRY_AUTH_TOKEN)

function Read-Response($response, $what)
{
   $body = $response.Content.ReadAsStringAsync().Result
   if (-not $response.IsSuccessStatusCode)
   {
      throw "$what failed with HTTP $([int]$response.StatusCode): $body"
   }
   return $body
}

function Get-Sha1Hex([byte[]]$bytes)
{
   $sha1 = [System.Security.Cryptography.SHA1]::Create()
   try { return -join ($sha1.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") }) }
   finally { $sha1.Dispose() }
}

$info = Read-Response ($client.GetAsync("$Url/api/0/organizations/$Org/chunk-upload/").Result) "Reading the chunk upload settings" | ConvertFrom-Json
# The server may name its own public host in the chunk URL. Only its path is used, so the chunks go through $Url too.
$chunkUrl = $Url + ([Uri]::new([Uri]"http://placeholder", $info.url)).AbsolutePath
$chunkSize = [int]$info.chunkSize

$assemble = @{}
foreach ($file in $files)
{
   $bytes = [System.IO.File]::ReadAllBytes($file)
   $chunks = @()
   for ($offset = 0; $offset -lt $bytes.Length; $offset += $chunkSize)
   {
      $length = [Math]::Min($chunkSize, $bytes.Length - $offset)
      $chunk = New-Object byte[] $length
      [Array]::Copy($bytes, $offset, $chunk, 0, $length)
      $checksum = Get-Sha1Hex $chunk
      $chunks += $checksum

      # Each chunk goes up gzipped and named by the SHA-1 of its plain bytes, one chunk per request.
      $gzipped = New-Object System.IO.MemoryStream
      $gzip = New-Object System.IO.Compression.GZipStream($gzipped, [System.IO.Compression.CompressionMode]::Compress)
      $gzip.Write($chunk, 0, $chunk.Length)
      $gzip.Dispose()
      $form = New-Object System.Net.Http.MultipartFormDataContent
      $part = New-Object System.Net.Http.ByteArrayContent(, $gzipped.ToArray())
      $form.Add($part, "file_gzip", $checksum)
      Read-Response ($client.PostAsync($chunkUrl, $form).Result) "Uploading a chunk of $file" | Out-Null
      $form.Dispose()
   }
   $assemble[(Get-Sha1Hex $bytes)] = @{ name = [System.IO.Path]::GetFileName($file); chunks = $chunks }
   Write-Host "Uploaded $file in $($chunks.Count) chunk(s)."
}

# GlitchTip builds each file from its chunks in the background, so the request repeats until every file reads ok.
$assembleUrl = "$Url/api/0/projects/$Org/$Project/files/difs/assemble/"
$assembleBody = $assemble | ConvertTo-Json -Depth 4
for ($attempt = 1; $attempt -le 60; $attempt++)
{
   $content = New-Object System.Net.Http.StringContent($assembleBody, [System.Text.Encoding]::UTF8, "application/json")
   $states = Read-Response ($client.PostAsync($assembleUrl, $content).Result) "Assembling the debug files" | ConvertFrom-Json
   $pending = $false
   foreach ($property in $states.PSObject.Properties)
   {
      $state = $property.Value.state
      if ($state -eq "error" -or $state -eq "not_found")
      {
         throw "GlitchTip could not assemble $($assemble[$property.Name].name): $state $($property.Value.detail)"
      }
      if ($state -ne "ok")
      {
         $pending = $true
      }
   }
   if (-not $pending)
   {
      break
   }
   Start-Sleep -Seconds 2
}
if ($pending)
{
   throw "GlitchTip did not finish assembling the debug files within two minutes."
}

# The PDB must now be listed with the debug id that the exe's crash reports carry.
$pdbChecksum = ($assemble.GetEnumerator() | Where-Object { $_.Value.name -like "*.pdb" }).Key
$stored = Read-Response ($client.GetAsync("$Url/api/0/projects/$Org/$Project/files/dsyms/").Result) "Listing the debug files" | ConvertFrom-Json
$pdb = $stored | Where-Object { $_.sha1 -eq $pdbChecksum } | Select-Object -First 1
if (-not $pdb)
{
   throw "GlitchTip reports the upload as done but does not list the PDB."
}
$client.Dispose()
Write-Host "GlitchTip has the PDB as debug id $($pdb.debugId) in $Org/$Project."
