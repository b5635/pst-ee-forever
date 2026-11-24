// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/locks.h"

#include <cstdio>

#include "core/log.h"
#include "core/patch.h"
#include "engine/sprite_ptr.h"
#include "engine/sprites.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {

template <class T>
T R(uint64_t a) {
  return *reinterpret_cast<const volatile T*>(a);
}
template <class T>
void W(uint64_t a, T v) {
  *reinterpret_cast<volatile T*>(a) = v;
}

using ActionFn = uint64_t(__fastcall*)(uint64_t sprite, uint64_t target);
using RandFn = int64_t(__fastcall*)(int64_t range, int64_t mod);

ActionFn g_pick = nullptr, g_bash = nullptr;
RandFn g_rand = nullptr;
LockFn g_fn = nullptr;
bool g_armed = false;   // the next UtilRandInt from a hooked call site returns g_forced
int64_t g_forced = 0;

constexpr uint64_t kActionNormal = 0, kActionDone = 0xFFFF;

const sym::Symbol* Find(uint32_t rva) {
  for (const auto& s : sym::kAll)
    if (s.rva == rva) return &s;
  return nullptr;
}

uint64_t Stats(uint64_t p) {
  return p + (R<int32_t>(p + sym::kOffSpriteStatsSel) ? sym::kOffSpriteStatsA : sym::kOffSpriteStatsB);
}

Pt Pos(uint64_t p) { return {R<int32_t>(p + sym::kOffSpritePos), R<int32_t>(p + sym::kOffSpritePos + 4)}; }

// Lock state of a door/container object (false for anything else).
bool ReadLock(uint64_t o, bool* door, bool* locked, int* difficulty) {
  const uint8_t type = R<uint8_t>(o + 8);
  if (type == sym::kObjTypeDoor) {
    *door = true;
    *locked = (R<uint32_t>(o + sym::kOffDoorFlags) & sym::kDoorLocked) != 0;
    *difficulty = static_cast<int>(R<uint32_t>(o + sym::kOffDoorLock));
    return true;
  }
  if (type == sym::kObjTypeContainer) {
    *door = false;
    *locked = (R<uint32_t>(o + sym::kOffContainerFlags) & sym::kContainerLocked) != 0;
    *difficulty = R<uint16_t>(o + sym::kOffContainerLock);
    return true;
  }
  return false;
}

// Where the engine stands to work the lock: a door's nearer stand point, a container's own position.
Pt WorkPoint(uint64_t o, bool door, Pt actor) {
  if (!door) return Pos(o);
  const Pt a{R<int32_t>(o + sym::kOffDoorStand1), R<int32_t>(o + sym::kOffDoorStand1 + 4)};
  const Pt b{R<int32_t>(o + sym::kOffDoorStand2), R<int32_t>(o + sym::kOffDoorStand2 + 4)};
  return NearerStand(actor, a, b);
}

bool ReadTry(LockAct act, uint64_t sprite, uint64_t target, LockTry* t) {
  if (!PlausiblePtr(sprite) || !PlausiblePtr(target)) return false;
  if (!ReadLock(target, &t->door, &t->locked, &t->difficulty)) return false;
  t->act = act;
  t->actor = R<int32_t>(sprite + sym::kOffSpriteId);
  t->target = R<int32_t>(target + sym::kOffSpriteId);
  const Pt me = Pos(sprite);
  t->in_range = CellDist2(WorkPoint(target, t->door, me), me) <= (act == LockAct::Pick ? sym::kPickRange2 : sym::kBashRange2);
  const uint64_t s = Stats(sprite);
  t->lockpicking = R<int16_t>(s + sym::kOffStatLockpicking);
  t->luck = R<int16_t>(s + sym::kOffStatLuck);
  t->str = R<int16_t>(s + sym::kOffStatStr);
  t->str_extra = R<int16_t>(s + sym::kOffStatStrExtra);
  return true;
}

// The engine's random number on the call sites we redirected: forced once when armed.
int64_t __fastcall RandDetour(int64_t range, int64_t mod) {
  if (g_armed) {
    g_armed = false;
    return g_forced;
  }
  return g_rand(range, mod);
}

uint64_t Run(LockAct act, ActionFn orig, uint64_t sprite, uint64_t target) {
  LockTry t;
  if (!g_fn || !ReadTry(act, sprite, target, &t)) return orig(sprite, target);
  const LockStep step = g_fn(t);
  switch (step) {
    case LockStep::Vanilla:
      return orig(sprite, target);
    case LockStep::Wait:
      return kActionNormal;
    case LockStep::Cancel:
      return kActionDone;
    case LockStep::Succeed:
    case LockStep::Fail:
      break;
  }
  const bool ok = step == LockStep::Succeed;
  // PickLock opens when (it + LOCKPICKING) > difficulty, compared unsigned for containers: a failure makes
  // the total exactly 0 (never above a difficulty). BashDoor's total is an i16 compared signed.
  if (act == LockAct::Pick)
    g_forced = ok ? 1000 : -static_cast<int64_t>(t.lockpicking);
  else
    g_forced = ok ? 1000 : -1000;
  g_armed = true;
  const uint64_t r = orig(sprite, target);
  if (g_armed) {
    g_armed = false;
    Log("locks: %s of %d by %d resolved without a roll (engine result %d)", act == LockAct::Pick ? "pick" : "bash",
        t.target, t.actor, static_cast<int16_t>(r));
  }
  return r;
}

uint64_t __fastcall HookPick(uint64_t sprite, uint64_t target) { return Run(LockAct::Pick, g_pick, sprite, target); }
uint64_t __fastcall HookBash(uint64_t sprite, uint64_t target) { return Run(LockAct::Bash, g_bash, sprite, target); }

}  // namespace

bool InstallLockHooks(LockFn fn) {
  if (g_pick) {
    g_fn = fn;
    return true;
  }
  const uint32_t rvas[] = {sym::kRvaPickLockCall, sym::kRvaBashDoorCall, sym::kRvaPickLockRand, sym::kRvaBashRand};
  for (uint32_t r : rvas) {
    const sym::Symbol* s = Find(r);
    if (!s || !CheckSymbol(*s)) return false;
  }
  const uint64_t p = RedirectCall(*Find(sym::kRvaPickLockCall), reinterpret_cast<void*>(&HookPick));
  const uint64_t b = RedirectCall(*Find(sym::kRvaBashDoorCall), reinterpret_cast<void*>(&HookBash));
  const uint64_t r1 = RedirectCall(*Find(sym::kRvaPickLockRand), reinterpret_cast<void*>(&RandDetour));
  const uint64_t r2 = RedirectCall(*Find(sym::kRvaBashRand), reinterpret_cast<void*>(&RandDetour));
  if (p != sym::kVaPickLock || b != sym::kVaBashDoor || r1 != sym::kVaUtilRandInt || r2 != sym::kVaUtilRandInt) {
    Log("locks: call targets differ (pick %llX bash %llX rand %llX %llX)", static_cast<unsigned long long>(p),
        static_cast<unsigned long long>(b), static_cast<unsigned long long>(r1), static_cast<unsigned long long>(r2));
    return false;
  }
  g_pick = reinterpret_cast<ActionFn>(p);
  g_bash = reinterpret_cast<ActionFn>(b);
  g_rand = reinterpret_cast<RandFn>(r1);
  g_fn = fn;
  Log("locks: PickLock/BashDoor hooked (2 action calls, 2 UtilRandInt calls)");
  return true;
}

std::string LockObjects(int32_t actor, int radius) {
  std::string out;
  const uint64_t a = LookupSprite(actor);
  if (!a) return out;
  const Pt me = Pos(a);
  const uint64_t area = R<uint64_t>(a + sym::kOffSpriteArea);
  for (const ObjectInfo& o : AllObjects()) {
    if (o.area != area) continue;
    const uint64_t p = LookupObject(o.id);
    bool door = false, locked = false;
    int diff = 0;
    if (!p || !ReadLock(p, &door, &locked, &diff)) continue;
    const Pt w = WorkPoint(p, door, me);
    const int dx = w.x - me.x, dy = w.y - me.y;
    if (dx * dx + dy * dy > radius * radius) continue;
    char name[33] = {};
    for (int i = 0; i < 32; ++i) {
      const char c = R<char>(p + (door ? sym::kOffDoorName : sym::kOffObjScriptName) + i);
      if (!c) break;
      name[i] = (c == ':' || c == ';') ? '_' : c;
    }
    char b[160];
    std::snprintf(b, sizeof b, "%d:%s:%d:%d:%d,%d:%s;", o.id, door ? "door" : "box", locked ? 1 : 0, diff, w.x, w.y, name);
    out += b;
  }
  return out;
}

bool SetLock(int32_t id, bool locked, int difficulty) {
  const uint64_t p = LookupObject(id);
  bool door = false, was = false;
  int diff = 0;
  if (!p || !ReadLock(p, &door, &was, &diff)) return false;
  if (door) {
    const uint32_t f = R<uint32_t>(p + sym::kOffDoorFlags);
    W<uint32_t>(p + sym::kOffDoorFlags, locked ? (f | sym::kDoorLocked) : (f & ~sym::kDoorLocked));
    W<uint32_t>(p + sym::kOffDoorLock, static_cast<uint32_t>(difficulty));
  } else {
    const uint32_t f = R<uint32_t>(p + sym::kOffContainerFlags);
    W<uint32_t>(p + sym::kOffContainerFlags, locked ? (f | sym::kContainerLocked) : (f & ~sym::kContainerLocked));
    W<uint16_t>(p + sym::kOffContainerLock, static_cast<uint16_t>(difficulty));
  }
  return true;
}

}  // namespace pstf::engine
