/*
 * Single translation unit that compiles the full Lua 5.5 implementation
 * from minilua (https://github.com/edubart/minilua).
 *
 * Only one .c/.cpp file in the project must define LUA_IMPL.
 */

#define LUA_IMPL
#include "minilua.h"
