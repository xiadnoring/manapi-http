find_path(CPPTRACE_INCLUDE_DIRS
        NAMES cpptrace/cpptrace.hpp
)
find_library(CPPTRACE_LIBRARIES
        NAMES cpptrace
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(CppTrace REQUIRED_VARS
        CPPTRACE_LIBRARIES CPPTRACE_INCLUDE_DIRS)

mark_as_advanced(CPPTRACE_INCLUDE_DIRS CPPTRACE_LIBRARIES)