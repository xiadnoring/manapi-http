#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#   include "ManapiHttp.hpp"
#include "../include/ManapiFetch.hpp"
#else
#   include <manapihttp/ManapiHttp.hpp>
#   include <manapihttp/services/ManapiFetch.hpp>
#endif
#include "./utest.h"