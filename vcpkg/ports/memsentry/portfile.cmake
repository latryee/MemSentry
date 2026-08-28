vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO latryee/MemSentry
    REF v${VERSION}
    SHA512 0 # Automatically calculated on vcpkg publish
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DMEMSENTRY_BUILD_TESTS=OFF
        -DMEMSENTRY_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/memsentry)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
