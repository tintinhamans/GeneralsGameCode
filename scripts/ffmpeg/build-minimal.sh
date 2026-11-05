#!/usr/bin/env bash
set -euo pipefail
export PATH="/usr/bin:$PATH"

linkage=shared
if [[ ${1:-} == --static ]]; then
    linkage=static
    shift
fi
if [[ $# -gt 2 ]]; then
    echo "Usage: bash build-minimal.sh [--static] [WORK_DIRECTORY [JOBS]]" >&2
    exit 2
fi
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ $linkage == static ]]; then
    default_work="$repo/build/ffmpeg-static"
    linkage_flags=(--enable-static --disable-shared --extra-cflags=-MD)
else
    default_work="$repo/build/ffmpeg-minimal"
    linkage_flags=(--disable-static --enable-shared)
fi
if [[ ${VSCMD_ARG_TGT_ARCH:-} != x86 ]]; then
    echo "Run from an x86 VS 2022 developer environment with MSYS2 tools and NASM on PATH." >&2
    exit 2
fi
for tool in cl.exe nasm make curl tar sha256sum; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 2; }
done
jobs=${2:-4}
if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
    echo "JOBS must be a positive integer." >&2
    exit 2
fi
mkdir -p "${1:-$default_work}"
work=$(cd "${1:-$default_work}" && pwd)
cd "$work"
version=9.0.2
archive="ffmpeg-$version.tar.xz"
sha256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
url="https://ffmpeg.org/releases/$archive"
if [[ -d "ffmpeg-$version" || -d build || -d install ]]; then
    echo "Source/build/install already exists; choose a fresh work directory. Nothing was removed." >&2
    exit 2
fi
if [[ ! -f $archive ]]; then
    curl --fail --location --retry 2 --proto '=https' "$url" -o "$archive"
fi
printf '%s  %s\n' "$sha256" "$archive" | sha256sum --check
# The pinned digest was verified against the official release signature.
tar -xf "$archive"
[[ $(cat "ffmpeg-$version/VERSION") == "$version" ]]
printf 'source=%s\nsha256=%s\n' "$url" "$sha256" > provenance.txt
cl.exe >> provenance.txt 2>&1 || true
nasm -v >> provenance.txt
make --version >> provenance.txt
mkdir build
cd build
../ffmpeg-9.0.2/configure \
    --toolchain=msvc --arch=x86 --target-os=win32 --prefix="$work/install" \
    --disable-debug --extra-ldflags='-OPT:REF -OPT:ICF' \
    --disable-everything --disable-autodetect --disable-programs --disable-doc \
    --disable-network --disable-avdevice --disable-avfilter --disable-protocols \
    "${linkage_flags[@]}" \
    --enable-avformat --enable-avcodec --enable-avutil --enable-swscale --enable-swresample \
    --enable-demuxer=bink,wav,mp3 \
    --enable-decoder=bink,binkaudio_dct,pcm_s16le,adpcm_ima_wav,mp3float \
    --enable-parser=mpegaudio 2>&1 | tee ../configure.log
make -j "$jobs" 2>&1 | tee ../build.log
make install 2>&1 | tee ../install.log
if [[ $linkage == static ]]; then
    find "$work/install/lib" -name '*.lib' -printf '%f %s bytes\n' | tee ../library-sizes.txt
else
    find "$work/install/bin" -name '*.dll' -printf '%f %s bytes\n' | tee ../runtime-sizes.txt
fi
