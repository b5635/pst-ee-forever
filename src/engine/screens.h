// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: which screen (CBaldurEngine) is active, and "Continue" from the start screen.
// Game thread only. Evidence: src/engine/symbols.h ("screens").
#pragma once

namespace pstf::engine {

enum class Screen { None, Start, Load, World, Other };
const char* ScreenName(Screen s);

bool ScreensAvailable();  // manifest bytes behind these reads/calls match
Screen ActiveScreen();

// CScreenStart::OnContinueGame: loads the save named in Baldur.lua 'Game Options','Last Save PST'.
// False if there is no such save name (the engine then does nothing).
bool ContinueGame();

}  // namespace pstf::engine
