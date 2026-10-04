#pragma once

// Library functions the Lua engine adds to MiniLua's trimmed standard library,
// written in Lua and run once when the engine creates its Lua state.

namespace StoermelderPackOne {
namespace MidiScript {
namespace Lua {

// string.split(s, sep, limit), modelled on JavaScript's str.split(sep, limit): sep is
// a plain string, not a pattern; empty fields are kept ("a,,b" gives three); an empty
// sep gives the single bytes; no sep gives the whole string. Also callable as s:split(sep).
static const char LUA_STRING_SPLIT_SOURCE[] = R"LUA(
function string.split(s, sep, limit)
    if type(s) ~= 'string' then error("string.split: expected a string", 2) end
    if sep ~= nil and type(sep) ~= 'string' then error('string.split: separator must be a string', 2) end
    if limit ~= nil and type(limit) ~= 'number' then error('string.split: limit must be a number', 2) end
    local t = {}
    if limit == nil or limit < 0 then limit = math.huge end
    if limit < 1 then return t end
    if sep == nil then t[1] = s return t end
    if sep == '' then
        for i = 1, math.min(#s, limit) do t[i] = s:sub(i, i) end
        return t
    end
    local pos = 1
    while #t < limit do
        local a, b = s:find(sep, pos, true)
        if not a then t[#t + 1] = s:sub(pos) break end
        t[#t + 1] = s:sub(pos, a - 1)
        pos = b + 1
    end
    return t
end
)LUA";

} // namespace Lua
} // namespace MidiScript
} // namespace StoermelderPackOne
