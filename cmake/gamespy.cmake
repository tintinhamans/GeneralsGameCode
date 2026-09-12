set(GS_OPENSSL FALSE)
set(GAMESPY_SERVER_NAME "server.cnc-online.net")

FetchContent_Declare(
    gamespy
    GIT_REPOSITORY https://github.com/TheSuperHackers/GamespySDK.git
    GIT_TAG        07e3d15c500415abc281efb74322ab6d9c857eb8
)

FetchContent_Populate(gamespy)

# GameSpy exports these configuration buffers from the shared DLL.  The
# upstream headers declare them as ordinary extern data, which makes MSVC
# generate direct references instead of importing them through the DLL.
foreach(_gamespy_header IN ITEMS
    "${gamespy_SOURCE_DIR}/include/gamespy/gstats/gstats.h"
    "${gamespy_SOURCE_DIR}/include/gamespy/gstats/gpersist.h")
    file(READ "${_gamespy_header}" _gamespy_header_contents)
    string(REPLACE
        "extern char gcd_secret_key[256];"
        "#if defined(_WIN32) && defined(GAMESPY_IMPORT_DATA)\n\textern __declspec(dllimport) char gcd_secret_key[256];\n#else\n\textern char gcd_secret_key[256];\n#endif"
        _gamespy_header_contents
        "${_gamespy_header_contents}")
    string(REPLACE
        "extern char gcd_gamename[256];"
        "#if defined(_WIN32) && defined(GAMESPY_IMPORT_DATA)\n\textern __declspec(dllimport) char gcd_gamename[256];\n#else\n\textern char gcd_gamename[256];\n#endif"
        _gamespy_header_contents
        "${_gamespy_header_contents}")
    file(WRITE "${_gamespy_header}" "${_gamespy_header_contents}")
endforeach()

add_subdirectory("${gamespy_SOURCE_DIR}" "${gamespy_BINARY_DIR}")
