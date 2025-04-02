find_path(MSQUIC_INCLUDE_DIR
        NAMES msquic.h
)
find_library(MSQUIC_LIBRARY
        NAMES msquic
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(MsQuic REQUIRED_VARS
        MSQUIC_LIBRARY MSQUIC_INCLUDE_DIR)

if(MSQUIC_FOUND)
    set(MSQUIC_LIBRARIES     ${MSQUIC_LIBRARY})
    set(MSQUIC_INCLUDE_DIRS  ${MSQUIC_INCLUDE_DIR})
endif()

mark_as_advanced(MSQUIC_INCLUDE_DIR MSQUIC_LIBRARY)