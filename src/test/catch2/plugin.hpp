#define CATCH_CONFIG_PREFIX_MESSAGES
#include "catch2/catch_amalgamated.hpp"

// Avoid redefinition issues: replace Catch2's function-like DEPRECATED with an object-like one
#ifdef DEPRECATED
#  undef DEPRECATED
#endif
#ifndef DEPRECATED
#  if defined(__GNUC__) || defined(__clang__)
#    define DEPRECATED __attribute__((deprecated))
#  elif defined(_MSC_VER)
#    define DEPRECATED __declspec(deprecated)
#  else
#    define DEPRECATED
#  endif
#endif

#include "../plugin.hpp"
using namespace Catch;