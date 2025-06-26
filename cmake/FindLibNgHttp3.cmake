find_path(LIBNGHTTP3_INCLUDE_DIR
        NAMES nghttp3/nghttp3.h
)
find_library(LIBNGHTTP3_LIBRARY
        NAMES nghttp3
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(LibNgHttp3 REQUIRED_VARS
        LIBNGHTTP3_LIBRARY LIBNGHTTP3_INCLUDE_DIR)

if(LIBNGHTTP3_FOUND)
    set(LIBNGHTTP3_LIBRARIES     ${LIBNGHTTP3_LIBRARY})
    set(LIBNGHTTP3_INCLUDE_DIRS  ${LIBNGHTTP3_INCLUDE_DIR})
endif()

mark_as_advanced(LIBNGHTTP3_INCLUDE_DIR LIBNGHTTP3_LIBRARY)