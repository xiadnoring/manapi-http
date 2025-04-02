find_path(QUICHE_INCLUDE_DIR
        NAMES quiche.h
)
find_library(QUICHE_LIBRARY
        NAMES quiche
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(quiche REQUIRED_VARS
        QUICHE_LIBRARY QUICHE_INCLUDE_DIR)

if(MSQUIC_FOUND)
    set(QUICHE_LIBRARIES     ${QUICHE_LIBRARY})
    set(QUICHE_INCLUDE_DIRS  ${QUICHE_INCLUDE_DIR})
endif()

mark_as_advanced(QUICHE_INCLUDE_DIR QUICHE_LIBRARY)