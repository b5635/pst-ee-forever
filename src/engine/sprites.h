// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: creatures (CGameSprite) by id. Game thread only. Pointers never leave this file;
// callers get ids and plain values. Evidence: src/engine/symbols.h ("creatures").
#pragma once
#include <cstdint>
#include <utility>
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
  // HUD fields (valid only if HudAvailable()):
  int max_hp = 0;
  int32_t name_ref = -1;   // TLK strref of the name (-1: party member with a custom name)
  int32_t bestiary = -1;   // PST bestiary index of its animation (-1: none)
  uint16_t action = 0;     // current action id (ACTION.IDS)
  bool has_path = false;   // the engine has a walk path for it
};

bool SpritesAvailable();

std::vector<SpriteInfo> AllSprites();       // every CGameSprite in the object array
std::vector<int32_t> PartyIds();            // CInfGame party slots, in order
bool GetSprite(int32_t id, SpriteInfo* out);
int32_t SpriteIdOf(const void* sprite);     // for hooks that receive `this`

void ClearAI(int32_t id);    // CGameSprite::ClearAI(0): actions, triggers, current action
void DropPath(int32_t id);   // CGameSprite::DropPath
bool MakeEnemy(int32_t id);  // CGameSprite::Enemy (the Enemy() action), for staging fights

// Turn-based HUD reads (symbols.h "turn-based HUD"); all false/empty when the bytes differ.
bool HudAvailable();
// Remaining walk path of a creature as world points (cells it has not reached yet).
std::vector<std::pair<int, int>> PathPoints(int32_t id);
// World point of the visible area -> UI (menu) coordinates, as the engine places map notes.
bool WorldToUi(int x, int y, int* ux, int* uy);

}  // namespace pstf::engine
