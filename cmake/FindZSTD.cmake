find_path(ZSTD_INCLUDE_DIRS
        NAMES zstd.h
)
find_library(ZSTD_LIBRARIES
        NAMES zstd
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(ZSTD REQUIRED_VARS
        ZSTD_LIBRARIES ZSTD_INCLUDE_DIRS)

mark_as_advanced(ZSTD_INCLUDE_DIRS ZSTD_LIBRARIES)