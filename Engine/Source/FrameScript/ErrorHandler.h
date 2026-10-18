#pragma once

#include <cstdint>
#include "Engine/Source/lua-5.1/src/lua.h"

// The original error stays on the stack if the UI's error callback fails.
// In that case its secondary error is left above it for the native logger.
inline int FrameScript_CallErrorHandler(lua_State* L, int reference, int32_t& active) {
    if (reference == -1 || active) { return 0; }
    lua_rawgeti(L, LUA_REGISTRYINDEX, reference);
    lua_pushvalue(L, -2);
    active = 1;
    int result = lua_pcall(L, 1, 1, 0);
    active = 0;
    if (!result) { lua_replace(L, -2); }
    return result;
}
