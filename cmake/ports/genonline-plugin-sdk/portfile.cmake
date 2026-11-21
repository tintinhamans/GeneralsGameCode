# The SDK repository, fetched over HTTPS (the builder's git credentials when it is private).
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/community-outpost/genonline-plugin-sdk.git
    REF 3facd6999df39572297e9088a6f106e9c59790d2
)

set(VCPKG_BUILD_TYPE release)
file(INSTALL "${SOURCE_PATH}/include/GenOnlinePluginApi.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/genonline-plugin-sdk-config.cmake" "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
