// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/blood.h"

#include "core/log.h"
#include "core/patch.h"
#include "engine/sprite_ptr.h"
#include "engine/sprites.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {

template <class T>
T R(uint64_t va) {
  return *reinterpret_cast<const volatile T*>(va);
}

using AddBloodFn = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
AddBloodFn g_add_blood = nullptr;
BloodFn g_fn = nullptr;

uint64_t __fastcall AddBloodDetour(uint64_t sprite, uint64_t a2, uint64_t dir, uint64_t type) {
  if (g_fn && PlausiblePtr(sprite)) {
    BloodEvent e;
    e.sprite_id = SpriteIdOf(reinterpret_cast<const void*>(sprite));
    e.x = R<int32_t>(sprite + sym::kOffSpritePosX);
    e.y = R<int32_t>(sprite + sym::kOffSpritePosY);
    const uint64_t anim = R<uint64_t>(sprite + sym::kOffSpriteAnim);
    e.anim_id = PlausiblePtr(anim) ? int(R<uint16_t>(anim + sym::kOffAnimId)) : -1;
    e.direction = int(int16_t(dir & 0xFFFF));
    e.type = int(int16_t(type & 0xFFFF));
    if (g_fn(e)) return 0;  // the module draws its own effect
  }
  return g_add_blood(sprite, a2, dir, type);
}

}  // namespace

bool InstallBloodHook(BloodFn fn) {
  if (g_add_blood) {
    g_fn = fn;
    return true;
  }
  const sym::Symbol* s = nullptr;
  for (const auto& x : sym::kAll)
    if (x.rva == sym::kRvaAddBlood) s = &x;
  if (!s || !CheckSymbol(*s)) return false;
  const uint64_t t = InlineHook(*s, sym::kAddBloodCopy, reinterpret_cast<void*>(&AddBloodDetour));
  if (!t) return false;
  g_add_blood = reinterpret_cast<AddBloodFn>(t);
  g_fn = fn;
  return true;
}

}  // namespace pstf::engine
