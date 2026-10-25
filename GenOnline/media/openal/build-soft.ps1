param(
    [Parameter(Mandatory = $true)][string]$WorkDirectory,
    [ValidateRange(1, 32)][int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x86') {
    throw 'Run from an x86 Visual Studio 2022 developer environment.'
}
foreach ($tool in 'cmake', 'ninja', 'cl', 'tar') {
    Get-Command $tool -ErrorAction Stop | Out-Null
}
New-Item -ItemType Directory -Force $WorkDirectory | Out-Null
$root = (Resolve-Path -LiteralPath $WorkDirectory).Path
$source = Join-Path $root 'openal-soft-1.25.2'
$build = Join-Path $root 'compile'
$install = Join-Path $root 'install'
foreach ($path in $source, $build, $install) {
    if (Test-Path -LiteralPath $path) {
        throw "Already exists: $path. Choose a fresh work directory; nothing was removed."
    }
}
$archive = Join-Path $root 'openal-soft-1.25.2.tar.gz'
$url = 'https://github.com/kcat/openal-soft/archive/refs/tags/1.25.2.tar.gz'
$expected = 'FB27E5839AA11F0E5B9D33756965291FAD5D6909AB928EA1F796F4A1A6877894'
if (!(Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri $url -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
    throw 'OpenAL Soft archive hash mismatch.'
}
& tar -xf $archive -C $root
if ($LASTEXITCODE -ne 0) { throw 'Source extraction failed.' }
$patch = & (Join-Path $PSScriptRoot 'apply-float-loader.ps1') -SourceDirectory $source
$arguments = @('-S', $source, '-B', $build, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_INSTALL_PREFIX=$install", '-DLIBTYPE=SHARED',
    '-DALSOFT_UTILS=OFF', '-DALSOFT_EXAMPLES=OFF', '-DALSOFT_TESTS=OFF', '-DALSOFT_EAX=OFF',
    '-DALSOFT_BACKEND_SDL2=OFF', '-DALSOFT_BACKEND_SDL3=OFF', '-DALSOFT_BACKEND_DSOUND=OFF',
    '-DALSOFT_BACKEND_WINMM=OFF', '-DALSOFT_BACKEND_WASAPI=ON', '-DALSOFT_BACKEND_WAVE=ON',
    '-DALSOFT_INSTALL_CONFIG=OFF', '-DALSOFT_INSTALL_HRTF_DATA=OFF',
    '-DALSOFT_INSTALL_AMBDEC_PRESETS=OFF', '-DALSOFT_ENABLE_MODULES=OFF')
$arguments | Set-Content (Join-Path $root 'configure-arguments.txt')
@{ source = $url; sha256 = $expected; compiler = (Get-Command cl).Source;
   compilerVersion = (Get-Item (Get-Command cl).Source).VersionInfo.FileVersion;
   patch = $patch } |
    ConvertTo-Json | Set-Content (Join-Path $root 'provenance.json')
& cmake @arguments *> (Join-Path $root 'configure.log')
if ($LASTEXITCODE -ne 0) { throw 'OpenAL Soft configuration failed; see configure.log.' }
& cmake --build $build -j $Jobs *> (Join-Path $root 'build.log')
if ($LASTEXITCODE -ne 0) { throw 'OpenAL Soft build failed; see build.log.' }
& cmake --install $build *> (Join-Path $root 'install.log')
if ($LASTEXITCODE -ne 0) { throw 'OpenAL Soft install failed; see install.log.' }
Get-ChildItem (Join-Path $install 'bin/*.dll') | Select-Object Name, Length
