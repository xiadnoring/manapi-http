# - Try to find libev
# Once done this will define
#  MSQUIC_FOUND        - System has libev
#  MSQUIC_INCLUDE_DIRS - The libev include directories
#  MSQUIC_LIBRARIES    - The libraries needed to use libev

find_path(MSQUIC_INCLUDE_DIR
        NAMES msquic.h
)
find_library(MSQUIC_LIBRARY
        NAMES msquic
)

#if(LIBEV_INCLUDE_DIR)
#    file(STRINGS "${LIBEV_INCLUDE_DIR}/msquic.h"
#            MSQUIC_VERSION_MAJOR REGEX "^#define[ \t]+MSQUIC_VERSION_MAJOR[ \t]+[0-9]+")
#    file(STRINGS "${LIBEV_INCLUDE_DIR}/msquic.h"
#            MSQUIC_VERSION_MINOR REGEX "^#define[ \t]+MSQUIC_VERSION_MINOR[ \t]+[0-9]+")
#    string(REGEX REPLACE "[^0-9]+" "" MSQUIC_VERSION_MAJOR "${MSQUIC_VERSION_MAJOR}")
#    string(REGEX REPLACE "[^0-9]+" "" MSQUIC_VERSION_MINOR "${MSQUIC_VERSION_MINOR}")
#    set(LIBEV_VERSION "${LIBEV_VERSION_MAJOR}.${LIBEV_VERSION_MINOR}")
#    unset(LIBEV_VERSION_MINOR)
#    unset(LIBEV_VERSION_MAJOR)
#endif()

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LIBEV_FOUND to TRUE
# if all listed variables are TRUE and the requested version matches.
find_package_handle_standard_args(MsQuic REQUIRED_VARS
        MSQUIC_LIBRARY MSQUIC_INCLUDE_DIR
        VERSION_VAR LIBEV_VERSION)

if(MSQUIC_FOUND)
    set(MSQUIC_LIBRARIES     ${MSQUIC_LIBRARY})
    set(MSQUIC_INCLUDE_DIRS  ${MSQUIC_INCLUDE_DIR})
endif()

mark_as_advanced(MSQUIC_INCLUDE_DIR MSQUIC_LIBRARY)