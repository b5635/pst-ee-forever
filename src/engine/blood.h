// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter for the gore module: every blood / critical-hit animation request of the engine goes
// through CGameSprite::AddBlood (symbols.h "Gore"). Game thread only.
#pragma once
#include <cstdint>

namespace pstf::engine {

struct BloodEvent {
  int32_t sprite_id = -1;
  int x = 0, y = 0;     // the creature's feet, area px
  int anim_id = -1;     // its animation id (RESDATA.INI section), -1 unreadable
  int direction = 0;    // engine direction 0..15 passed with the hit
  int type = 0;         // CBlood type (3..7 gushes/death, 8 whiff, 9 glancing sparks, 10..17 criticals)
};
// Return true to swallow the engine's own animation for this event (the module draws its own).
using BloodFn = bool (*)(const BloodEvent& e);
bool InstallBloodHook(BloodFn fn);

}  // namespace pstf::engine
