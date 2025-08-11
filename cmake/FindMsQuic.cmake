find_path(MSQUIC_INCLUDE_DIRS
        NAMES msquic.h
)
find_library(MSQUIC_LIBRARIES
        NAMES msquic
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(MsQuic REQUIRED_VARS
        MSQUIC_LIBRARIES MSQUIC_INCLUDE_DIRS)

mark_as_advanced(MSQUIC_INCLUDE_DIRS MSQUIC_LIBRARIES)