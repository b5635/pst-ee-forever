// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: creatures (CGameSprite) by id. Game thread only. Pointers never leave this file;
// callers get ids and plain values. Evidence: src/engine/symbols.h ("creatures").
#pragma once
#include <cstdint>
#include <string>
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
int SpriteAnimId(int32_t id);               // animation id (RESDATA.INI section), -1 if unreadable

void ClearAI(int32_t id);    // CGameSprite::ClearAI(0): actions, triggers, current action
void DropPath(int32_t id);   // CGameSprite::DropPath
bool MakeEnemy(int32_t id);  // CGameSprite::Enemy (the Enemy() action), for staging fights

// Turn-based HUD reads (symbols.h "turn-based HUD"); all false/empty when the bytes differ.
bool HudAvailable();
// Remaining walk path of a creature as world points (cells it has not reached yet).
std::vector<std::pair<int, int>> PathPoints(int32_t id);
// World point of the visible area -> UI (menu) coordinates, as the engine places map notes.
bool WorldToUi(int x, int y, int* ux, int* uy);
bool UiToWorld(int ux, int uy, int* x, int* y);  // inverse of WorldToUi (zoom aware)

// Selection, the engine's way (as a portrait click: UnselectAll + SelectCharacter + SelectToolbar).
bool IsSelected(int32_t id);

// Cursor position in the world of the visible area (false when it is outside the world viewport).
bool CursorWorld(int* x, int* y);

// Walk cost of search-map cells for one creature, as CGameSprite::SetTarget asks the engine
// (CSearchBitmap::GetCost with its terrain table and personal space). 0xFF = impassable.
struct CellCoster {
  uint64_t bitmap = 0;
  const uint8_t* terrain = nullptr;
  uint8_t personal_space = 1;
  int flags = 0;
  int width = 0, height = 0;  // search map in cells (CSearchBitmap +312/+316, GetCost's bounds)  // GetCost's last argument (SetTarget passes 1); 0 = terrain only
  bool ok() const { return bitmap && terrain; }
  int Cost(int cx, int cy, int16_t* pixel = nullptr) const;
};
CellCoster CosterFor(int32_t id);
void SelectOnly(int32_t id);  // -1: select nobody

// Sub-tick draw state (symbols.h "frame interpolation"). draw_x/draw_y are the integer positions the
// engine draws at (object +0x0C/+0x10); x/y are exact positions. For a walking creature x/y carry the
// fraction the engine keeps between ticks (Q10 at sprite+0x442C/+0x4430, y scaled 3/4), so x/y differ
// from draw_x/draw_y; for anything else (a creature that is not walking, a projectile) they are the
// integer position.
struct MoveState {
  float x = 0.0f, y = 0.0f;
  int draw_x = 0, draw_y = 0;
  bool exact = false;  // x/y carry a sub-pixel fraction
};
bool SpriteMoveState(int32_t id, MoveState* out);   // creature
bool ObjectMoveState(int32_t id, MoveState* out);   // non-creature object (projectiles)

// Temporary draw shift for the render pass (used by engine/frame.cpp on the game thread): pos becomes
// draw + (dx, dy) and the old ints come back in old_x/old_y so the caller can put them back.
bool ShiftSpritePos(int32_t id, int dx, int dy, int* old_x, int* old_y);
void RestoreSpritePos(int32_t id, int x, int y);
bool ShiftObjectPos(int32_t id, int dx, int dy, int* old_x, int* old_y);
void RestoreObjectPos(int32_t id, int x, int y);

// Every live object in the object array that is not a creature (projectiles, doors, ...): for probes
// and for "the caster's spell is still resolving".
struct ObjectInfo {
  int32_t id = -1;
  uint8_t type = 0;       // CGameObject +8 (GetObjectType)
  uint64_t vtable = 0;    // compare only
  uint64_t area = 0;      // compare only
  int32_t source = -1;    // projectiles: the caster (CProjectile +0x68; DeliverEffects lin 2BAB20 +0x64, win 140282EC0)
  int32_t target = -1;    // projectiles: +0x6C
  int x = 0, y = 0;
  int32_t raw[16] = {};   // probe: dwords +0x50..+0x8C
};
std::vector<ObjectInfo> AllObjects();

// Queues BCS actions on any creature through the engine's own console path (CLUAConsole::Eval,
// lin 27BFE0): Eval(text, slot) inserts the parsed actions into the creature in party slot `slot`;
// party slot 0 holds `id` for the duration of that one synchronous call. Game thread only.
bool EvalAs(int32_t id, const std::string& bcs);


}  // namespace pstf::engine
