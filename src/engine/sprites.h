// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: creatures (CGameSprite) by id. Game thread only. Pointers never leave this file;
// callers get ids and plain values. Evidence: src/engine/symbols.h ("creatures").
#pragma once
#include <cstdint>
#include <vector>

namespace pstf::engine {

struct SpriteInfo {
  int32_t id = -1;
  int x = 0, y = 0;
  uint64_t area = 0;  // identity only (compare), never dereferenced outside the adapter
  uint8_t ea = 0;
  bool alive = false;
  bool busy = false;  // has a current action or queued actions
  int hp = 0;         // current hit points
};

bool SpritesAvailable();

std::vector<SpriteInfo> AllSprites();       // every CGameSprite in the object array
std::vector<int32_t> PartyIds();            // CInfGame party slots, in order
bool GetSprite(int32_t id, SpriteInfo* out);
int32_t SpriteIdOf(const void* sprite);     // for hooks that receive `this`

void ClearAI(int32_t id);    // CGameSprite::ClearAI(0): actions, triggers, current action
void DropPath(int32_t id);   // CGameSprite::DropPath
bool MakeEnemy(int32_t id);  // CGameSprite::Enemy (the Enemy() action), for staging fights

}  // namespace pstf::engine
