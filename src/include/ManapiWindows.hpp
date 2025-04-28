#pragma once

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <processthreadsapi.h>
#   include <io.h>
#   include <fcntl.h>
#   include <stdlib.h>
#   include <stdio.h>
#   include <share.h>
#endif