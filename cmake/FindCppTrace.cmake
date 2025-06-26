find_path(CPPTRACE_INCLUDE_DIR
        NAMES cpptrace/cpptrace.hpp
)
find_library(CPPTRACE_LIBRARY
        NAMES cpptrace
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(CppTrace REQUIRED_VARS
        CPPTRACE_LIBRARY CPPTRACE_INCLUDE_DIR)

if(CPPTRACE_FOUND)
    set(CPPTRACE_LIBRARIES     ${CPPTRACE_LIBRARY})
    set(CPPTRACE_INCLUDE_DIRS  ${CPPTRACE_INCLUDE_DIR})
endif()

mark_as_advanced(CPPTRACE_INCLUDE_DIR CPPTRACE_LIBRARY)