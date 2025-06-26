find_path(LIBNGHTTP2_INCLUDE_DIR
        NAMES nghttp2/nghttp2.h
)
find_library(LIBNGHTTP2_LIBRARY
        NAMES nghttp2
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(LibNgHttp2 REQUIRED_VARS
        LIBNGHTTP2_LIBRARY LIBNGHTTP2_INCLUDE_DIR)

if(LIBNGHTTP2_FOUND)
    set(LIBNGHTTP2_LIBRARIES     ${LIBNGHTTP2_LIBRARY})
    set(LIBNGHTTP2_INCLUDE_DIRS  ${LIBNGHTTP2_INCLUDE_DIR})
endif()

mark_as_advanced(LIBNGHTTP2_INCLUDE_DIR LIBNGHTTP2_LIBRARY)