// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/sprites.h"
#include "engine/party.h"

#include <cstdlib>

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

void W32(uint64_t va, int32_t v) {
  *reinterpret_cast<volatile int32_t*>(va) = v;
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
// Any object (sprite, projectile, ...) by id, 0 if the slot does not hold it.
uint64_t LookupAny(int32_t id) {
  if (id == -1 || (id & 0x8000)) return 0;
  const int idx = (id >> 16) & 0x7FFF;
  if (idx > R<int16_t>(sym::kVaObjMax) || static_cast<int16_t>(id) >= R<int16_t>(sym::kVaObjNext))
    return 0;
  const uint64_t e = sym::kVaObjArray + 16ull * idx;
  if (R<uint16_t>(e) != static_cast<uint16_t>(id)) return 0;
  const uint64_t p = R<uint64_t>(e + 8);
  return Plausible(p) && R<int32_t>(p + sym::kOffSpriteId) == id ? p : 0;
}

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
bool TargetOk() {
  static const int ok = [] {
    for (const auto& s : sym::kAll)
      if (s.rva == sym::kRvaTargetId) return CheckSymbol(s) ? 1 : 0;
    return 0;
  }();
  return ok != 0;
}
}  // namespace

// Adapter-internal (engine/sprite_ptr.h): raw lookups for other files of src/engine only.
uint64_t LookupSprite(int32_t id) { return SpritesAvailable() ? Lookup(id) : 0; }
uint64_t GameObject() { return Game(); }
uint64_t LookupObject(int32_t id) { return SpritesAvailable() ? LookupAny(id) : 0; }
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
  if (TargetOk()) s.target = R<int32_t>(p + sym::kOffSpriteTargetId);
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

int32_t PickedId() {
  const uint64_t area = HudAvailable() ? VisibleArea() : 0;
  return area ? R<int32_t>(area + sym::kOffAreaPicked) : -1;
}

bool SpriteIsOver(int32_t id, int x, int y) {
  const uint64_t p = HudAvailable() ? Lookup(id) : 0;
  if (!p) return false;
  const uint64_t fn = R<uint64_t>(R<uint64_t>(p) + sym::kVtblSpriteIsOver);
  if (fn != sym::kVaSpriteIsOver) return false;  // not the 3.2.2 sprite vtable we know
  const int32_t pt[2] = {x, y};
  return reinterpret_cast<int(__fastcall*)(uint64_t, const int32_t*)>(fn)(p, pt) != 0;
}

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

namespace {
// The search map of the creature's area, its occupancy bytes and size (0 if none).
uint64_t SearchOcc(int32_t id, int* w, int* h, int* cx, int* cy) {
  const uint64_t p = HudAvailable() && CostOk() ? Lookup(id) : 0;
  if (!p) return 0;
  const uint64_t area = R<uint64_t>(p + sym::kOffSpriteArea);
  if (!Plausible(area)) return 0;
  const uint64_t bm = area + sym::kOffAreaSearch;
  *w = R<int32_t>(bm + 312);
  *h = R<int32_t>(bm + 316);
  const uint64_t occ = R<uint64_t>(bm + 288);
  if (*w <= 0 || *h <= 0 || *w > 4096 || *h > 4096 || !Plausible(occ)) return 0;
  if (cx) *cx = R<int32_t>(p + sym::kOffSpritePos) / 16;
  if (cy) *cy = R<int32_t>(p + sym::kOffSpritePos + 4) / 12;
  return occ;
}
}  // namespace

SearchMapLift::SearchMapLift(int32_t id) {
  int w = 0, h = 0, cx = 0, cy = 0;
  const uint64_t occ = SearchOcc(id, &w, &h, &cx, &cy);
  if (!occ) return;
  const int ps = CosterFor(id).personal_space;
  const int half = (ps - 1) / 2, diamond = ps / 2 + 1;
  for (int y = cy - half; y <= cy + half; ++y)
    for (int x = cx - half; x <= cx + half; ++x) {
      if (x < 0 || y < 0 || x >= w || y >= h || std::abs(x - cx) + std::abs(y - cy) > diamond) continue;
      const uint64_t a = occ + static_cast<uint64_t>(y) * w + x;
      const uint8_t b = R<uint8_t>(a);
      if (!(b & 0x7F)) continue;
      saved_.push_back({a, b});
      *reinterpret_cast<volatile uint8_t*>(a) = static_cast<uint8_t>(b & 0x80);
    }
  occ_ = occ;
}
SearchMapLift::~SearchMapLift() {
  for (const auto& s : saved_) *reinterpret_cast<volatile uint8_t*>(s.first) = s.second;
}
int SearchMapByte(int32_t id, int cx, int cy) {
  int w = 0, h = 0;
  const uint64_t occ = SearchOcc(id, &w, &h, nullptr, nullptr);
  if (!occ || cx < 0 || cy < 0 || cx >= w || cy >= h) return -1;
  return R<uint8_t>(occ + static_cast<uint64_t>(cy) * w + cx);
}

int CellCoster::SearchCost(int cx, int cy) const {
  const int base = Cost(cx, cy);
  if (base >= 0xFF || !occ) return base;
  const int half = (personal_space - 2) / 2;
  int cost = base;
  for (int dy = -half; dy <= half; ++dy)
    for (int dx = -half; dx <= half; ++dx) {
      const int x = cx + dx, y = cy + dy;
      if (x < 0 || y < 0 || x >= width || y >= height || std::abs(dx) + std::abs(dy) > half + 1) continue;
      cost += 5 * base * (R<uint8_t>(occ + static_cast<uint64_t>(y) * width + x) & 0x0E);
    }
  return cost;
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
  c.occ = c.bitmap ? R<uint64_t>(c.bitmap + 288) : 0;
  if (!Plausible(c.occ)) c.occ = 0;
  return c;
}

bool IsSelected(int32_t id) {
  const uint64_t p = SelectOk() ? Lookup(id) : 0;
  return p && R<int32_t>(p + sym::kOffSpriteSelected) != 0;
}

void SelectOnly(int32_t id, bool sound) {
  const uint64_t g = SelectOk() && SpritesAvailable() ? Game() : 0;
  if (!g) return;
  reinterpret_cast<void(__fastcall*)(uint64_t)>(Va(sym::kRvaUnselectAll))(g);
  if (id != -1) reinterpret_cast<int64_t(__fastcall*)(uint64_t, int32_t, uint8_t)>(Va(sym::kRvaSelectChar))(g, id, sound ? 1 : 0);  // SelectCharacter(id, playSound) lin 36A1A0 L110
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

bool SpriteMoveState(int32_t id, MoveState* out) {
  if (!out) return false;
  const uint64_t p = SpritesAvailable() ? Lookup(id) : 0;
  if (!p) return false;
  out->draw_x = R<int32_t>(p + sym::kOffSpritePos);
  out->draw_y = R<int32_t>(p + sym::kOffSpritePos + 4);
  const int32_t step_x = R<int32_t>(p + sym::kOffSpriteWalkStepX);
  const int32_t step_y = R<int32_t>(p + sym::kOffSpriteWalkStepY);
  if (step_x == 0 && step_y == 0) {
    out->x = float(out->draw_x);
    out->y = float(out->draw_y);
    out->exact = false;
    return true;
  }
  const int32_t acc_x = R<int32_t>(p + sym::kOffSpriteWalkAccX);
  const int32_t acc_y = R<int32_t>(p + sym::kOffSpriteWalkAccY);
  // The engine derives pos from the accumulators: x = acc_x >> 10, y = (acc_y*3 + round_term) >> 2 >> 10
  // (1403B76DC..1403B76F4: lea eax,[rax+rax*2]; cdq; and edx,3; add; sar 2; sar 0Ah). The accumulators
  // must agree with pos to be usable; a stale pair (after a teleport, a blocked step, a new area) falls
  // back to the integer position.
  const int ty = acc_y * 3;
  const int yr = ((ty + (acc_y < 0 ? 3 : (ty & 3))) >> 2) >> 10;
  if (std::abs((acc_x >> 10) - out->draw_x) > 1 || std::abs(yr - out->draw_y) > 1) {
    out->x = float(out->draw_x);
    out->y = float(out->draw_y);
    out->exact = false;
    return true;
  }
  out->x = float(acc_x) / 1024.0f;
  out->y = (float(acc_y) * 0.75f) / 1024.0f;  // 12 px up/down per 16 px across (AIUpdateWalk 1403B76DC)
  out->exact = true;
  return true;
}

bool ObjectMoveState(int32_t id, MoveState* out) {
  if (!out) return false;
  const uint64_t p = SpritesAvailable() ? LookupObject(id) : 0;
  if (!p) return false;
  out->draw_x = R<int32_t>(p + sym::kOffSpritePos);
  out->draw_y = R<int32_t>(p + sym::kOffSpritePos + 4);
  out->x = float(out->draw_x);
  out->y = float(out->draw_y);
  out->exact = false;
  return true;
}

bool ShiftSpritePos(int32_t id, int dx, int dy, int* old_x, int* old_y) {
  const uint64_t p = SpritesAvailable() ? Lookup(id) : 0;
  if (!p || !old_x || !old_y) return false;
  *old_x = R<int32_t>(p + sym::kOffSpritePos);
  *old_y = R<int32_t>(p + sym::kOffSpritePos + 4);
  W32(p + sym::kOffSpritePos, *old_x + dx);
  W32(p + sym::kOffSpritePos + 4, *old_y + dy);
  return true;
}

void RestoreSpritePos(int32_t id, int x, int y) {
  const uint64_t p = SpritesAvailable() ? Lookup(id) : 0;
  if (!p) return;
  W32(p + sym::kOffSpritePos, x);
  W32(p + sym::kOffSpritePos + 4, y);
}

bool ShiftObjectPos(int32_t id, int dx, int dy, int* old_x, int* old_y) {
  const uint64_t p = SpritesAvailable() ? LookupObject(id) : 0;
  if (!p || !old_x || !old_y) return false;
  *old_x = R<int32_t>(p + sym::kOffSpritePos);
  *old_y = R<int32_t>(p + sym::kOffSpritePos + 4);
  W32(p + sym::kOffSpritePos, *old_x + dx);
  W32(p + sym::kOffSpritePos + 4, *old_y + dy);
  return true;
}

void RestoreObjectPos(int32_t id, int x, int y) {
  const uint64_t p = SpritesAvailable() ? LookupObject(id) : 0;
  if (!p) return;
  W32(p + sym::kOffSpritePos, x);
  W32(p + sym::kOffSpritePos + 4, y);
}

bool EvalAs(int32_t id, const std::string& bcs) {
  const uint64_t g = SpritesAvailable() ? Game() : 0;
  if (!g || !Lookup(id) || R<int16_t>(g + Party().count_off) < 1) return false;
  volatile int32_t* slot0 = reinterpret_cast<volatile int32_t*>(g + Party().order_off);
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
  const int n = R<int16_t>(g + Party().count_off);
  for (int i = 0; i < n && i < Party().max; ++i) {
    const int32_t id = R<int32_t>(g + Party().order_off + 4 * i);
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

int SpriteAnimId(int32_t id) {
  const uint64_t p = LookupSprite(id);
  if (!p) return -1;
  const uint64_t anim = *reinterpret_cast<const volatile uint64_t*>(p + sym::kOffSpriteAnim);
  if (!Plausible(anim)) return -1;
  return int(*reinterpret_cast<const volatile uint16_t*>(anim + sym::kOffAnimId));
}

int32_t SpriteIdOf(const void* sprite) {
  return R<int32_t>(reinterpret_cast<uint64_t>(sprite) + sym::kOffSpriteId);
}

void ClearAI(int32_t id) {
  if (const uint64_t p = Lookup(id))
    reinterpret_cast<void(__fastcall*)(uint64_t, uint8_t)>(Va(sym::kRvaClearAI))(p, 0);
}

// The engine's spell-cast state (CGameSprite::SpellPST lin 4CDDC0 l.196-310: this+0x4694 "cast in progress",
// the WORD aura counter this+0x4690 (-1 = may cast) and this+0x4698 "cast running"; win 14042E3F0
// cmp [rbx+46ACh], esi / cmp word ptr [rbx+46A8h], 0FFFFh; CGameSprite::DoAction win 1404079D0 counts it:
// movzx r8d, word ptr [rcx+46A8h] ... cmp [rdi+46B0h], r15d ... mov [rdi+46A8h], r12w).
bool CastState(int32_t id, int32_t* in_progress, int16_t* counter, bool* running) {
  const uint64_t p = Lookup(id);
  if (!p) return false;
  if (in_progress) *in_progress = R<int32_t>(p + 0x46AC);
  if (counter) *counter = R<int16_t>(p + 0x46A8);
  if (running) *running = R<int32_t>(p + 0x46B0) != 0;
  return true;
}

// The CSpell being cast: CGameSprite::SpellPST lin 4CDDC0 l.202 `this + 2300` (+0x47E0 on Linux), win 14042E632
// `mov rdi, [rbx+47F8h]` right after the in-progress test (+0x46AC, Linux +0x4694: Windows is +0x18 here);
// The holder is a 16-byte CResHelper (SpellPST lin l.356-379: operator new(0x10), +0 CRes*, +8 the CResRef itself,
// 8 inline chars, upper case, NUL-padded), not a CRes: reading +8 as a name pointer found nothing, ever.
std::string CurrentSpell(int32_t id) {
  const uint64_t p = Lookup(id);
  if (!p) return "";
  const uint64_t helper = R<uint64_t>(p + 0x47F8);
  if (!PlausiblePtr(helper)) return "";
  std::string out;
  for (int i = 0; i < 8; ++i) {
    const char c = R<char>(helper + 8 + i);
    if (!c) break;
    if (c < 0x20 || c > 0x7E) return "";
    out += c;
  }
  return out;
}

bool SetCastAura(int32_t id, int16_t counter) {
  const uint64_t p = Lookup(id);
  if (!p) return false;
  *reinterpret_cast<volatile int16_t*>(p + 0x46A8) = counter;
  return true;
}

void DropPath(int32_t id) {
  const uint64_t p = Lookup(id);
  if (!p) return;
  reinterpret_cast<void(__fastcall*)(uint64_t)>(Va(sym::kRvaDropPath))(p);
  // As CMessageDropPath does: a walk cut short goes back to the ready stance (no running in place).
  static const int seq_ok = [] {
    int found = 0;
    for (const auto& s : sym::kAll)
      if ((s.rva == sym::kRvaDropPathSeq || s.rva == sym::kRvaSetSequence) && CheckSymbol(s)) ++found;
    return found == 2 ? 1 : 0;
  }();
  if (seq_ok && R<uint16_t>(p + sym::kOffSpriteSequence) == sym::kSeqWalk)
    reinterpret_cast<void(__fastcall*)(uint64_t, int16_t)>(Va(sym::kRvaSetSequence))(p, sym::kSeqReady);
}

bool MakeEnemy(int32_t id) {
  const uint64_t p = Lookup(id);
  if (!p) return false;
  reinterpret_cast<int16_t(__fastcall*)(uint64_t)>(Va(sym::kRvaEnemy))(p);
  return true;
}

}  // namespace pstf::engine
