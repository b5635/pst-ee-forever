// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "engine/lua.h"

namespace pstf::dev {

// Registers PSTF_* Lua functions when CLUAConsole::LuaInit loads luaAuto.cfg. Ini: [dev] lua=1.
bool InstallLuaBridge();

// Other modules add their PSTF_* functions here (before LuaInit runs, i.e. from PSTF_Init).
void AddLuaFunction(const char* name, engine::LuaCFn fn);

}  // namespace pstf::dev
