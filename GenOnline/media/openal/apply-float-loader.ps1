param([Parameter(Mandatory = $true)][string]$SourceDirectory)
$ErrorActionPreference = 'Stop'
$version = [IO.File]::ReadAllText((Join-Path $SourceDirectory 'CMakeLists.txt'))
if ($version -notmatch '(?m)^project\(OpenAL\s+VERSION 1\.25\.2\s+LANGUAGES') {
    throw 'The float loader patch requires OpenAL Soft 1.25.2.'
}
$path = Join-Path $SourceDirectory 'core/voice.cpp'
$before = '7157D8120F979015927C2FE3A7C481C1CC802F4483FE14E2E86264FF0F1A6DDE'
$after = 'A2E0B882DDE4F9A937F1F1BC46473F4F122956F4F98375675E9689309E26EC40'
$current = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
if ($current -ne $before -and $current -ne $after) {
    throw 'Unexpected OpenAL Soft voice.cpp; nothing was patched.'
}
$fragmentPath = Join-Path $PSScriptRoot 'openal-soft-1.25.2-float-loader.inc'
$fragment = [IO.File]::ReadAllText($fragmentPath).Replace("`r`n", "`n")
if ($current -eq $before) {
    $text = [IO.File]::ReadAllText($path)
    $marker = "template<>`nvoid LoadSamples<IMA4Data>"
    $index = $text.IndexOf($marker, [StringComparison]::Ordinal)
    if ($index -lt 0 -or $text.LastIndexOf($marker, [StringComparison]::Ordinal) -ne $index) {
        throw 'The float loader insertion point is not unique.'
    }
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($text.Insert($index, $fragment))
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $actual = [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-', '') }
    finally { $sha.Dispose() }
    if ($actual -ne $after) { throw 'Float loader patch digest mismatch; nothing was written.' }
    [IO.File]::WriteAllBytes($path, $bytes)
}
[PSCustomObject]@{ name = 'float-loader-1.25.2'; before = $before; after = $after;
    fragmentSha256 = (Get-FileHash -LiteralPath $fragmentPath -Algorithm SHA256).Hash }
