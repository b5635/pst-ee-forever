// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: add our own .menu definitions (menus + inline Lua) to the engine's UI.
// uiLoadMenu copies the UI.MENU text before tokenizing it (win 140459EC3, symbols.h "turn-based
// HUD"); we hand it UI.MENU + our text instead. Nothing on disk changes; with no text added the
// engine parses exactly UI.MENU. Must be installed from PSTF_Init (before CBaldurChitin::Init).
#pragma once
#include <string>

namespace pstf::engine {

// Appends `menu_text` to every UI.MENU load (boot, and the debug UI reload). Call once per module.
void AddMenuText(const std::string& menu_text);
bool InstallMenuHook();

}  // namespace pstf::engine
