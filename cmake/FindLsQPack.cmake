find_path(LSQPACK_INCLUDE_DIR
        NAMES lsqpack.h
)
find_library(LSQPACK_LIBRARY
        NAMES ls-qpack
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(LsQPack REQUIRED_VARS
        LSQPACK_LIBRARY LSQPACK_INCLUDE_DIR)

if(LSQPACK_FOUND)
    set(LSQPACK_LIBRARIES     ${LSQPACK_LIBRARY})
    set(LSQPACK_INCLUDE_DIRS  ${LSQPACK_INCLUDE_DIR})
endif()

mark_as_advanced(LSQPACK_INCLUDE_DIR LSQPACK_LIBRARY)