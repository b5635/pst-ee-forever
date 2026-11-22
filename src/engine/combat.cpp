// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/combat.h"

#include <cstdio>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

#include <windows.h>

#include "core/log.h"
#include "core/patch.h"
#include "engine/sprite_ptr.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {

template <class T>
T R(uint64_t va) {
  return *reinterpret_cast<const volatile T*>(va);
}
template <class T>
void W(uint64_t va, T v) {
  *reinterpret_cast<volatile T*>(va) = v;
}

const sym::Symbol* Find(uint32_t rva) {
  for (const auto& s : sym::kAll)
    if (s.rva == rva) return &s;
  for (const auto& s : sym::kData)
    if (s.rva == rva) return &s;
  return nullptr;
}
bool AllOk(std::initializer_list<uint32_t> rvas) {
  for (uint32_t r : rvas) {
    const sym::Symbol* s = Find(r);
    if (!s || !CheckSymbol(*s)) return false;
  }
  return true;
}

// Derived stats in use (sprite +0x4BBC selects the temporary set during effect evaluation).
uint64_t Stats(uint64_t p) {
  return p + (R<int32_t>(p + sym::kOffSpriteStatsSel) ? sym::kOffSpriteStatsA : sym::kOffSpriteStatsB);
}

// --- round clock ---------------------------------------------------------------------------------
FrozenFn g_frozen = nullptr;
OnsetFn g_onset = nullptr;
std::unordered_map<uint64_t, int> g_onsets;  // delayed effect -> OnsetFn kind (seen once)
bool g_clock = false;

extern "C" bool __cdecl PstfPersistFrozen(void* sprite) {
  if (!g_frozen || !sprite) return false;
  return g_frozen(R<int32_t>(reinterpret_cast<uint64_t>(sprite) + sym::kOffSpriteId));
}

bool ClockSymbolsOk() {
  static const int ok = AllOk({sym::kRvaProcessEffectList, sym::kRvaAiTickRead, sym::kRvaPersistGate, sym::kRvaHandlePersist, sym::kRvaRegenStamp,
                               sym::kRvaEffListTimed, sym::kRvaEffListEquip, sym::kRvaForceEffects,
                               sym::kRvaEffTypeStore})
                            ? 1
                            : 0;
  return ok != 0;
}

bool ClockedType(uint32_t t) { return t == 4096 || t == 6 || t == 7 || t == 8; }

template <class Fn>
void ForEachEffect(uint64_t sprite, Fn fn) {
  for (uint32_t off : {sym::kOffSpriteEffTimed, sym::kOffSpriteEffEquip}) {
    uint64_t node = R<uint64_t>(sprite + off + 8);
    for (int guard = 0; node && guard < 4096; ++guard) {
      if (!PlausiblePtr(node)) break;
      const uint64_t eff = R<uint64_t>(node + 0x10);
      if (PlausiblePtr(eff)) fn(eff);
      node = R<uint64_t>(node);
    }
  }
}

}  // namespace

bool InstallEffectClock(FrozenFn frozen) {
  if (g_clock) {
    g_frozen = frozen;
    return true;
  }
  if (!ClockSymbolsOk()) {
    Log("combat: effect clock symbols differ; effects keep real time");
    return false;
  }
  // Stub called in place of `cmp byte [rcx+7824h],0`; leaves ZF=1 (skip) when the timer is stopped
  // or the sprite (rsi) is frozen. Saves the volatile registers the C call may change.
  uint8_t code[] = {
      0x80, 0xB9, 0x24, 0x78, 0x00, 0x00, 0x00,  // cmp byte [rcx+7824h], 0
      0x74, 0x2F,                                // je done
      0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,  // push rax..r11
      0x48, 0x89, 0xF1,                          // mov rcx, rsi
      0x48, 0x83, 0xEC, 0x20,                    // sub rsp, 20h
      0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,        // mov rax, PstfPersistFrozen
      0xFF, 0xD0,                                // call rax
      0x48, 0x83, 0xC4, 0x20,                    // add rsp, 20h
      0x3C, 0x01,                                // cmp al, 1  (ZF=1: frozen)
      0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58,  // pop r11..rax
      0xC3,                                      // done: ret
  };
  static_assert(sizeof(code) == 57, "stub layout");
  const uint64_t fn = reinterpret_cast<uint64_t>(&PstfPersistFrozen);
  std::memcpy(code + 29, &fn, 8);
  uint8_t* stub = AllocCode(sizeof code);
  if (!stub) return false;
  std::memcpy(stub, code, sizeof code);
  const uint64_t site = sym::kImageBase + sym::kRvaPersistGate;
  const int64_t rel = static_cast<int64_t>(reinterpret_cast<uint64_t>(stub)) - static_cast<int64_t>(site + 5);
  if (rel < INT32_MIN || rel > INT32_MAX) return false;
  uint8_t insn[7] = {0xE8, 0, 0, 0, 0, 0x66, 0x90};
  const int32_t r32 = static_cast<int32_t>(rel);
  std::memcpy(insn + 1, &r32, 4);
  g_frozen = frozen;
  if (!PatchCode(*Find(sym::kRvaPersistGate), insn, sizeof insn)) return false;
  g_clock = true;
  Log("combat: effect round clock installed at %llX", static_cast<unsigned long long>(site));
  return true;
}

bool EffectClockInstalled() { return g_clock; }

void HeldEffectsTick(void* sprite) {
  if (!g_clock || !sprite) return;
  const uint64_t p = reinterpret_cast<uint64_t>(sprite);
  W<int32_t>(p + sym::kOffSpriteAiTick, R<int32_t>(p + sym::kOffSpriteAiTick) + 1);
  using PelFn = int64_t(__fastcall*)(uint64_t, int);
  reinterpret_cast<PelFn>(Va(sym::kRvaProcessEffectList))(p, 1);
}

void SetOnsetRule(OnsetFn resolves_now) { g_onset = resolves_now; }
void ForgetOnsets() { g_onsets.clear(); }

int ResolvingOnsets(int32_t id) {
  const uint64_t p = g_clock ? LookupSprite(id) : 0;
  if (!p) return 0;
  int n = 0;
  ForEachEffect(p, [&](uint64_t eff) {
    const uint32_t type = R<uint32_t>(eff + sym::kOffEffType);
    if (type < 6 || type > 8) return;
    const auto it = g_onsets.find(eff);
    if (it != g_onsets.end() && it->second == 1) ++n;
  });
  return n;
}

void ShiftEffectClock(int32_t id, int32_t ticks) {
  const uint64_t p = g_clock && ticks ? LookupSprite(id) : 0;
  if (!p) return;
  const uint64_t g = GameObject();
  const uint32_t now = g ? R<uint32_t>(g + 0x7820) : 0;
  ForEachEffect(p, [&](uint64_t eff) {
    const uint32_t type = R<uint32_t>(eff + sym::kOffEffType);
    if (!ClockedType(type)) return;
    const uint32_t t = R<uint32_t>(eff + sym::kOffEffTick);
    if (type >= 6 && type <= 8 && g_onset && ticks > 0) {
      auto it = g_onsets.find(eff);
      if (it == g_onsets.end()) {
        const int op = R<int32_t>(eff + sym::kOffEffOpcode);
        const int kind = g_onset(static_cast<int32_t>(t - now), op);
        it = g_onsets.emplace(eff, kind).first;
        if (kind) Log("combat: %X: onset in %d ticks %s (opcode %d)", static_cast<unsigned>(id),
                      static_cast<int32_t>(t - now), kind == 1 ? "resolves now" : "plays now (presentation)", op);
      }
      if (it->second) {  // the spell resolving: world time, and its list is checked every tick
        W<int32_t>(p + sym::kOffSpriteForceEffects, 1);
        return;
      }
    }
    if (t >= 0xF0000000u) return;  // "forever" style values stay
    W<uint32_t>(eff + sym::kOffEffTick, t + static_cast<uint32_t>(ticks));
  });
  W<uint32_t>(p + sym::kOffSpriteRegenStamp, R<uint32_t>(p + sym::kOffSpriteRegenStamp) + static_cast<uint32_t>(ticks));
}

void StepEffectClock(int32_t id, uint32_t ticks) {
  const uint64_t p = g_clock ? LookupSprite(id) : 0;
  if (!p) return;
  const uint64_t g = GameObject();
  const uint32_t now = g ? R<uint32_t>(g + 0x7820) : 0;
  const uint32_t stamp0 = R<uint32_t>(p + sym::kOffSpriteRegenStamp);
  ShiftEffectClock(id, -static_cast<int32_t>(ticks));
  // Less than a round but more than nothing left is a whole round (the next turn start ends it):
  // never a tick or two that world time could use up during someone else's turn.
  ForEachEffect(p, [&](uint64_t eff) {
    if (!ClockedType(R<uint32_t>(eff + sym::kOffEffType))) return;
    const int32_t left = static_cast<int32_t>(R<uint32_t>(eff + sym::kOffEffTick) - now);
    if (left > 0 && left < 4) W<uint32_t>(eff + sym::kOffEffTick, now + 4);
  });
  Log("combat: step %X: now %u regen stamp %u -> %u (elapsed %d), flags %X con %d hp %d", static_cast<unsigned>(id), now,
      stamp0, R<uint32_t>(p + sym::kOffSpriteRegenStamp),
      static_cast<int>(now - R<uint32_t>(p + sym::kOffSpriteRegenStamp)), R<uint32_t>(p + 0x560), R<int16_t>(p + 4160),
      R<int16_t>(p + sym::kOffSpriteHp));
  using HandleFn = int64_t(__fastcall*)(uint64_t, uint32_t);
  reinterpret_cast<HandleFn>(Va(sym::kRvaHandlePersist))(p, ticks);
  W<int32_t>(p + sym::kOffSpriteForceEffects, 1);
}

void SetNoInterrupt(int32_t id) {
  static const int ok = AllOk({sym::kRvaNoInterrupt}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  if (p) W<uint32_t>(p + sym::kOffSpriteFlags, R<uint32_t>(p + sym::kOffSpriteFlags) | sym::kFlagNoInterrupt);
}

std::vector<EffectView> TimedEffects(int32_t id) {
  std::vector<EffectView> out;
  const uint64_t p = ClockSymbolsOk() ? LookupSprite(id) : 0;
  const uint64_t g = GameObject();
  if (!p || !g) return out;
  const uint32_t now = R<uint32_t>(g + 0x7820);
  ForEachEffect(p, [&](uint64_t eff) {
    EffectView v;
    v.opcode = R<int32_t>(eff + sym::kOffEffOpcode);
    v.type = R<uint32_t>(eff + sym::kOffEffType);
    if (ClockedType(v.type)) v.left = static_cast<int32_t>(R<uint32_t>(eff + sym::kOffEffTick) - now);
    out.push_back(v);
  });
  return out;
}

int PersistentCount(int32_t id) {
  const uint64_t p = ClockSymbolsOk() ? LookupSprite(id) : 0;
  if (!p) return 0;
  int n = 0;
  uint64_t node = R<uint64_t>(p + 0x4770);
  for (; node && n < 4096 && PlausiblePtr(node); node = R<uint64_t>(node)) ++n;
  uint64_t node2 = R<uint64_t>(p + 0x18D8 + 8);
  for (; node2 && n < 8192 && PlausiblePtr(node2); node2 = R<uint64_t>(node2)) ++n;
  return n;
}

// --- projectiles held with their caster ------------------------------------------------------------
namespace {
ProjGateFn g_proj_gate = nullptr;
using ObjFn = void(__fastcall*)(void*);
std::map<uint64_t, ObjFn> g_proj_orig;  // vtable -> original AIUpdate

bool InExeSection(uint64_t va, bool code) {
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(sym::kImageBase);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(sym::kImageBase + dos->e_lfanew);
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    const uint64_t lo = sym::kImageBase + sec->VirtualAddress, hi = lo + sec->Misc.VirtualSize;
    if (va < lo || va >= hi) continue;
    const bool exec = (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    return code ? exec : !exec;
  }
  return false;
}

bool LingerOk() {
  static const int ok =
      AllOk({sym::kRvaAreaAI, sym::kRvaSwarmAI, sym::kRvaAreaArrived, sym::kRvaSwarmArrived}) ? 1 : 0;
  return ok != 0;
}

void __fastcall HookProjAI(void* obj) {
  const uint64_t o = reinterpret_cast<uint64_t>(obj);
  const auto it = g_proj_orig.find(R<uint64_t>(o));
  if (it == g_proj_orig.end()) return;  // cannot happen: only hooked vtables call here
  if (g_proj_gate) {
    const uint64_t fn = reinterpret_cast<uint64_t>(it->second);
    const bool lingers = (fn == sym::kImageBase + sym::kRvaAreaAI || fn == sym::kImageBase + sym::kRvaSwarmAI) &&
                         LingerOk() && R<int32_t>(o + sym::kOffProjArrived) != 0;
    if (!g_proj_gate(R<int32_t>(o + sym::kOffProjSource), R<int32_t>(o + sym::kOffSpriteId), lingers)) return;
  }
  it->second(obj);
}

bool HookVtable(uint64_t vtbl) {
  if (g_proj_orig.count(vtbl)) return true;
  static const int ok = AllOk({sym::kRvaGetObjectType}) ? 1 : 0;
  if (!ok || !InExeSection(vtbl, false)) return false;
  if (R<uint64_t>(vtbl + 8) != sym::kImageBase + sym::kRvaGetObjectType) return false;
  const uint64_t slot = vtbl + sym::kVtblObjAIUpdate, fn = R<uint64_t>(slot);
  if (!InExeSection(fn, true)) return false;
  DWORD old;
  if (!VirtualProtect(reinterpret_cast<void*>(slot), 8, PAGE_READWRITE, &old)) return false;
  g_proj_orig[vtbl] = reinterpret_cast<ObjFn>(fn);
  *reinterpret_cast<uint64_t*>(slot) = reinterpret_cast<uint64_t>(&HookProjAI);
  VirtualProtect(reinterpret_cast<void*>(slot), 8, old, &old);
  Log("combat: projectile vtable %llX hooked (AIUpdate %llX)", static_cast<unsigned long long>(vtbl),
      static_cast<unsigned long long>(fn));
  return true;
}
}  // namespace

void SetProjectileGate(ProjGateFn run) { g_proj_gate = run; }

bool ProjectileLingers(int32_t projectile) {
  const uint64_t p = LookupObject(projectile);
  if (!p || R<uint8_t>(p + 8) != 0 || !LingerOk()) return false;
  const auto it = g_proj_orig.find(R<uint64_t>(p));
  if (it == g_proj_orig.end()) return false;
  const uint64_t fn = reinterpret_cast<uint64_t>(it->second);
  return (fn == sym::kImageBase + sym::kRvaAreaAI || fn == sym::kImageBase + sym::kRvaSwarmAI) &&
         R<int32_t>(p + sym::kOffProjArrived) != 0;
}

void WatchProjectiles() {
  if (!g_proj_gate) return;
  const int max = R<int16_t>(sym::kVaObjMax);
  for (int i = 0; i <= max; ++i) {
    const uint64_t e = sym::kVaObjArray + 16ull * i;
    const uint64_t p = R<uint64_t>(e + 8);
    if (!PlausiblePtr(p) || R<uint8_t>(p + 8) != 0) continue;
    if (R<int32_t>(p + sym::kOffSpriteId) != ((i << 16) | R<uint16_t>(e))) continue;
    if (R<int32_t>(p + sym::kOffProjSource) == -1) continue;
    HookVtable(R<uint64_t>(p));
  }
}

// --- pacing --------------------------------------------------------------------------------------
namespace {
bool RunOk() {
  static const int ok = AllOk({sym::kRvaSetRunning, sym::kRvaRunScale, sym::kRvaSlotSetRunning}) ? 1 : 0;
  return ok != 0;
}
uint64_t AnimOf(uint64_t p) {
  const uint64_t a = R<uint64_t>(p + sym::kOffSpriteAnim);
  return PlausiblePtr(a) ? a : 0;
}
}  // namespace

void ForceRun(int32_t id, bool run, uint8_t run_scale_other) {
  const uint64_t p = RunOk() ? LookupSprite(id) : 0;
  const uint64_t a = p ? AnimOf(p) : 0;
  if (!a) return;
  if (R<uint64_t>(a) == sym::kVaAnimPstVtable) {
    const bool running = R<int32_t>(a + sym::kOffAnimRunFlag) == 1;
    // The run scale may be 0 for animations without a run sequence: then the walk scale doubles.
    const double rs = R<double>(a + sym::kOffAnimRunScale), ws = R<double>(a + sym::kOffAnimWalkScale);
    using SetRunningFn = int64_t(__fastcall*)(uint64_t, int, int);
    const auto set_running = reinterpret_cast<SetRunningFn>(R<uint64_t>(R<uint64_t>(a) + sym::kVtblAnimSetRunning));
    if (rs >= 1.0) {
      if (run != running) set_running(a, run ? 1 : 0, 0);
      return;
    }
    // No run sequence: walk frames, run speed.
    if (run && ws >= 1.0 && R<uint8_t>(a + sym::kOffAnimMoveScale) < run_scale_other)
      W<uint8_t>(a + sym::kOffAnimMoveScale, run_scale_other);
    else if (!run && R<uint8_t>(a + sym::kOffAnimMoveScale) == run_scale_other)
      W<uint8_t>(a + sym::kOffAnimMoveScale, static_cast<uint8_t>(ws + 0.5));
    return;
  }
  // Other animations have no run sequence: keep the walk frames at the run speed.
  const uint8_t def = R<uint8_t>(a + sym::kOffAnimMoveScale + 1);
  if (run) {
    if (R<uint8_t>(a + sym::kOffAnimMoveScale) < run_scale_other) W<uint8_t>(a + sym::kOffAnimMoveScale, run_scale_other);
  } else if (R<uint8_t>(a + sym::kOffAnimMoveScale) == run_scale_other && def) {
    W<uint8_t>(a + sym::kOffAnimMoveScale, def);
  }
}

uint8_t MoveScale(int32_t id) {
  const uint64_t p = RunOk() ? LookupSprite(id) : 0;
  const uint64_t a = p ? AnimOf(p) : 0;
  return a ? R<uint8_t>(a + sym::kOffAnimMoveScale) : 0;
}

void KeepBattleMusic(int32_t id, int ticks) {
  static const int ok = AllOk({sym::kRvaBattleCounter}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  if (!p) return;
  const uint64_t area = R<uint64_t>(p + sym::kOffSpriteArea);
  if (!PlausiblePtr(area)) return;
  if (R<int32_t>(area + sym::kOffAreaBattleCounter) < ticks) W<int32_t>(area + sym::kOffAreaBattleCounter, ticks);
}

namespace {
uint64_t ViewArea() {
  const uint64_t g = GameObject();
  if (!g) return 0;
  const uint64_t a = R<uint64_t>(g + sym::kOffGameAreas + 8ull * R<uint8_t>(g + sym::kOffGameVisibleIdx));
  return PlausiblePtr(a) ? a : 0;
}
bool CameraOk() {
  static const int ok = AllOk({sym::kRvaSetViewPos, sym::kRvaCenterLeaderJmp, sym::kRvaVisibleArea}) ? 1 : 0;
  return ok != 0;
}
}  // namespace

namespace {
uint64_t g_reveal_area = 0;  // compare only
std::vector<int32_t> g_revealed;
}  // namespace

void RevealCombatants(int32_t anchor, const std::vector<int32_t>& ids, int radius) {
  static const int ok = AllOk({sym::kRvaVisMapLea, sym::kRvaVisMasks}) ? 1 : 0;
  if (!ok) return;
  // The area the party stands in now; marks from another area are forgotten, never written (that
  // area may be gone).
  const uint64_t ap = LookupSprite(anchor);
  const uint64_t area = ap ? R<uint64_t>(ap + sym::kOffSpriteArea) : 0;
  if (area != g_reveal_area) g_revealed.clear();
  g_reveal_area = area;
  if (!area || !PlausiblePtr(area)) return;
  const uint64_t tiles = R<uint64_t>(area + sym::kOffAreaVisMap);
  const int w = R<int16_t>(area + sym::kOffAreaVisMap + 8);
  const int h = (R<int32_t>(area + sym::kOffAreaHeightPx) + sym::kVisTile - 1) / sym::kVisTile;
  if (!PlausiblePtr(tiles) || w <= 0 || h <= 0 || w > 4096 || h > 4096) return;
  std::vector<int32_t> now;
  const int r = radius / sym::kVisTile;
  for (int32_t id : ids) {
    const uint64_t p = LookupSprite(id);
    if (!p || R<uint64_t>(p + sym::kOffSpriteArea) != area) continue;
    const int tx = R<int32_t>(p + sym::kOffSpritePos) / sym::kVisTile;
    const int ty = R<int32_t>(p + sym::kOffSpritePos + 4) / sym::kVisTile;
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        const int x = tx + dx, y = ty + dy;
        if (dx * dx + dy * dy > r * r + r || x < 0 || y < 0 || x >= w || y >= h) continue;
        now.push_back(y * w + x);
      }
  }
  std::sort(now.begin(), now.end());
  now.erase(std::unique(now.begin(), now.end()), now.end());
  for (int32_t t : g_revealed)
    if (!std::binary_search(now.begin(), now.end(), t))
      W<uint16_t>(tiles + 2ull * t, static_cast<uint16_t>(R<uint16_t>(tiles + 2ull * t) & ~sym::kVisCombat));
  for (int32_t t : now)
    W<uint16_t>(tiles + 2ull * t, static_cast<uint16_t>(R<uint16_t>(tiles + 2ull * t) | sym::kVisCombat | sym::kVisExplored));
  g_revealed.swap(now);
}

namespace {
using UseItemFn = int64_t(__fastcall*)(void*, void*);
using UseItemPointFn = int64_t(__fastcall*)(void*);
UseItemFn g_use_item = nullptr;
UseItemPointFn g_use_item_point = nullptr;
std::vector<int32_t> g_item_users;
void NoteItemUser(void* sprite) {
  const int32_t id = sprite ? R<int32_t>(reinterpret_cast<uint64_t>(sprite) + sym::kOffSpriteId) : -1;
  if (id != -1 && g_item_users.size() < 64 &&
      std::find(g_item_users.begin(), g_item_users.end(), id) == g_item_users.end())
    g_item_users.push_back(id);
}
int64_t __fastcall HookUseItem(void* self, void* target) {
  NoteItemUser(self);
  return g_use_item(self, target);
}
int64_t __fastcall HookUseItemPoint(void* self) {
  NoteItemUser(self);
  return g_use_item_point(self);
}
}  // namespace

bool InstallItemUseHooks() {
  if (g_use_item) return true;
  if (!AllOk({sym::kRvaUseItem, sym::kRvaUseItemPoint})) return false;
  const uint64_t a = InlineHook(*Find(sym::kRvaUseItem), 5, reinterpret_cast<void*>(&HookUseItem));
  if (!a) return false;
  g_use_item = reinterpret_cast<UseItemFn>(a);
  const uint64_t b = InlineHook(*Find(sym::kRvaUseItemPoint), 5, reinterpret_cast<void*>(&HookUseItemPoint));
  if (!b) return false;
  g_use_item_point = reinterpret_cast<UseItemPointFn>(b);
  Log("combat: item use hooks installed");
  return true;
}

bool TookItemAction(int32_t id) {
  const auto it = std::find(g_item_users.begin(), g_item_users.end(), id);
  const bool took = it != g_item_users.end();
  g_item_users.clear();  // one frame's worth: only the active creature is asked
  return took;
}

int GameCursorState() {
  static const int ok = AllOk({sym::kRvaGameStateStore}) ? 1 : 0;
  const uint64_t g = ok ? GameObject() : 0;
  return g ? R<int16_t>(g + sym::kOffGameState) : 0;
}

bool ViewCenter(int* x, int* y) {
  const uint64_t area = CameraOk() ? ViewArea() : 0;
  if (!area) return false;
  const uint64_t inf = area + sym::kOffAreaInfinity;
  const int w = R<int32_t>(inf + sym::kOffInfRect + 8) - R<int32_t>(inf + sym::kOffInfRect);
  const int h = R<int32_t>(inf + sym::kOffInfRect + 12) - R<int32_t>(inf + sym::kOffInfRect + 4);
  *x = R<int32_t>(inf + sym::kOffInfView) + w / 2;
  *y = R<int32_t>(inf + sym::kOffInfView + 4) + h / 2;
  return true;
}

void CenterView(int x, int y) {
  const uint64_t area = CameraOk() ? ViewArea() : 0;
  if (!area) return;
  const uint64_t inf = area + sym::kOffAreaInfinity;
  const int w = R<int32_t>(inf + sym::kOffInfRect + 8) - R<int32_t>(inf + sym::kOffInfRect);
  const int h = R<int32_t>(inf + sym::kOffInfRect + 12) - R<int32_t>(inf + sym::kOffInfRect + 4);
  using SetViewFn = int64_t(__fastcall*)(uint64_t, int, int, uint8_t);
  reinterpret_cast<SetViewFn>(Va(sym::kRvaSetViewPos))(inf, x - w / 2, y - h / 2, 1);
}

// --- rolls ---------------------------------------------------------------------------------------
namespace {
using HitFn = uint8_t(__fastcall*)(void*, void*, void*, int, int*, int);
using FeedBackFn = void(__fastcall*)(void*, uint32_t, int, int, uint32_t, int, int, void*);
using CheckSaveFn = int64_t(__fastcall*)(void*, void*, uint8_t*, uint8_t*, uint8_t*, uint8_t*, uint8_t*, uint8_t*);
HitFn g_hit = nullptr;
FeedBackFn g_feedback = nullptr;
CheckSaveFn g_check_save = nullptr;
int32_t g_hit_target = -1;
std::vector<RollEvent> g_rolls;

int32_t IdOf(void* sprite) {
  return sprite ? R<int32_t>(reinterpret_cast<uint64_t>(sprite) + sym::kOffSpriteId) : -1;
}

uint8_t __fastcall HookHit(void* self, void* target, void* item, int ability, int* crit, int a6) {
  const int32_t prev = g_hit_target;
  g_hit_target = IdOf(target);
  const uint8_t r = g_hit(self, target, item, ability, crit, a6);
  g_hit_target = prev;
  return r;
}

void __fastcall HookFeedBack4(void* self, uint32_t type, int total, int hit, uint32_t roll, int a6, int a7, void* str) {
  if (type == 4 && g_rolls.size() < 64) {
    RollEvent e;
    e.kind = RollEvent::Attack;
    e.actor = IdOf(self);
    e.target = g_hit_target;
    e.roll = static_cast<int>(roll);
    e.total = static_cast<int16_t>(total);
    e.success = hit != 0;
    CombatStats a, t;
    if (GetCombatStats(e.actor, &a) && GetCombatStats(e.target, &t)) e.needs = a.thac0 - t.ac;
    g_rolls.push_back(e);
  }
  g_feedback(self, type, total, hit, roll, a6, a7, str);
}

int64_t __fastcall HookCheckSave(void* eff, void* sprite, uint8_t* poly, uint8_t* wand, uint8_t* death, uint8_t* breath,
                                 uint8_t* spell, uint8_t* resist) {
  const uint32_t flags = eff ? R<uint32_t>(reinterpret_cast<uint64_t>(eff) + sym::kOffEffSaveFlags) & 0x1F : 0;
  uint8_t* by_bit[5] = {spell, breath, death, wand, poly};
  uint8_t before[5] = {};
  for (int i = 0; i < 5; ++i) before[i] = by_bit[i] ? *by_bit[i] : 0;
  const int64_t r = g_check_save(eff, sprite, poly, wand, death, breath, spell, resist);
  if (flags && g_rolls.size() < 64) {
    RollEvent e;
    e.kind = RollEvent::Save;
    e.actor = IdOf(sprite);
    e.success = r == 0;
    int bit = -1;
    for (int i = 0; i < 5 && bit < 0; ++i)
      if (by_bit[i] && (*by_bit[i] & 0x80) && !(before[i] & 0x80)) bit = i;  // the save that succeeded
    for (int i = 0; i < 5 && bit < 0; ++i)
      if (flags & (1u << i)) bit = i;
    e.save_type = bit;
    e.roll = bit >= 0 && by_bit[bit] ? (*by_bit[bit] & 0x7F) : 0;
    e.total = e.roll + static_cast<int32_t>(R<int32_t>(reinterpret_cast<uint64_t>(eff) + sym::kOffEffSaveBonus));
    g_rolls.push_back(e);
  }
  return r;
}
FeedBackFn g_feedback_all = nullptr;
void __fastcall HookFeedBackAll(void* self, uint32_t type, int a, int b, uint32_t c, int d, int e, void* str) {
  Log("combat: feedback %u (%d, %d, %u) from %X", type, a, b, c, static_cast<unsigned>(IdOf(self)));
  g_feedback_all(self, type, a, b, c, d, e, str);
}
}  // namespace

bool InstallFeedbackLog() {
  if (g_feedback_all) return true;
  const sym::Symbol* s = Find(sym::kRvaFeedBack);
  if (!s) return false;
  const uint64_t t = InlineHook(*s, 5, reinterpret_cast<void*>(&HookFeedBackAll));
  if (!t) return false;
  g_feedback_all = reinterpret_cast<FeedBackFn>(t);
  Log("combat: feedback log on");
  return true;
}

bool InstallRollHooks() {
  if (g_feedback) return true;
  if (!AllOk({sym::kRvaHitFeedbackCall, sym::kRvaSwingHit1, sym::kRvaSwingHit2, sym::kRvaSwingHit3,
              sym::kRvaThac0Ac, sym::kRvaCheckSave})) {
    Log("combat: roll hook sites differ; no dice");
    return false;
  }
  const uint64_t fb = RedirectCall(*Find(sym::kRvaHitFeedbackCall), reinterpret_cast<void*>(&HookFeedBack4));
  if (fb != sym::kVaFeedBack) return false;
  g_feedback = reinterpret_cast<FeedBackFn>(fb);
  for (uint32_t site : {sym::kRvaSwingHit1, sym::kRvaSwingHit2, sym::kRvaSwingHit3}) {
    const uint64_t h = RedirectCall(*Find(site), reinterpret_cast<void*>(&HookHit));
    if (h != sym::kVaHit) return false;
    g_hit = reinterpret_cast<HitFn>(h);
  }
  // CheckSave prologue: push rbp / push rbx / push rsi / push rdi / push r12 = 7 bytes.
  const uint64_t cs = InlineHook(*Find(sym::kRvaCheckSave), 7, reinterpret_cast<void*>(&HookCheckSave));
  if (!cs) return false;
  g_check_save = reinterpret_cast<CheckSaveFn>(cs);
  Log("combat: roll hooks installed (Hit, FeedBack 4, CheckSave)");
  return true;
}

std::vector<RollEvent> TakeRollEvents() {
  std::vector<RollEvent> out;
  out.swap(g_rolls);
  return out;
}

bool SetHpForTest(int32_t id, int hp) {
  const uint64_t p = LookupSprite(id);
  if (!p || hp < 1 || hp > 30000) return false;
  W<int16_t>(p + sym::kOffSpriteHp, static_cast<int16_t>(hp));
  return true;
}

std::string ScanBarStrings() {
  const uint64_t g = GameObject();
  if (!g) return "";
  const uint64_t a = g + sym::kOffGameButtonArray;
  std::string out;
  for (uint32_t off = 0; off + 8 <= 0x2400; ++off) {
    int n = 0;
    while (n < 8) {
      const char c = R<char>(a + off + n);
      if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || (c >= 'a' && c <= 'z'))) break;
      ++n;
    }
    if (n >= 5 && (off == 0 || !isalnum(static_cast<unsigned char>(R<char>(a + off - 1))))) {
      char b[40];
      std::snprintf(b, sizeof b, "+%X:", off);
      out += b;
      for (int k = 0; k < n; ++k) out += R<char>(a + off + k);
      out += ' ';
      off += n;
    }
  }
  return out;
}

std::string ActiveFlags(int32_t id) {
  const uint64_t p = LookupSprite(id);
  if (!p) return "";
  char b[64];
  std::snprintf(b, sizeof b, "%d %d %d %d", R<int32_t>(p + 0x3904), R<int32_t>(p + 0x3908), R<int32_t>(p + 0x390C),
                R<int32_t>(p + 0x3910));
  return b;
}

bool GetCombatStats(int32_t id, CombatStats* out) {
  static const int ok = AllOk({sym::kRvaThac0Ac, sym::kRvaMaxHp, sym::kRvaHp}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  if (!p) return false;
  const uint64_t s = Stats(p);
  out->thac0 = R<int16_t>(s + sym::kOffStatThac0);
  out->ac = R<int16_t>(s + sym::kOffStatAc);
  out->max_hp = R<int16_t>(s + 4);
  out->hp = R<int16_t>(p + sym::kOffSpriteHp);
  return true;
}

std::vector<BarButton> ActionBarButtons() {
  std::vector<BarButton> out;
  static const int ok = AllOk({sym::kRvaButtonArray, sym::kRvaBarIcon, sym::kRvaBarDisabled}) ? 1 : 0;
  const uint64_t g = ok ? GameObject() : 0;
  if (!g) return out;
  const uint64_t arr = g + sym::kOffGameButtonArray;
  for (int i = 0; i < 12; ++i) {
    const uint64_t b = arr + static_cast<uint64_t>(sym::kButtonDataSize) * i;
    BarButton bb;
    char ref[9] = {};
    for (int k = 0; k < 8; ++k) {
      const char c = R<char>(b + sym::kOffBarIcon + k);
      if (!c) break;
      if (c < 33 || c > 126) {
        ref[0] = 0;  // not a resref
        break;
      }
      ref[k] = c;
    }
    bb.icon = ref;
    bb.disabled = R<int32_t>(b + sym::kOffBarDisabled) != 0;
    out.push_back(bb);
  }
  return out;
}

std::string ActionBarHex(int slot, int bytes) {
  static const int ok = AllOk({sym::kRvaButtonArray}) ? 1 : 0;
  const uint64_t g = ok ? GameObject() : 0;
  if (!g || slot < 0 || slot > 11 || bytes > 0x2A0) return "";
  const uint64_t b = g + sym::kOffGameButtonArray + static_cast<uint64_t>(sym::kButtonDataSize) * slot;
  std::string s;
  char t[4];
  for (int i = 0; i < bytes; ++i) {
    std::snprintf(t, sizeof t, "%02X", R<uint8_t>(b + i));
    s += t;
    if (i % 8 == 7) s += ' ';
  }
  return s;
}

}  // namespace pstf::engine
