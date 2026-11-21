// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: the engine's own Lua 5.2 state (console, .menu UI). Game thread only.
// Only functions confirmed on both binaries are exposed (see symbols.h "Lua 5.2 API").
#pragma once
#include <cstdint>
#include <string>

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
std::string LuaStringArg(LuaState* L, int idx);              // "" if absent (numbers are converted)

// Runs a Lua file in the engine's global state (g_lua). Returns "" on success, else the error text.
// Game thread only, outside any engine Lua call (we use it from the frame tick).
std::string LuaDoFile(const char* path);
std::string LuaDoString(const char* code);  // same, for a chunk of Lua source (luaL_loadstring)
std::string LuaQuote(const std::string& s); // "..." Lua string literal

}  // namespace pstf::engine
