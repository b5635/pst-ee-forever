// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: world clock, round length, engine mode. Game thread only.
// Evidence for every address/offset: src/engine/symbols.h ("combat-round length", "world clock").
#pragma once
#include <cstdint>

namespace pstf::engine {

bool WorldApiAvailable();  // all manifest bytes behind these reads match in memory

uint32_t RoundLengthTicks();  // CGameAnimationType::ROUND_LENGTH (51 in PST:EE after LoadOptions)
int EngineMode();             // CChitin::ENGINE_MODE (3 = PST:EE)

// CInfGame::m_worldTime. False if no game object yet (main menu before a game exists).
bool WorldTime(uint32_t* ticks, bool* running);

}  // namespace pstf::engine
