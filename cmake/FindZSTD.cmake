find_path(LIBZSTD_INCLUDE_DIRS
        NAMES zstd.h
)
find_library(LIBZSTD_LIBRARIES
        NAMES zstd
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(ZSTD REQUIRED_VARS
        LIBZSTD_LIBRARIES LIBZSTD_INCLUDE_DIRS)

mark_as_advanced(LIBZSTD_INCLUDE_DIRS LIBZSTD_LIBRARIES)