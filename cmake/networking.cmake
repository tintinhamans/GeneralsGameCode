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

set(CURL_USE_OPENSSL ON CACHE BOOL "Build libcurl with OpenSSL" FORCE)
set(CURL_DISABLE_WEBSOCKETS OFF CACHE BOOL "Enable libcurl WebSocket support" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "Do not build the curl command-line tool" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Do not build third-party tests" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build libcurl as a shared library" FORCE)
set(BUILD_STATIC_LIBS OFF CACHE BOOL "Do not build a static libcurl" FORCE)

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG        curl-8_11_0
)
FetchContent_MakeAvailable(curl)

set(BUILD_STATIC_LIB OFF CACHE BOOL "Do not build the static GameNetworkingSockets library" FORCE)
set(BUILD_SHARED_LIB ON CACHE BOOL "Build the shared GameNetworkingSockets library" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Do not build GameNetworkingSockets examples" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Do not build GameNetworkingSockets tests" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "Do not build GameNetworkingSockets tools" FORCE)
set(ENABLE_ICE ON CACHE BOOL "Enable GameNetworkingSockets NAT traversal" FORCE)
set(USE_STEAMWEBRTC ON CACHE BOOL "Enable GameNetworkingSockets WebRTC ICE support" FORCE)
set(USE_CRYPTO OpenSSL CACHE STRING "Use OpenSSL for GameNetworkingSockets crypto" FORCE)
set(Protobuf_USE_STATIC_LIBS OFF CACHE BOOL "Use shared protobuf when available" FORCE)

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
file(WRITE "${_gns_steamwebrtc_cmake}" "${_gns_steamwebrtc_contents}")

add_subdirectory(
    "${gamenetworkingsockets_SOURCE_DIR}"
    "${gamenetworkingsockets_BINARY_DIR}"
)
