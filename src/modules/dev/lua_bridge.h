// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace pstf::dev {

// Registers PSTF_* Lua functions when CLUAConsole::LuaInit loads luaAuto.cfg. Ini: [dev] lua=1.
bool InstallLuaBridge();

}  // namespace pstf::dev
