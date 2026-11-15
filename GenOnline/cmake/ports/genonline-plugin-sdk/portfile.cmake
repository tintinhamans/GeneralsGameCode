# The SDK repository is private, so git fetches it over SSH with the builder's own key.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL git@github.com:community-outpost/genonline-plugin-sdk.git
    REF 56518cd8751c0b5269490f68ff3602afea0f1838
)

set(VCPKG_BUILD_TYPE release)
file(INSTALL "${SOURCE_PATH}/include/GenOnlinePluginApi.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/genonline-plugin-sdk-config.cmake" "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
