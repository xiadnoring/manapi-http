find_path(LIBNGHTTP2_INCLUDE_DIRS
        NAMES nghttp2/nghttp2.h
)
find_library(LIBNGHTTP2_LIBRARIES
        NAMES nghttp2
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(LibNgHttp2 REQUIRED_VARS
        LIBNGHTTP2_LIBRARIES LIBNGHTTP2_INCLUDE_DIRS)

mark_as_advanced(LIBNGHTTP2_INCLUDE_DIRS LIBNGHTTP2_LIBRARIES)