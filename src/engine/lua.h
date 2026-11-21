// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: the engine's own Lua 5.2 state (console, .menu UI). Game thread only.
// Only functions confirmed on both binaries are exposed (see symbols.h "Lua 5.2 API").
#pragma once
#include <cstdint>

namespace pstf::engine {

struct LuaState;  // opaque lua_State
using LuaCFn = int(__cdecl*)(LuaState*);

bool LuaApiAvailable();

void LuaRegister(LuaState* L, const char* global_name, LuaCFn fn);  // pushcclosure + setglobal
void LuaPushNumber(LuaState* L, double v);
void LuaPushString(LuaState* L, const char* s);
void LuaPushBool(LuaState* L, bool v);
int LuaTop(LuaState* L);
double LuaNumberArg(LuaState* L, int idx, double fallback);  // fallback if absent / not a number

}  // namespace pstf::engine
