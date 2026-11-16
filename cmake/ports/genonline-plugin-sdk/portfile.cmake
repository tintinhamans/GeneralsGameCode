# The SDK repository is private, so git fetches it over SSH with the builder's own key.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL git@github.com:community-outpost/genonline-plugin-sdk.git
    REF d20fdd383ff62e74f753d0ae9b952a785f4e8d3d
)

set(VCPKG_BUILD_TYPE release)
file(INSTALL "${SOURCE_PATH}/include/GenOnlinePluginApi.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/genonline-plugin-sdk-config.cmake" "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
