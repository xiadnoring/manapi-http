find_path(QUICHE_INCLUDE_DIRS
        NAMES quiche.h
)
find_library(QUICHE_LIBRARIES
        NAMES quiche
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(quiche REQUIRED_VARS
        QUICHE_LIBRARIES QUICHE_INCLUDE_DIRS)

mark_as_advanced(QUICHE_INCLUDE_DIRS QUICHE_LIBRARIES)