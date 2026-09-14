# CMake's own FindOpenSSL module references openssl/applink.c as a required
# interface source for static OpenSSL on Windows/MSVC, but vcpkg's openssl
# port does not install that file into the package -- only into its own
# scratch build tree. Copy it into place ourselves before find_package(OpenSSL)
# looks for it.
#
# OpenSSL itself is only still needed here for GameNetworkingSockets' bundled
# WebRTC/ICE code (src/external/steamwebrtc), which links OpenSSL::Crypto and
# OpenSSL::SSL directly and unconditionally in its own CMakeLists.txt -- that
# is real WebRTC DTLS-SRTP transport code with OpenSSL's API baked in, not a
# pluggable backend, so it can't be swapped for BCrypt/SChannel like curl and
# GNS's own core crypto (USE_CRYPTO below) could be.
set(_openssl_applink_dst "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/openssl/applink.c")
if(NOT EXISTS "${_openssl_applink_dst}")
    file(GLOB_RECURSE _openssl_applink_src "${CMAKE_BINARY_DIR}/vcpkg_installed/vcpkg/blds/openssl/*/ms/applink.c")
    list(LENGTH _openssl_applink_src _openssl_applink_src_count)
    if(_openssl_applink_src_count GREATER 0)
        list(GET _openssl_applink_src 0 _openssl_applink_src)
        file(COPY "${_openssl_applink_src}" DESTINATION "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/openssl")
    endif()
    unset(_openssl_applink_src_count)
endif()
unset(_openssl_applink_dst)

find_package(OpenSSL REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "Build Sentry as a static library" FORCE)
set(SENTRY_BUILD_TESTS OFF CACHE BOOL "Do not build Sentry tests" FORCE)
set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "Do not build Sentry examples" FORCE)
set(SENTRY_ENABLE_INSTALL OFF CACHE BOOL "Do not install Sentry" FORCE)
set(SENTRY_TRANSPORT winhttp CACHE STRING "Use WinHTTP for Sentry transport" FORCE)
set(SENTRY_BACKEND inproc CACHE STRING "Use in-process Sentry crash capture" FORCE)

FetchContent_Declare(
    sentry_native
    GIT_REPOSITORY https://github.com/getsentry/sentry-native.git
    GIT_TAG        0.9.0
)
FetchContent_MakeAvailable(sentry_native)

set(BUILD_TESTING OFF CACHE BOOL "Do not build third-party tests" FORCE)
set(BUILD_STATIC_LIB ON CACHE BOOL "Build the static GameNetworkingSockets library" FORCE)
set(BUILD_SHARED_LIB OFF CACHE BOOL "Do not build the shared GameNetworkingSockets library" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Do not build GameNetworkingSockets examples" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Do not build GameNetworkingSockets tests" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "Do not build GameNetworkingSockets tools" FORCE)
set(ENABLE_ICE ON CACHE BOOL "Enable GameNetworkingSockets NAT traversal" FORCE)
set(USE_STEAMWEBRTC ON CACHE BOOL "Enable GameNetworkingSockets WebRTC ICE support" FORCE)
set(USE_CRYPTO BCrypt CACHE STRING "Use Windows native BCrypt for GameNetworkingSockets crypto" FORCE)
set(Protobuf_USE_STATIC_LIBS ON CACHE BOOL "Use static protobuf (matches the static vcpkg triplet)" FORCE)

FetchContent_Declare(
    gamenetworkingsockets
    GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
    GIT_TAG        v1.6.0
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_Populate(gamenetworkingsockets)

# The WebRTC wrapper bundled with GNS predates vcpkg's protobuf package and
# otherwise adds its vendored Abseil targets a second time.
set(_gns_steamwebrtc_cmake
    "${gamenetworkingsockets_SOURCE_DIR}/src/external/steamwebrtc/CMakeLists.txt")
file(READ "${_gns_steamwebrtc_cmake}" _gns_steamwebrtc_contents)
string(REPLACE
    "set(absl_FOUND OFF)"
    "find_package(absl CONFIG REQUIRED)"
    _gns_steamwebrtc_contents
    "${_gns_steamwebrtc_contents}")
string(REGEX REPLACE "install\\([^)]*\\)" "" _gns_steamwebrtc_contents "${_gns_steamwebrtc_contents}")
file(WRITE "${_gns_steamwebrtc_cmake}" "${_gns_steamwebrtc_contents}")

# GameNetworkingSockets has no option to skip its own install() rules, and
# they try to write into CMAKE_INSTALL_PREFIX, which normally requires admin
# privileges. Strip them the same way as steamwebrtc's above.
set(_gns_src_cmake "${gamenetworkingsockets_SOURCE_DIR}/src/CMakeLists.txt")
file(READ "${_gns_src_cmake}" _gns_src_contents)
string(REGEX REPLACE "install\\([^)]*\\)" "" _gns_src_contents "${_gns_src_contents}")
file(WRITE "${_gns_src_cmake}" "${_gns_src_contents}")

add_subdirectory(
    "${gamenetworkingsockets_SOURCE_DIR}"
    "${gamenetworkingsockets_BINARY_DIR}"
)
