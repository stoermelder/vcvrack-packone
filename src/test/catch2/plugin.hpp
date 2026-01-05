#define CATCH_CONFIG_PREFIX_MESSAGES
#define CATCH_CONFIG_THREAD_SAFE_ASSERTIONS
#define CATCH_CONFIG_FAST_COMPILE
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

// We will call several deprecated functions in the tests, thus disable warnings here
#if defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__) || defined(__GNUG__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif