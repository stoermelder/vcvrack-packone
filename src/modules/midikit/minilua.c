/*
 * Single translation unit that compiles the full Lua 5.4 implementation
 * from minilua (https://github.com/mackron/minilua).
 *
 * Only one .c/.cpp file in the project must define LUA_IMPL.
 */

#define LUA_IMPL
#include "minilua.h"
