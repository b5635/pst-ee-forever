// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/sprites.h"

#include "core/patch.h"
#include "engine/symbols.h"

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

bool IsSprite(uint64_t p) { return p && R<uint64_t>(p) == sym::kVaSpriteVtable; }

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
    if (anim && R<uint64_t>(anim) == sym::kVaAnimPstVtable) s.bestiary = R<int32_t>(anim + sym::kOffAnimBestiary);
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
  if (!cells) return out;
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
  if (!area) return false;
  const uint64_t inf = area + sym::kOffAreaInfinity;
  const float zoom = R<float>(area + sym::kOffAreaZoom);
  const int sx = x - R<int32_t>(inf + sym::kOffInfView) + R<int32_t>(inf + sym::kOffInfRect);
  const int sy = y - R<int32_t>(inf + sym::kOffInfView + 4) + R<int32_t>(inf + sym::kOffInfRect + 4);
  const float z = zoom > 0.05f && zoom < 20.f ? zoom : 1.f;
  *ux = static_cast<int>(sx / z);
  *uy = static_cast<int>(sy / z);
  return true;
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
