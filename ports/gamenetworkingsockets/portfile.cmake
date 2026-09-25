# Overlay of the upstream port that builds the WebRTC ICE backend instead of the native ICE client.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ValveSoftware/GameNetworkingSockets
    REF "2cb93a06350bb065db53abdb0d87cf297e0bfd34" # v1.6.0
    SHA512 c2deaa3aab42cd840dd13560ca4da40faa375ab846ea15af38d55eb7acc48cfe8cbdbe0c76b9c3484d26f9e1163e36ac1eb73a317e5c19cefe60d0b861d19e06
    HEAD_REF master
)

# WebRTC is a git submodule of GameNetworkingSockets; fetch it at the commit v1.6.0 pins.
vcpkg_from_git(
    OUT_SOURCE_PATH WEBRTC_SOURCE_PATH
    URL https://webrtc.googlesource.com/src
    REF 30a3e787948dd6cdd541773101d664b85eb332a6
)
file(REMOVE_RECURSE "${SOURCE_PATH}/src/external/webrtc")
file(RENAME "${WEBRTC_SOURCE_PATH}" "${SOURCE_PATH}/src/external/webrtc")

# Use vcpkg's abseil instead of the abseil submodule; only then are the steamwebrtc targets exported.
vcpkg_replace_string("${SOURCE_PATH}/src/external/steamwebrtc/CMakeLists.txt"
    "set(absl_FOUND OFF)"
    "find_package(absl CONFIG REQUIRED)"
)

if("${VCPKG_LIBRARY_LINKAGE}" STREQUAL "dynamic")
    set(BUILD_SHARED_LIB ON)
    set(BUILD_STATIC_LIB OFF)
else()
    set(BUILD_SHARED_LIB OFF)
    set(BUILD_STATIC_LIB ON)
endif()

if("${VCPKG_CRT_LINKAGE}" STREQUAL "static")
    set(MSVC_CRT_STATIC ON)
else()
    set(MSVC_CRT_STATIC OFF)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DUSE_CRYPTO=OpenSSL
        -DBUILD_STATIC_LIB=${BUILD_STATIC_LIB}
        -DBUILD_SHARED_LIB=${BUILD_SHARED_LIB}
        -DMSVC_CRT_STATIC=${MSVC_CRT_STATIC}
        -DBUILD_TESTS=OFF
        -DBUILD_EXAMPLES=OFF
        -DBUILD_TOOLS=OFF
        -DENABLE_ICE=ON
        -DUSE_STEAMWEBRTC=ON
    MAYBE_UNUSED_VARIABLES
        MSVC_CRT_STATIC
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/GameNetworkingSockets")
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_copy_pdbs()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
