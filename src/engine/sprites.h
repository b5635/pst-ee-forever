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
// SpellPST's cast-in-progress flag, casting-aura counter (-1 = may cast a spell) and cast-running flag.
bool CastState(int32_t id, int32_t* in_progress, int16_t* counter, bool* running = nullptr);
bool SetCastAura(int32_t id, int16_t counter);
// The spell it is casting now (its CSpell resource's resref), "" when none.
std::string CurrentSpell(int32_t id);
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
  uint64_t occ = 0;  // occupancy bytes (CSearchBitmap +288)
  bool ok() const { return bitmap && terrain; }
  int Cost(int cx, int cy, int16_t* pixel = nullptr) const;
  // The cost the engine's path search uses (CSearchBitmap::SnapshotGetCost lin 33FC20, win 1402AFA70):
  // Cost(), plus for every creature-count bit (0x0E) of the occupancy over the footprint diamond
  // (box (ps-2)/2, |dx|+|dy| <= that + 1) 5 x cost x bits: a standing creature (0x02) is not a wall,
  // it is ten times the ground's cost to walk through, so paths go round it.
  int SearchCost(int cx, int cy) const;
};
CellCoster CosterFor(int32_t id);
// While it lives, the creature's own footprint is out of the live search map (CSearchBitmap +288: one
// occupancy byte per cell, bit 0 / 0x70 stationary creatures, 0x0E moving count; lin 33F6B0
// AddObject / 33F860 RemoveObject, same diamond: box (ps-1)/2, |dx|+|dy| <= ps/2+1), as the engine's
// own search does with the walker (SetTarget lin 51E5F0 L155 RemoveObject; SearchThreadMain
// SnapshotRemoveObject). Restored byte for byte. Game thread only, for a synchronous preview search.
class SearchMapLift {
 public:
  explicit SearchMapLift(int32_t id);
  ~SearchMapLift();
  SearchMapLift(const SearchMapLift&) = delete;
  SearchMapLift& operator=(const SearchMapLift&) = delete;

 private:
  uint64_t occ_ = 0;
  std::vector<std::pair<uint64_t, uint8_t>> saved_;
};
int SearchMapByte(int32_t id, int cx, int cy);  // probe: occupancy byte of a cell in id's area (-1)
void SelectOnly(int32_t id, bool sound = false);  // -1: select nobody; sound: its selection line

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
