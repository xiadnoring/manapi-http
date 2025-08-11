find_path(LSQPACK_INCLUDE_DIRS
        NAMES lsqpack.h
)
find_library(LSQPACK_LIBRARIES
        NAMES ls-qpack
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(LsQPack REQUIRED_VARS
        LSQPACK_LIBRARIES LSQPACK_INCLUDE_DIRS)

mark_as_advanced(LSQPACK_INCLUDE_DIRS LSQPACK_LIBRARIES)