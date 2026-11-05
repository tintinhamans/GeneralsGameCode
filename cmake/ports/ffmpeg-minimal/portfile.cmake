# Same configuration as scripts/ffmpeg/build-minimal.sh --static, release libraries only
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
if(NOT VCPKG_LIBRARY_LINKAGE STREQUAL "static" OR NOT VCPKG_CRT_LINKAGE STREQUAL "dynamic")
    message(FATAL_ERROR "${PORT} only builds static libraries against the DLL C runtime")
endif()

vcpkg_download_distfile(ARCHIVE
    URLS "https://ffmpeg.org/releases/ffmpeg-${VERSION}.tar.xz"
    FILENAME "ffmpeg-${VERSION}.tar.xz"
    SHA512 7756f8434371b9bc1d01cf578bf49014e058b0869c3c1df05030f5f4035f1def64b2e7dba3cf340da4dc8dd9239dd5f894530c9e45793ac27876807079aa546a
)
# The digest pinned in scripts/ffmpeg, verified against the official release signature
file(SHA256 "${ARCHIVE}" archive_sha256)
if(NOT archive_sha256 STREQUAL "8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e")
    message(FATAL_ERROR "FFmpeg archive SHA256 mismatch: ${archive_sha256}")
endif()
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
if(SOURCE_PATH MATCHES " ")
    message(FATAL_ERROR "FFmpeg does not build in a path with spaces: ${SOURCE_PATH}")
endif()

vcpkg_find_acquire_program(NASM)
get_filename_component(nasm_dir "${NASM}" DIRECTORY)
vcpkg_acquire_msys(MSYS_ROOT)
find_program(cl_exe cl.exe REQUIRED)
get_filename_component(cl_dir "${cl_exe}" DIRECTORY)
# MSVC first so the coreutils link.exe does not shadow the linker
vcpkg_add_to_path(PREPEND "${MSYS_ROOT}/usr/bin")
vcpkg_add_to_path(PREPEND "${nasm_dir}")
vcpkg_add_to_path(PREPEND "${cl_dir}")

set(build_dir "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")
file(REMOVE_RECURSE "${build_dir}")
file(MAKE_DIRECTORY "${build_dir}")
set(configure
    sh "'${SOURCE_PATH}/configure'"
    --toolchain=msvc --arch=x86 --target-os=win32 "'--prefix=${CURRENT_PACKAGES_DIR}'"
    --disable-debug "--extra-ldflags='-OPT:REF -OPT:ICF'"
    --disable-everything --disable-autodetect --disable-programs --disable-doc
    --disable-network --disable-avdevice --disable-avfilter --disable-protocols
    --enable-static --disable-shared --extra-cflags=-MD
    --enable-avformat --enable-avcodec --enable-avutil --enable-swscale --enable-swresample
    --enable-demuxer=bink,wav,mp3
    --enable-decoder=bink,binkaudio_dct,pcm_s16le,adpcm_ima_wav,mp3float
    --enable-parser=mpegaudio
)
list(JOIN configure " " configure)
set(shell "${MSYS_ROOT}/usr/bin/bash.exe")
vcpkg_execute_required_process(
    COMMAND "${shell}" -c "${configure}"
    WORKING_DIRECTORY "${build_dir}"
    LOGNAME "configure-${TARGET_TRIPLET}-rel"
    SAVE_LOG_FILES ffbuild/config.log
)
vcpkg_execute_required_process(
    COMMAND "${shell}" -c "make -j${VCPKG_CONCURRENCY}"
    WORKING_DIRECTORY "${build_dir}"
    LOGNAME "build-${TARGET_TRIPLET}-rel"
)
vcpkg_execute_required_process(
    COMMAND "${shell}" -c "make install"
    WORKING_DIRECTORY "${build_dir}"
    LOGNAME "install-${TARGET_TRIPLET}-rel"
)

# Headers and avformat.lib, avcodec.lib, avutil.lib, swscale.lib, swresample.lib only
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib/pkgconfig" "${CURRENT_PACKAGES_DIR}/share/ffmpeg")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md" "${SOURCE_PATH}/COPYING.LGPLv2.1")
