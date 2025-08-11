find_path(LIBNGHTTP3_INCLUDE_DIRS
        NAMES nghttp3/nghttp3.h
)
find_library(LIBNGHTTP3_LIBRARIES
        NAMES nghttp3
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(LibNgHttp3 REQUIRED_VARS
        LIBNGHTTP3_LIBRARIES LIBNGHTTP3_INCLUDE_DIRS)

mark_as_advanced(LIBNGHTTP3_INCLUDE_DIRS LIBNGHTTP3_LIBRARIES)