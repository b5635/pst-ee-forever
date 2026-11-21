// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/sprites.h"

#include "core/patch.h"
#include "engine/symbols.h"
#include "engine/sprite_ptr.h"
#include "engine/lua.h"

namespace pstf::engine {
namespace {
template <class T>
T R(uint64_t va) {
  return *reinterpret_cast<const volatile T*>(va);
}

uint64_t Game() {
  const uint64_t c = R<uint64_t>(sym::kVaBaldurChitin);
  return c ? R<uint64_t>(c + sym::kOffChitinGame) : 0;
}

// Free object-array slots hold -1 (crash dumps 2026-11-21: read of FFFFFFFFFFFFFFFF while scanning
// the array). Only dereference plausible user-mode heap pointers.
bool Plausible(uint64_t p) { return p >= 0x10000 && p < 0x00007FFFFFFF0000ull && (p & 7) == 0; }
bool IsSprite(uint64_t p) { return Plausible(p) && R<uint64_t>(p) == sym::kVaSpriteVtable; }

// CGameObjectArray::GetDeny without the lock: index = id >> 16 & 0x7FFF, seq = low word.
uint64_t Lookup(int32_t id) {
  if (id == -1 || (id & 0x8000)) return 0;
  const int idx = (id >> 16) & 0x7FFF;
  if (idx > R<int16_t>(sym::kVaObjMax) || static_cast<int16_t>(id) >= R<int16_t>(sym::kVaObjNext))
    return 0;
  const uint64_t e = sym::kVaObjArray + 16ull * idx;
  if (R<uint16_t>(e) != static_cast<uint16_t>(id)) return 0;
  const uint64_t p = R<uint64_t>(e + 8);
  return IsSprite(p) ? p : 0;
}

bool HudOk();
}  // namespace

// Adapter-internal (engine/sprite_ptr.h): raw lookups for other files of src/engine only.
uint64_t LookupSprite(int32_t id) { return SpritesAvailable() ? Lookup(id) : 0; }
uint64_t GameObject() { return Game(); }
bool PlausiblePtr(uint64_t p) { return Plausible(p); }

namespace {
SpriteInfo Info(uint64_t p) {
  SpriteInfo s;
  s.id = R<int32_t>(p + sym::kOffSpriteId);
  s.x = R<int32_t>(p + sym::kOffSpritePos);
  s.y = R<int32_t>(p + sym::kOffSpritePos + 4);
  s.area = R<uint64_t>(p + sym::kOffSpriteArea);
  s.ea = R<uint8_t>(p + sym::kOffSpriteEa);
  const uint32_t sel = R<int32_t>(p + sym::kOffSpriteStatsSel) ? sym::kOffSpriteStatsA : sym::kOffSpriteStatsB;
  s.alive = (R<uint32_t>(p + sel) & sym::kStateDead) == 0;
  s.hp = R<int16_t>(p + sym::kOffSpriteHp);
  s.busy = R<uint16_t>(p + sym::kOffSpriteCurAction) != 0 || R<int32_t>(p + sym::kOffSpriteQueueCount) > 0;
  s.action = R<uint16_t>(p + sym::kOffSpriteCurAction);
  if (HudOk()) {
    s.max_hp = R<int16_t>(p + (R<int32_t>(p + sym::kOffSpriteStatsSel) ? sym::kOffSpriteMaxHpA
                                                                        : sym::kOffSpriteMaxHpB));
    s.name_ref = R<int32_t>(p + sym::kOffSpriteNameRef);
    s.has_path = R<uint64_t>(p + sym::kOffSpritePath) != 0;
    const uint64_t anim = R<uint64_t>(p + sym::kOffSpriteAnim);
    // Only PST character animations carry the bestiary field.
    if (Plausible(anim) && R<uint64_t>(anim) == sym::kVaAnimPstVtable) s.bestiary = R<int32_t>(anim + sym::kOffAnimBestiary);
  }
  return s;
}
bool HudOk() {
  static const int ok = [] {
    const uint32_t need[] = {sym::kRvaAnimVtblLea, sym::kRvaAnimBestiary, sym::kRvaSpriteAnim,
                             sym::kRvaMaxHp,       sym::kRvaNameRef,       sym::kRvaPathPtr,
                             sym::kRvaPathIndex,   sym::kRvaGridX,         sym::kRvaGridY,
                             sym::kRvaGridSx,      sym::kRvaVisibleArea,   sym::kRvaScreenCoords,
                             sym::kRvaZoom};
    int found = 0;
    for (const auto& s : sym::kAll)
      for (uint32_t r : need)
        if (s.rva == r) {
          if (!CheckSymbol(s)) return 0;
          ++found;
        }
    return found == static_cast<int>(sizeof(need) / sizeof(need[0])) ? 1 : 0;
  }();
  return ok != 0;
}
}  // namespace

bool HudAvailable() { return SpritesAvailable() && HudOk(); }

std::vector<std::pair<int, int>> PathPoints(int32_t id) {
  std::vector<std::pair<int, int>> out;
  const uint64_t p = HudAvailable() ? Lookup(id) : 0;
  if (!p) return out;
  const uint64_t cells = R<uint64_t>(p + sym::kOffSpritePath);
  if (!Plausible(cells)) return out;
  const int gx = R<int16_t>(sym::kVaGridActualX), gy = R<int16_t>(sym::kVaGridActualY);
  const int sx = R<uint8_t>(sym::kVaGridSquareX), sy = R<uint8_t>(sym::kVaGridSquareY);
  const int from = R<int16_t>(p + sym::kOffSpritePathIndex), n = R<int16_t>(p + sym::kOffSpritePathCount);
  if (gx <= 0 || from < 0 || n > 4096) return out;
  for (int i = from; i < n; ++i) {
    const int32_t c = R<int32_t>(cells + 4ull * i);
    out.emplace_back((c % gx) * sx + sx / 2, (gy - 1 - c / gx) * sy + sy / 2);
  }
  return out;
}

bool WorldToUi(int x, int y, int* ux, int* uy) {
  const uint64_t g = HudAvailable() ? Game() : 0;
  if (!g) return false;
  const uint64_t area = R<uint64_t>(g + sym::kOffGameAreas + 8ull * R<uint8_t>(g + sym::kOffGameVisibleIdx));
  if (!Plausible(area)) return false;
  const uint64_t inf = area + sym::kOffAreaInfinity;
  const float zoom = R<float>(area + sym::kOffAreaZoom);
  const int sx = x - R<int32_t>(inf + sym::kOffInfView) + R<int32_t>(inf + sym::kOffInfRect);
  const int sy = y - R<int32_t>(inf + sym::kOffInfView + 4) + R<int32_t>(inf + sym::kOffInfRect + 4);
  const float z = zoom > 0.05f && zoom < 20.f ? zoom : 1.f;
  *ux = static_cast<int>(sx / z);
  *uy = static_cast<int>(sy / z);
  return true;
}

bool UiToWorld(int ux, int uy, int* x, int* y) {
  const uint64_t g = HudAvailable() ? Game() : 0;
  if (!g) return false;
  const uint64_t area = R<uint64_t>(g + sym::kOffGameAreas + 8ull * R<uint8_t>(g + sym::kOffGameVisibleIdx));
  if (!Plausible(area)) return false;
  const uint64_t inf = area + sym::kOffAreaInfinity;
  const float zoom = R<float>(area + sym::kOffAreaZoom);
  const float z = zoom > 0.05f && zoom < 20.f ? zoom : 1.f;
  *x = static_cast<int>(ux * z) + R<int32_t>(inf + sym::kOffInfView) - R<int32_t>(inf + sym::kOffInfRect);
  *y = static_cast<int>(uy * z) + R<int32_t>(inf + sym::kOffInfView + 4) - R<int32_t>(inf + sym::kOffInfRect + 4);
  return true;
}

namespace {
bool SelectOk() {
  static const int ok = [] {
    const uint32_t need[] = {sym::kRvaPortraitSelect, sym::kRvaUnselectAll, sym::kRvaSelectChar,
                             sym::kRvaSelectToolbar, sym::kRvaSelectedFlag};
    int found = 0;
    for (const auto& s : sym::kAll)
      for (uint32_t r : need)
        if (s.rva == r) {
          if (!CheckSymbol(s)) return 0;
          ++found;
        }
    return found == 5 ? 1 : 0;
  }();
  return ok != 0;
}
}  // namespace

namespace {
bool CostOk() {
  static const int ok = [] {
    const uint32_t need[] = {sym::kRvaCostPs, sym::kRvaCostTerrain, sym::kRvaGetCostCall, sym::kRvaGetCost,
                             sym::kRvaImpassableCmp, sym::kRvaMouseStore};
    int found = 0;
    for (const auto& s : sym::kAll)
      for (uint32_t r : need)
        if (s.rva == r) {
          if (!CheckSymbol(s)) return 0;
          ++found;
        }
    return found == 6 ? 1 : 0;
  }();
  return ok != 0;
}
uint64_t VisibleArea() {
  const uint64_t g = Game();
  if (!g) return 0;
  const uint64_t a = R<uint64_t>(g + sym::kOffGameAreas + 8ull * R<uint8_t>(g + sym::kOffGameVisibleIdx));
  return Plausible(a) ? a : 0;
}
}  // namespace

bool CursorWorld(int* x, int* y) {
  const uint64_t area = HudAvailable() && CostOk() ? VisibleArea() : 0;
  if (!area) return false;
  const int mx = R<int32_t>(area + sym::kOffAreaMouse), my = R<int32_t>(area + sym::kOffAreaMouse + 4);
  const uint64_t inf = area + sym::kOffAreaInfinity;
  const int l = R<int32_t>(inf + sym::kOffInfRect), t = R<int32_t>(inf + sym::kOffInfRect + 4);
  const int r = R<int32_t>(inf + sym::kOffInfRect + 8), b = R<int32_t>(inf + sym::kOffInfRect + 12);
  if (mx < l || mx > r || my < t || my > b) return false;
  *x = mx - l + R<int32_t>(inf + sym::kOffInfView);
  *y = my - t + R<int32_t>(inf + sym::kOffInfView + 4);
  return true;
}

int CellCoster::Cost(int cx, int cy, int16_t* pixel) const {
  if (!ok() || cx < 0 || cy < 0 || cx >= width || cy >= height) return 0xFF;
  int32_t cell[2] = {cx, cy};
  int16_t out = 0;
  using GetCostFn = uint8_t(__fastcall*)(uint64_t, const int32_t*, const uint8_t*, uint8_t, int16_t*, int);
  const int c = reinterpret_cast<GetCostFn>(Va(sym::kRvaGetCost))(bitmap, cell, terrain, personal_space, &out, flags);
  if (pixel) *pixel = out;
  return c;
}

CellCoster CosterFor(int32_t id) {
  CellCoster c;
  const uint64_t p = HudAvailable() && CostOk() ? Lookup(id) : 0;
  if (!p) return c;
  const uint64_t area = R<uint64_t>(p + sym::kOffSpriteArea);
  if (!Plausible(area)) return c;
  if (R<uint8_t>(p + sym::kOffSpritePsFlag) & 4) {
    c.personal_space = R<uint8_t>(p + sym::kOffSpritePs);
  } else {
    const uint64_t anim = R<uint64_t>(p + sym::kOffSpriteAnim);
    if (!Plausible(anim)) return c;
    using PsFn = uint8_t(__fastcall*)(uint64_t);
    c.personal_space = reinterpret_cast<PsFn>(R<uint64_t>(R<uint64_t>(anim) + sym::kVtblAnimPersonalSpace))(anim);
  }
  using TerrainFn = const uint8_t*(__fastcall*)(uint64_t);
  c.terrain = reinterpret_cast<TerrainFn>(R<uint64_t>(R<uint64_t>(p) + sym::kVtblSpriteTerrain))(p);
  c.bitmap = area + sym::kOffAreaSearch;
  c.width = R<int32_t>(c.bitmap + 312);
  c.height = R<int32_t>(c.bitmap + 316);
  if (c.width <= 0 || c.height <= 0 || c.width > 4096 || c.height > 4096) c.bitmap = 0;
  return c;
}

bool IsSelected(int32_t id) {
  const uint64_t p = SelectOk() ? Lookup(id) : 0;
  return p && R<int32_t>(p + sym::kOffSpriteSelected) != 0;
}

void SelectOnly(int32_t id) {
  const uint64_t g = SelectOk() && SpritesAvailable() ? Game() : 0;
  if (!g) return;
  reinterpret_cast<void(__fastcall*)(uint64_t)>(Va(sym::kRvaUnselectAll))(g);
  if (id != -1) reinterpret_cast<int64_t(__fastcall*)(uint64_t, int32_t, uint8_t)>(Va(sym::kRvaSelectChar))(g, id, 0);
  reinterpret_cast<void(__fastcall*)(uint64_t)>(Va(sym::kRvaSelectToolbar))(g);
}

bool SpritesAvailable() {
  static const int ok = [] {
    const uint32_t need[] = {sym::kRvaObjArrayLea, sym::kRvaObjMaxCmp, sym::kRvaObjNextCmp,
                             sym::kRvaPartyCount,  sym::kRvaPartyIds,  sym::kRvaSpriteArea,
                             sym::kRvaSpritePos,   sym::kRvaAnimate,   sym::kRvaEnemyEa,
                             sym::kRvaEnemyId,     sym::kRvaQueueCount, sym::kRvaClearAICurAction,
                             sym::kRvaClearAI,     sym::kRvaDropPath,  sym::kRvaEnemy,
                             sym::kRvaWorldTimeChain, sym::kRvaHp};
    int found = 0;
    for (const auto& s : sym::kAll)
      for (uint32_t r : need)
        if (s.rva == r) {
          if (!CheckSymbol(s)) return 0;
          ++found;
        }
    return found == static_cast<int>(sizeof(need) / sizeof(need[0])) ? 1 : 0;
  }();
  return ok != 0;
}

std::vector<SpriteInfo> AllSprites() {
  std::vector<SpriteInfo> out;
  if (!SpritesAvailable()) return out;
  const int max = R<int16_t>(sym::kVaObjMax);
  for (int i = 0; i <= max; ++i) {
    const uint64_t e = sym::kVaObjArray + 16ull * i;
    const uint64_t p = R<uint64_t>(e + 8);
    // A live entry's object carries the id that addresses this slot (index << 16 | seq).
    if (IsSprite(p) && R<int32_t>(p + sym::kOffSpriteId) == ((i << 16) | R<uint16_t>(e)))
      out.push_back(Info(p));
  }
  return out;
}

std::vector<ObjectInfo> AllObjects() {
  std::vector<ObjectInfo> out;
  if (!SpritesAvailable()) return out;
  const int max = R<int16_t>(sym::kVaObjMax);
  for (int i = 0; i <= max; ++i) {
    const uint64_t e = sym::kVaObjArray + 16ull * i;
    const uint64_t p = R<uint64_t>(e + 8);
    if (!Plausible(p) || IsSprite(p)) continue;
    if (R<int32_t>(p + sym::kOffSpriteId) != ((i << 16) | R<uint16_t>(e))) continue;
    ObjectInfo o;
    o.id = R<int32_t>(p + sym::kOffSpriteId);
    o.type = R<uint8_t>(p + 8);
    o.vtable = R<uint64_t>(p);
    o.area = R<uint64_t>(p + sym::kOffSpriteArea);
    o.source = R<int32_t>(p + 0x68);
    o.target = R<int32_t>(p + 0x6C);
    o.x = R<int32_t>(p + sym::kOffSpritePos);
    o.y = R<int32_t>(p + sym::kOffSpritePos + 4);
    for (int k = 0; k < 16; ++k) o.raw[k] = R<int32_t>(p + 0x50 + 4 * k);
    out.push_back(o);
  }
  return out;
}

bool EvalAs(int32_t id, const std::string& bcs) {
  const uint64_t g = SpritesAvailable() ? Game() : 0;
  if (!g || !Lookup(id) || R<int16_t>(g + sym::kOffGamePartyCount) < 1) return false;
  volatile int32_t* slot0 = reinterpret_cast<volatile int32_t*>(g + sym::kOffGamePartyIds);
  const int32_t keep = *slot0;
  *slot0 = id;
  const std::string err = LuaDoString(("C:Eval(" + LuaQuote(bcs) + ", 0)").c_str());
  *slot0 = keep;
  return err.empty();
}

std::vector<int32_t> PartyIds() {
  std::vector<int32_t> ids;
  const uint64_t g = SpritesAvailable() ? Game() : 0;
  if (!g) return ids;
  const int n = R<int16_t>(g + sym::kOffGamePartyCount);
  for (int i = 0; i < n && i < 6; ++i) {
    const int32_t id = R<int32_t>(g + sym::kOffGamePartyIds + 4 * i);
    if (id != -1) ids.push_back(id);
  }
  return ids;
}

bool GetSprite(int32_t id, SpriteInfo* out) {
  const uint64_t p = SpritesAvailable() ? Lookup(id) : 0;
  if (!p) return false;
  *out = Info(p);
  return true;
}

int32_t SpriteIdOf(const void* sprite) {
  return R<int32_t>(reinterpret_cast<uint64_t>(sprite) + sym::kOffSpriteId);
}

void ClearAI(int32_t id) {
  if (const uint64_t p = Lookup(id))
    reinterpret_cast<void(__fastcall*)(uint64_t, uint8_t)>(Va(sym::kRvaClearAI))(p, 0);
}

void DropPath(int32_t id) {
  if (const uint64_t p = Lookup(id))
    reinterpret_cast<void(__fastcall*)(uint64_t)>(Va(sym::kRvaDropPath))(p);
}

bool MakeEnemy(int32_t id) {
  const uint64_t p = Lookup(id);
  if (!p) return false;
  reinterpret_cast<int16_t(__fastcall*)(uint64_t)>(Va(sym::kRvaEnemy))(p);
  return true;
}

}  // namespace pstf::engine
