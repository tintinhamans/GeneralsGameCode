# Same source, patch and options as scripts/openal/build-soft.ps1
vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/kcat/openal-soft/archive/refs/tags/${VERSION}.tar.gz"
    FILENAME "openal-soft-${VERSION}.tar.gz"
    SHA512 d3790aedce0bdb0b348b35f917dec0626de2486f5967c9160cf2d50d5c2a00ff264ff73133ac6a0aa9207e8e5042e5d4fcc663c1e733df8e733647e843c39b25
)
file(SHA256 "${ARCHIVE}" archive_sha256)
if(NOT archive_sha256 STREQUAL "fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894")
    message(FATAL_ERROR "OpenAL Soft archive SHA256 mismatch: ${archive_sha256}")
endif()
vcpkg_extract_source_archive(SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    PATCHES 0001-float-loader.patch
)
# The voice.cpp digest scripts/openal/apply-float-loader.ps1 expects after patching
file(SHA256 "${SOURCE_PATH}/core/voice.cpp" voice_sha256)
if(NOT voice_sha256 STREQUAL "a2e0b882dde4f9a937f1f1bc46473f4f122956f4f98375675e9689309e26ec40")
    message(FATAL_ERROR "Patched OpenAL Soft voice.cpp SHA256 mismatch: ${voice_sha256}")
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    set(libtype SHARED)
else()
    set(libtype STATIC)
endif()
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DLIBTYPE=${libtype}
        -DFORCE_STATIC_VCRT=OFF
        -DALSOFT_UTILS=OFF
        -DALSOFT_EXAMPLES=OFF
        -DALSOFT_TESTS=OFF
        -DALSOFT_EAX=OFF
        -DALSOFT_BACKEND_SDL2=OFF
        -DALSOFT_BACKEND_SDL3=OFF
        -DALSOFT_BACKEND_DSOUND=OFF
        -DALSOFT_BACKEND_WINMM=OFF
        -DALSOFT_BACKEND_WASAPI=ON
        -DALSOFT_BACKEND_WAVE=ON
        -DALSOFT_INSTALL_CONFIG=OFF
        -DALSOFT_INSTALL_HRTF_DATA=OFF
        -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF
        -DALSOFT_ENABLE_MODULES=OFF
    MAYBE_UNUSED_VARIABLES
        FORCE_STATIC_VCRT
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/OpenAL")
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/lib/pkgconfig"
    "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig"
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

# The static export links Threads::Threads, so its config has to find Threads
if(libtype STREQUAL "STATIC")
    set(config "${CURRENT_PACKAGES_DIR}/share/${PORT}/OpenALConfig.cmake")
    file(READ "${config}" contents)
    file(WRITE "${config}" "include(CMakeFindDependencyMacro)\nfind_dependency(Threads)\n${contents}")
endif()

vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/COPYING"
    "${SOURCE_PATH}/BSD-3Clause"
    "${SOURCE_PATH}/LICENSE-pffft"
    "${SOURCE_PATH}/core/bs2b.h"
    "${SOURCE_PATH}/gsl/LICENSE"
    "${SOURCE_PATH}/fmt-11.2.0/LICENSE"
)
