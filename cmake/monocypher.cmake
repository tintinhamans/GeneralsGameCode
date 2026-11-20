# Monocypher 4.0.2 verifies the minisign signatures of the GenOnline plugin manifests.
set(MONOCYPHER_DIR ${CMAKE_BINARY_DIR}/_deps/monocypher-src)

FetchContent_Populate(monocypher DOWNLOAD_EXTRACT_TIMESTAMP
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        0d85f98c9d9b0227e42cf795cb527dff372b40a4
    SOURCE_DIR     ${MONOCYPHER_DIR}
)

add_library(monocypher STATIC)

target_sources(monocypher PRIVATE
    "${MONOCYPHER_DIR}/src/monocypher.c"
    "${MONOCYPHER_DIR}/src/monocypher.h"
    "${MONOCYPHER_DIR}/src/optional/monocypher-ed25519.c"
    "${MONOCYPHER_DIR}/src/optional/monocypher-ed25519.h"
)

target_include_directories(monocypher PUBLIC ${MONOCYPHER_DIR}/src ${MONOCYPHER_DIR}/src/optional)
