find_path(CPPTRACE_INCLUDE_DIRS
        NAMES cpptrace/cpptrace.hpp
        HINTS
            $ENV{HOME}/wherever
            $ENV{HOME}/wherever/include
)

find_library(CPPTRACE_LIBRARIES
        NAMES cpptrace
        HINTS
            $ENV{HOME}/wherever
            $ENV{HOME}/wherever/lib
            $ENV{HOME}/wherever/lib64
)

find_library(CPPTRACE_DWARF_LIBRARIES
        NAMES dwarf
        HINTS
            $ENV{HOME}/wherever
            $ENV{HOME}/wherever/lib
            $ENV{HOME}/wherever/lib64
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(CppTrace REQUIRED_VARS
        CPPTRACE_LIBRARIES CPPTRACE_INCLUDE_DIRS CPPTRACE_DWARF_LIBRARIES)

mark_as_advanced(CPPTRACE_INCLUDE_DIRS CPPTRACE_LIBRARIES CPPTRACE_DWARF_LIBRARIES)