// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/combat.h"

#include <cstdio>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <intrin.h>

#include "core/log.h"
#include "core/patch.h"
#include "engine/sprite_ptr.h"
#include "engine/symbols.h"
#include "modules/tbc/round_rules.h"

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
OnsetSyncFn g_onset_sync = nullptr;
// Delayed effect -> its OnsetFn kind (classified once). Keyed by the effect's address, so an entry lives only while
// that effect is still pending on its owner: the engine frees effects and reuses their memory, and a stale entry gave
// a new effect the old one's kind (Submerge the Will's 29-tick shield round-clocked to its caster's next turn).
tbc::OnsetBook g_onsets;  // round_rules.h
bool g_clock = false;
ResistFn g_resist_fn = nullptr;  // statusfx: a spell resisted by magic resistance

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
void SetOnsetSync(OnsetSyncFn sync) { g_onset_sync = sync; }
void ForgetOnsets() { g_onsets.Clear(); }

int ResolvingOnsets(int32_t id) {
  const uint64_t p = g_clock ? LookupSprite(id) : 0;
  if (!p) return 0;
  int n = 0;
  ForEachEffect(p, [&](uint64_t eff) {
    const uint32_t type = R<uint32_t>(eff + sym::kOffEffType);
    if (type < 6 || type > 8) return;
    if (g_onsets.Kind(eff) == 1) ++n;
  });
  return n;
}

void ShiftEffectClock(int32_t id, int32_t ticks) {
  const uint64_t p = g_clock && ticks ? LookupSprite(id) : 0;
  if (!p) return;
  const uint64_t g = GameObject();
  const uint32_t now = g ? R<uint32_t>(g + 0x7820) : 0;
  if (g_onset && ticks > 0) {  // classify the delayed effects new since the last pass, together
    std::vector<uint64_t> effs;
    std::vector<int32_t> lefts;
    std::vector<int> ops, kinds;
    std::vector<uint64_t> srcs;
    std::vector<uint64_t> pending;  // this creature's delayed effects now
    ForEachEffect(p, [&](uint64_t eff) {
      const uint32_t type = R<uint32_t>(eff + sym::kOffEffType);
      if (!ClockedType(type) || type < 6 || type > 8) return;
      pending.push_back(eff);
      const int op = R<int32_t>(eff + sym::kOffEffOpcode);
      const uint64_t src = R<uint64_t>(eff + sym::kOffEffParentRes);
      if (g_onsets.Known(eff, id, op, src)) return;
      const int32_t left = static_cast<int32_t>(R<uint32_t>(eff + sym::kOffEffTick) - now);
      effs.push_back(eff), lefts.push_back(left), ops.push_back(op), kinds.push_back(g_onset(left, op));
      srcs.push_back(src);
    });
    // entries of this creature whose effect is no longer pending (it ran, or was removed) go
    g_onsets.Prune(id, pending.data(), static_cast<int>(pending.size()));
    const int n = static_cast<int>(effs.size());
    std::vector<int> before = kinds;
    if (n && g_onset_sync) g_onset_sync(lefts.data(), ops.data(), srcs.data(), kinds.data(), n);
    for (int i = 0; i < n; ++i) {
      g_onsets.Add(effs[i], kinds[i], id, ops[i], srcs[i]);
      if (kinds[i])
        Log("combat: %X: onset in %d ticks %s (opcode %d)%s", static_cast<unsigned>(id), lefts[i],
            kinds[i] == 1 ? "resolves now" : "plays now (presentation)", ops[i],
            kinds[i] != before[i] ? ", with its spell's visuals" : "");
    }
  }
  ForEachEffect(p, [&](uint64_t eff) {
    const uint32_t type = R<uint32_t>(eff + sym::kOffEffType);
    if (!ClockedType(type)) return;
    const uint32_t t = R<uint32_t>(eff + sym::kOffEffTick);
    if (type >= 6 && type <= 8 && g_onset && ticks > 0) {
      if (g_onsets.Kind(eff) > 0) {  // the spell resolving: world time, and its list is checked every tick
        W<int32_t>(p + sym::kOffSpriteForceEffects, 1);
        return;
      }
    }
    if (t >= 0xF0000000u) return;  // "forever" style values stay
    W<uint32_t>(eff + sym::kOffEffTick, t + static_cast<uint32_t>(ticks));
  });
  W<uint32_t>(p + sym::kOffSpriteRegenStamp, R<uint32_t>(p + sym::kOffSpriteRegenStamp) + static_cast<uint32_t>(ticks));
  // PST attached spell objects (Submerge the Will, shields) end on their own world-time stamp.
  static const int att_ok = AllOk({sym::kRvaAttachedCount, sym::kRvaAttachedList, sym::kRvaAttachedEnd}) ? 1 : 0;
  if (att_ok) {
    const int n = std::min<int>(R<uint8_t>(p + sym::kOffSpriteAttachedCount), sym::kMaxAttached);
    for (int i = 0; i < n; ++i) {
      const uint64_t o = R<uint64_t>(p + sym::kOffSpriteAttached + 8ull * i);
      if (!PlausiblePtr(o)) continue;
      // its pulse countdown is on the round clock too (round_rules.h PulseAfterShift): frozen with its owner, a round
      // off it at the owner's turn start, so Conflagration burns once per victim round, not every 51 world ticks
      static const int pulse_ok = AllOk({sym::kRvaAttachedKind, sym::kRvaAttachedPulse}) ? 1 : 0;
      const uint8_t kind = R<uint8_t>(o + sym::kOffAttachedKind);
      if (pulse_ok && (kind == 8 || kind == 0xD))
        W<uint16_t>(o + sym::kOffAttachedPulse, tbc::PulseAfterShift(R<uint16_t>(o + sym::kOffAttachedPulse), ticks));
      const uint32_t end = R<uint32_t>(o + sym::kOffAttachedEnd);
      if (end == 0 || end >= 0xF0000000u) continue;
      W<uint32_t>(o + sym::kOffAttachedEnd, end + static_cast<uint32_t>(ticks));
    }
  }
}

int AttachedLeft(int32_t id, std::vector<int32_t>* out) {
  const uint64_t p = LookupSprite(id);
  const uint64_t g = GameObject();
  if (!p || !g || !AllOk({sym::kRvaAttachedCount, sym::kRvaAttachedList, sym::kRvaAttachedEnd})) return 0;
  const uint32_t now = R<uint32_t>(g + 0x7820);
  const int n = std::min<int>(R<uint8_t>(p + sym::kOffSpriteAttachedCount), sym::kMaxAttached);
  for (int i = 0; i < n; ++i) {
    const uint64_t o = R<uint64_t>(p + sym::kOffSpriteAttached + 8ull * i);
    if (!PlausiblePtr(o)) continue;
    const uint32_t end = R<uint32_t>(o + sym::kOffAttachedEnd);
    if (out) out->push_back(end ? static_cast<int32_t>(end - now) : 0);
    if (AllOk({sym::kRvaAttachedKind, sym::kRvaAttachedPulse}))
      Log("combat: %X attached %d: kind %u pulse %u, %d ticks left", static_cast<unsigned>(id), i,
          R<uint8_t>(o + sym::kOffAttachedKind), R<uint16_t>(o + sym::kOffAttachedPulse),
          end ? static_cast<int32_t>(end - now) : 0);
  }
  return n;
}

std::vector<AttachedView> AttachedObjects(int32_t id) {
  std::vector<AttachedView> out;
  const uint64_t p = LookupSprite(id);
  const uint64_t g = GameObject();
  if (!p || !g || !AllOk({sym::kRvaAttachedCount, sym::kRvaAttachedList, sym::kRvaAttachedEnd})) return out;
  const uint32_t now = R<uint32_t>(g + 0x7820);
  const int n = std::min<int>(R<uint8_t>(p + sym::kOffSpriteAttachedCount), sym::kMaxAttached);
  for (int i = 0; i < n; ++i) {
    const uint64_t o = R<uint64_t>(p + sym::kOffSpriteAttached + 8ull * i);
    if (!PlausiblePtr(o)) continue;
    const uint32_t end = R<uint32_t>(o + sym::kOffAttachedEnd);
    const int32_t left = end ? static_cast<int32_t>(end - now) : 0;
    // the key carries its kind: a new object at a freed one's address (Balance in All Things, kind 16, where Infernal
    // Shield's kind 14 was, with the same 100 s) is another effect to the icons (status.h OrphanMap)
    static const int kind_ok = AllOk({sym::kRvaAttachedKind}) ? 1 : 0;
    const uint64_t kind = kind_ok ? R<uint8_t>(o + sym::kOffAttachedKind) : 0;
    if (left > 0) out.push_back({o | (kind << 56), left});
  }
  return out;
}

int UnapplySpell(int32_t id, const char* res) {
  const uint64_t p = LookupSprite(id);
  if (!p || !res || !AllOk({sym::kRvaUnapplySpell})) return -1;
  char ref[8] = {};  // CResRef: 8 bytes, upper case, compared as one qword (effect +0x94)
  for (int i = 0; i < 8 && res[i]; ++i) ref[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(res[i])));
  const size_t before = TimedEffects(id).size();
  reinterpret_cast<void(__fastcall*)(uint64_t, const char*)>(Va(sym::kRvaUnapplySpell))(p, ref);
  const size_t after = TimedEffects(id).size();
  return static_cast<int>(before - after);
}

int EndAttached(int32_t id) {
  const uint64_t p = LookupSprite(id);
  const uint64_t g = GameObject();
  if (!p || !g || !AllOk({sym::kRvaAttachedCount, sym::kRvaAttachedList, sym::kRvaAttachedEnd})) return 0;
  const uint32_t now = R<uint32_t>(g + 0x7820);
  const int n = std::min<int>(R<uint8_t>(p + sym::kOffSpriteAttachedCount), sym::kMaxAttached);
  int ended = 0;
  for (int i = 0; i < n; ++i) {
    const uint64_t o = R<uint64_t>(p + sym::kOffSpriteAttached + 8ull * i);
    if (!PlausiblePtr(o)) continue;
    W<uint32_t>(o + sym::kOffAttachedEnd, now + 1);  // the object ends itself on its stamp
    ++ended;
  }
  return ended;
}

int ExpireOrphans(int32_t id) {
  const uint64_t p = ClockSymbolsOk() ? LookupSprite(id) : 0;
  const uint64_t g = GameObject();
  if (!p || !g) return 0;
  const uint32_t now = R<uint32_t>(g + 0x7820);
  int n = 0;
  ForEachEffect(p, [&](uint64_t eff) {
    if (!ClockedType(R<uint32_t>(eff + sym::kOffEffType)) || R<uint64_t>(eff + sym::kOffEffParentRes) != 0) return;
    if (static_cast<int32_t>(R<uint32_t>(eff + sym::kOffEffTick) - now) <= 1) return;
    W<uint32_t>(eff + sym::kOffEffTick, now + 1);
    ++n;
  });
  if (n) W<int32_t>(p + sym::kOffSpriteForceEffects, 1);
  return n;
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
  // One call per tick, as ProcessEffectList makes them in real time (lin 4E21F0 L1635: HandlePersistantEffects(this,
  // 1) each tick the timer runs): a persistent effect's period counter counts calls, not ticks (Prayer's pulse,
  // CPersistantEffectPrayer::AIUpdate lin 214660: word +32 counts down by one per call, reset to its period 75),
  // so one call with 75 ticks ran its pulses 75 times too slowly. Durations (+40) take the ticks either way.
  using HandleFn = int64_t(__fastcall*)(uint64_t, uint32_t);
  for (uint32_t i = 0; i < ticks; ++i) reinterpret_cast<HandleFn>(Va(sym::kRvaHandlePersist))(p, 1);
  // ... plus one 0-tick call (counters step, no duration passes): a period counter fires on the call AFTER it
  // reaches 0 (CPersistantEffectPrayer::AIUpdate lin 214660 l.42-49: decrement while non-zero, else pulse and reset to
  // 75), so a 5 s pulse is 76 calls apart. With 75 per round the pulse came due at the end of the owner's turn start
  // and fired only if a world tick ran in its turn: the round after Prayer's cast its 6 s curse ran out on the foe
  // (spell_follow.lua, 2026-11-25). Now a 5 s aura pulses once per owner round, at its turn start.
  if (ticks > 0) reinterpret_cast<HandleFn>(Va(sym::kRvaHandlePersist))(p, 0);
  W<int32_t>(p + sym::kOffSpriteForceEffects, 1);
}

void PulsePersistent(int32_t id) {
  const uint64_t p = g_clock ? LookupSprite(id) : 0;
  if (!p) return;
  using HandleFn = int64_t(__fastcall*)(uint64_t, uint32_t);
  reinterpret_cast<HandleFn>(Va(sym::kRvaHandlePersist))(p, 0);  // 0 ticks: no duration passes, counters step once
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

std::vector<ParentEffectView> ParentEffects(int32_t id) {
  std::vector<ParentEffectView> out;
  const uint64_t p = ClockSymbolsOk() ? LookupSprite(id) : 0;
  const uint64_t g = GameObject();
  if (!p || !g) return out;
  const uint32_t now = R<uint32_t>(g + 0x7820);
  ForEachEffect(p, [&](uint64_t eff) {
    ParentEffectView v;
    const uint32_t type = R<uint32_t>(eff + sym::kOffEffType);
    v.timed = type == 4096;  // absolute: running now (delayed types have not started)
    v.delayed = type >= 6 && type <= 8;
    if (!v.timed && !v.delayed) return;
    v.key = eff;
    v.opcode = static_cast<int>(R<uint32_t>(eff + sym::kOffEffOpcode));
    v.left = static_cast<int32_t>(R<uint32_t>(eff + sym::kOffEffTick) - now);
    v.parent_type = R<uint32_t>(eff + sym::kOffEffParentType);
    v.parent_flags = R<uint32_t>(eff + sym::kOffEffParentFlags);
    for (int i = 0; i < 8; ++i) {
      const char c = R<char>(eff + sym::kOffEffParentRes + i);
      if (!c) break;
      v.parent[i] = c;
    }
    out.push_back(v);
  });
  return out;
}

std::vector<ColorEffectView> ColorEffects(int32_t id) {
  std::vector<ColorEffectView> out;
  const uint64_t p = ClockSymbolsOk() ? LookupSprite(id) : 0;
  if (!p) return out;
  ForEachEffect(p, [&](uint64_t eff) {
    const int op = R<int32_t>(eff + sym::kOffEffOpcode);
    if (op != 8 && op != 9 && op != 52 && op != 61 && op != 353) return;
    ColorEffectView v;
    v.opcode = op;
    v.param1 = R<uint32_t>(eff + sym::kOffEffParam1);
    v.param2 = R<uint32_t>(eff + sym::kOffEffParam2);
    v.timing = R<uint32_t>(eff + sym::kOffEffType);
    v.key = eff;
    out.push_back(v);
  });
  return out;
}

// --- status visuals (statusfx) ----------------------------------------------------------------------
std::vector<EffectEntry> EffectEntries(int32_t id) {
  std::vector<EffectEntry> out;
  static const int ok = AllOk({sym::kRvaEffResLea}) ? 1 : 0;
  const uint64_t p = ok && ClockSymbolsOk() ? LookupSprite(id) : 0;
  if (!p) return out;
  for (uint32_t off : {sym::kOffSpriteEffTimed, sym::kOffSpriteEffEquip}) {
    uint64_t node = R<uint64_t>(p + off + 8);
    for (int guard = 0; node && guard < 4096 && PlausiblePtr(node); ++guard) {
      const uint64_t eff = R<uint64_t>(node + 0x10);
      node = R<uint64_t>(node);
      if (!PlausiblePtr(eff)) continue;
      EffectEntry e;
      e.key = eff;
      e.opcode = R<int32_t>(eff + sym::kOffEffOpcode);
      e.type = R<uint32_t>(eff + sym::kOffEffType);
      e.equipped = off == sym::kOffSpriteEffEquip;
      e.param1 = R<uint32_t>(eff + sym::kOffEffParam1);
      for (int i = 0; i < 8; ++i) {
        const char c = R<char>(eff + sym::kOffEffParentRes + i), r = R<char>(eff + sym::kOffEffRes + i);
        e.parent[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        e.res[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(r)));
      }
      out.push_back(e);
    }
  }
  return out;
}

namespace {
uint64_t FindEffect(uint64_t p, uint64_t key) {
  uint64_t found = 0;
  ForEachEffect(p, [&](uint64_t eff) {
    if (eff == key) found = eff;
  });
  return found;
}
}  // namespace

bool ExpireEffectNow(int32_t id, uint64_t key) {
  const uint64_t p = ClockSymbolsOk() ? LookupSprite(id) : 0;
  const uint64_t g = GameObject();
  if (!p || !g || !FindEffect(p, key)) return false;
  if (R<uint32_t>(key + sym::kOffEffType) != 4096) return false;  // running timed effects only (a delayed one would start)
  const uint32_t now = R<uint32_t>(g + 0x7820);
  if (static_cast<int32_t>(R<uint32_t>(key + sym::kOffEffTick) - now) <= 1) return true;
  W<uint32_t>(key + sym::kOffEffTick, now + 1);
  W<int32_t>(p + sym::kOffSpriteForceEffects, 1);
  return true;
}

bool QuietColorEffect(int32_t id, uint64_t key) {
  static const int ok = AllOk({sym::kRvaColorRefresh}) ? 1 : 0;
  const uint64_t p = ok && ClockSymbolsOk() ? LookupSprite(id) : 0;
  if (!p || !FindEffect(p, key)) return false;
  const int op = R<int32_t>(key + sym::kOffEffOpcode);
  // Pulse/glow/light add their colour (type 1/2 entries): 0 adds nothing. Tint multiplies (type 0): white.
  uint32_t neutral;
  if (op == 8 || op == 9 || op == 52) neutral = 0;
  else if (op == 51) neutral = 0xFFFFFF00u;
  else return false;
  if (R<uint32_t>(key + sym::kOffEffParam1) == neutral) return false;
  W<uint32_t>(key + sym::kOffEffParam1, neutral);
  W<int32_t>(p + sym::kOffSpriteColorRefresh, 1);
  W<int32_t>(p + sym::kOffSpriteForceEffects, 1);
  return true;
}

bool SetEffectResource(int32_t id, uint64_t key, const char* res) {
  static const int ok = AllOk({sym::kRvaEffResLea}) ? 1 : 0;
  const uint64_t p = ok && ClockSymbolsOk() ? LookupSprite(id) : 0;
  if (!p || !FindEffect(p, key)) return false;
  for (int i = 0; i < 8; ++i) W<char>(key + sym::kOffEffRes + i, res && *res ? *res++ : 0);
  return true;
}

int SpriteTextHeight(int32_t id) {
  static const int ok = AllOk({sym::kRvaFxRectCall}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  if (!p) return -1;
  const uint64_t anim = R<uint64_t>(p + sym::kOffSpriteAnim);
  if (!PlausiblePtr(anim)) return -1;
  const uint64_t vt = R<uint64_t>(anim);
  if (!PlausiblePtr(vt)) return -1;
  using FxRectFn = void(__fastcall*)(uint64_t anim, int32_t* rect, int32_t* ref, int z);
  const auto fn = reinterpret_cast<FxRectFn>(R<uint64_t>(vt + sym::kSlotCalcFxRect));
  int32_t rect[4] = {}, ref[2] = {};
  const int z = R<int32_t>(p + sym::kOffSpriteZ);
  fn(anim, rect, ref, z);
  const int h = ref[1] + z;
  return h > 0 && h < 1000 ? h : -1;
}

void SetResistListener(ResistFn fn) {
  if (fn && !AllOk({sym::kRvaResistMark})) {
    Log("combat: CheckSave resist mark differs; no resist events");
    return;
  }
  g_resist_fn = fn;
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

// A landed cloud/swarm (its "arrived" flag), or a globe (always): it pulses where it is instead of flying.
bool LingeringFn(uint64_t fn, uint64_t o) {
  if (!LingerOk()) return false;
  if (fn == sym::kImageBase + sym::kRvaGlobeAI || fn == sym::kImageBase + sym::kRvaConflagAI) {
    static const int ok = AllOk({sym::kRvaGlobeAI, sym::kRvaConflagAI}) ? 1 : 0;
    return ok != 0;
  }
  return (fn == sym::kImageBase + sym::kRvaAreaAI || fn == sym::kImageBase + sym::kRvaSwarmAI) &&
         R<int32_t>(o + sym::kOffProjArrived) != 0;
}

void __fastcall HookProjAI(void* obj) {
  const uint64_t o = reinterpret_cast<uint64_t>(obj);
  const auto it = g_proj_orig.find(R<uint64_t>(o));
  if (it == g_proj_orig.end()) return;  // cannot happen: only hooked vtables call here
  if (g_proj_gate) {
    const uint64_t fn = reinterpret_cast<uint64_t>(it->second);
    const bool lingers = LingeringFn(fn, o);
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
  if (!p || R<uint8_t>(p + 8) != 0) return false;
  const auto it = g_proj_orig.find(R<uint64_t>(p));
  if (it == g_proj_orig.end()) return false;
  return LingeringFn(reinterpret_cast<uint64_t>(it->second), p);
}

int32_t GameFreezeOwner() {
  static const int ok = AllOk({sym::kRvaFreezeOwnerCmp}) ? 1 : 0;
  const uint64_t pst = ok ? R<uint64_t>(sym::kVaPlanescape) : 0;
  return pst ? R<int32_t>(pst + sym::kOffPstFreezeOwner) : -1;
}

// Probe: the PST "cinematic" spell projectiles (Celestial Host win 140268EA0 and kin) run their stage machine only
// while game +0xCE90 <= 0 and their +0x224 equals game +0xB8A4, and leave stage 0 once g_PST +0x674 (the game freeze
// owner, sub_14025AB10) is theirs, or game +0xB8A4 == 1, or game +0xB558 == 65902.
std::string CinematicProbe(int32_t projectile) {
  const uint64_t p = LookupObject(projectile);
  const uint64_t g = GameObject();
  const uint64_t pst = R<uint64_t>(sym::kVaPlanescape);
  if (!p || !g || !pst) return "?";
  char b[200];
  std::snprintf(b, sizeof b, "stage %u, own %d vs game %d, game+CE90 %d, freeze owner %X (me %X), game+B558 %d",
                R<uint8_t>(p + 504), R<int32_t>(p + 548), R<int32_t>(g + 47268), R<int32_t>(g + 52880),
                static_cast<unsigned>(R<int32_t>(pst + 1652)), static_cast<unsigned>(R<int32_t>(p + 72)), R<int32_t>(g + 46424));
  return b;
}

uint64_t ProjectileUpdateFn(int32_t projectile) {
  const uint64_t p = LookupObject(projectile);
  if (!p || R<uint8_t>(p + 8) != 0) return 0;
  const uint64_t vt = R<uint64_t>(p);
  const auto it = g_proj_orig.find(vt);
  return it != g_proj_orig.end() ? reinterpret_cast<uint64_t>(it->second) : R<uint64_t>(vt + sym::kVtblObjAIUpdate);
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
      if (run != running) {
        set_running(a, run ? 1 : 0, 0);
        // The walk/run cycle is picked by the animation's SetSequence only (symbols.h AnimPst.vtbl.SetSequence):
        // mid-walk, set the walk sequence again so the frames match the new speed (Withdraw walks).
        // Only when the cycle changes (4 run, 5 walk): an AI chasing its target (Attack) has the engine turn
        // running off again every tick, and setting the sequence each time restarts the cycle at frame 0 (the
        // creature glides on one frame).
        static const int seq_ok = AllOk({sym::kRvaSlotAnimSetSeq}) ? 1 : 0;
        const int16_t want = run ? sym::kAnimCycleRun : sym::kAnimCycleWalk;
        // An animation without run frames stays on the walk cycle when running: remembered, not re-set every tick.
        static std::unordered_map<uint64_t, int16_t> no_cycle;  // anim -> the cycle it could not take
        const auto nc = no_cycle.find(a);
        if (seq_ok && R<uint16_t>(p + sym::kOffSpriteSequence) == sym::kSeqWalk && R<int16_t>(a + sym::kOffAnimCycle) != want &&
            (nc == no_cycle.end() || nc->second != want)) {
          using SetSeqFn = int64_t(__fastcall*)(uint64_t, int16_t);
          reinterpret_cast<SetSeqFn>(R<uint64_t>(R<uint64_t>(a) + sym::kVtblAnimSetSeq))(a, static_cast<int16_t>(sym::kSeqWalk));
          if (R<int16_t>(a + sym::kOffAnimCycle) != want) no_cycle[a] = want;
        }
      }
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
uint8_t g_hit_site = 0;
std::vector<RollEvent> g_rolls;

int32_t IdOf(void* sprite) {
  return sprite ? R<int32_t>(reinterpret_cast<uint64_t>(sprite) + sym::kOffSpriteId) : -1;
}

uint8_t HookHit(uint8_t site, void* self, void* target, void* item, int ability, int* crit, int a6) {
  const int32_t prev = g_hit_target;
  const uint8_t prev_site = g_hit_site;
  g_hit_target = IdOf(target);
  g_hit_site = site;
  const size_t n0 = g_rolls.size();
  const uint8_t r = g_hit(self, target, item, ability, crit, a6);
  if (r && crit && *crit && g_rolls.size() > n0 && g_rolls.back().actor == IdOf(self)) g_rolls.back().crit = true;
  g_hit_target = prev;
  g_hit_site = prev_site;
  return r;
}

MeleeThac0Fn g_melee_thac0 = nullptr;
MeleeOutcomeFn g_melee_outcome = nullptr;
// Swing's melee Hit call (sym::kMeleeHitSites): a takedown's THAC0 for this one roll, then blocked or tripping
// hits do no damage (Swing goes down its miss branch, lin 4EAC10 L560-561).
uint8_t HookHitFiltered(uint8_t site, void* self, void* target, void* item, int ability, int* crit, int a6) {
  const bool melee = (sym::kMeleeHitSites >> site) & 1;
  if (!melee || (!g_melee_thac0 && !g_melee_outcome)) return HookHit(site, self, target, item, ability, crit, a6);
  const int32_t a = IdOf(self), t = IdOf(target);
  const uint64_t p = reinterpret_cast<uint64_t>(self);
  const int add = g_melee_thac0 && p ? g_melee_thac0(a, t) : 0;
  uint64_t thac0 = 0;
  int16_t before = 0;
  if (add && PlausiblePtr(p)) {
    thac0 = Stats(p) + sym::kOffStatThac0;
    before = R<int16_t>(thac0);
    W<int16_t>(thac0, static_cast<int16_t>(before + add));
  }
  const size_t n0 = g_rolls.size();
  uint8_t r = HookHit(site, self, target, item, ability, crit, a6);
  if (thac0) W<int16_t>(thac0, before);
  RollEvent* e = g_rolls.size() > n0 ? &g_rolls.back() : nullptr;
  const int roll = e && e->actor == a ? e->roll : 0;
  const uint8_t out = g_melee_outcome ? g_melee_outcome(a, t, r != 0, roll) : 0;
  if (out && r) {
    r = 0;
    if (crit) *crit = 0;
    if (e && e->actor == a) e->success = false, e->crit = false, e->outcome = out;
  }
  return r;
}
uint8_t __fastcall HookHit1(void* s, void* t, void* i, int ab, int* c, int a6) { return HookHitFiltered(1, s, t, i, ab, c, a6); }
uint8_t __fastcall HookHit2(void* s, void* t, void* i, int ab, int* c, int a6) { return HookHitFiltered(2, s, t, i, ab, c, a6); }
uint8_t __fastcall HookHit3(void* s, void* t, void* i, int ab, int* c, int a6) { return HookHitFiltered(3, s, t, i, ab, c, a6); }

void __fastcall HookFeedBack4(void* self, uint32_t type, int total, int hit, uint32_t roll, int a6, int a7, void* str) {
  if (type == 4 && g_rolls.size() < 64) {
    RollEvent e;
    e.kind = RollEvent::Attack;
    e.actor = IdOf(self);
    e.target = g_hit_target;
    e.roll = static_cast<int>(roll);
    e.total = static_cast<int16_t>(total);
    e.success = hit != 0;
    e.site = g_hit_site;
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
  const uint8_t resist_before = resist ? *resist : 0x80;
  const int64_t r = g_check_save(eff, sprite, poly, wand, death, breath, spell, resist);
  // symbols.h kRvaResistMark: the engine marks the roll when it prints "Spell resisted" (once per spell).
  if (g_resist_fn && resist && !(resist_before & 0x80) && (*resist & 0x80) && r == 0) g_resist_fn(IdOf(sprite));
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
std::map<int32_t, int> g_casts;  // feedback 5 per sprite: the engine's "casts <spell>" line, sent when a cast fires
bool g_feedback_log = false;     // env PSTF_FEEDBACK_LOG: also log every FeedBack
void __fastcall HookFeedBackAll(void* self, uint32_t type, int a, int b, uint32_t c, int d, int e, void* str) {
  if (g_feedback_log) Log("combat: feedback %u (%d, %d, %u) from %X", type, a, b, c, static_cast<unsigned>(IdOf(self)));
  // FeedBack(5, 0|1, ...) comes from CGameSprite::Spell/SpellPoint/SpellPST/SpellPointPST when the spell goes off
  // (lin 4CDDC0 l.821/823, 4CF7E0 l.560/562); a cast the engine refuses (no ability for the caster level) sends none.
  if (type == 5) ++g_casts[IdOf(self)];
  g_feedback_all(self, type, a, b, c, d, e, str);
}
}  // namespace

int SpellCasts(int32_t id) {
  if (!g_feedback_all) return -1;
  const auto it = g_casts.find(id);
  return it == g_casts.end() ? 0 : it->second;
}

bool InstallFeedbackHook(bool log) {
  if (log && !g_feedback_log) {
    g_feedback_log = true;
    Log("combat: feedback log on");
  }
  if (g_feedback_all) return true;
  const sym::Symbol* s = Find(sym::kRvaFeedBack);
  if (!s) return false;
  const uint64_t t = InlineHook(*s, 5, reinterpret_cast<void*>(&HookFeedBackAll));
  if (!t) return false;
  g_feedback_all = reinterpret_cast<FeedBackFn>(t);
  return true;
}
bool InstallFeedbackLog() { return InstallFeedbackHook(true); }

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
    void* hook = site == sym::kRvaSwingHit1   ? reinterpret_cast<void*>(&HookHit1)
                 : site == sym::kRvaSwingHit2 ? reinterpret_cast<void*>(&HookHit2)
                                              : reinterpret_cast<void*>(&HookHit3);
    const uint64_t h = RedirectCall(*Find(site), hook);
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

bool SilenceSaveFeedback() {
  static bool done = false;
  if (done) return true;
  const sym::Symbol* s = Find(sym::kRvaSaveFeedbackRead);
  if (!s || !AllOk({sym::kRvaSaveFeedbackRead})) {
    Log("combat: CheckSave feedback read differs; the engine's \"Save vs.\" line stays");
    return false;
  }
  static const uint8_t kXorEax[] = {0x31, 0xC0, 0x90, 0x90, 0x90, 0x90};  // xor eax, eax; 4x nop
  done = PatchCode(*s, kXorEax, sizeof kXorEax);
  Log("combat: engine \"Save vs.\" line %s", done ? "off (save banner)" : "patch failed");
  return done;
}

void SetMeleeHitFilter(MeleeThac0Fn thac0_add, MeleeOutcomeFn outcome) {
  g_melee_thac0 = thac0_add;
  g_melee_outcome = outcome;
}

// --- effects -------------------------------------------------------------------------------------
namespace {
#pragma pack(push, 1)
struct ItemEffect {  // Item_effect_st (ITM/SPL feature block v1, 0x30 bytes)
  uint16_t opcode;
  uint8_t target, power;
  uint32_t param1, param2;
  uint8_t timing, dispel;
  uint32_t duration;
  uint8_t prob1, prob2;
  char res[8];
  uint32_t dice_count, dice_sides, save_type;
  int32_t save_bonus;
  uint32_t special;
};
#pragma pack(pop)
static_assert(sizeof(ItemEffect) == 0x30, "Item_effect_st is 0x30 bytes");
struct EnginePoint {
  int32_t x, y;
};
using DecodeEffectFn = void*(__fastcall*)(ItemEffect*, const EnginePoint*, int, const EnginePoint*, int);
using AddEffectFn = void(__fastcall*)(void* sprite, void* effect, uint8_t list, int no_save, int immediate);
}  // namespace

bool EffectsAvailable() {
  static const int ok = AllOk({sym::kRvaDecodeEffect, sym::kRvaAddEffectCall}) ? 1 : 0;
  return ok != 0;
}

bool ApplyEffect(int32_t target, int32_t source, const EffectSpec& s) {
  if (!EffectsAvailable()) return false;
  const uint64_t p = LookupSprite(target);
  if (!p || !PlausiblePtr(p)) return false;
  const uint64_t vt = R<uint64_t>(p);
  if (!PlausiblePtr(vt)) return false;
  ItemEffect fx{};
  fx.opcode = s.opcode;
  fx.target = 1;  // self (the creature it is added to)
  fx.param1 = s.param1;
  fx.param2 = s.param2;
  fx.timing = s.timing;
  fx.duration = s.duration;
  fx.prob1 = 100;  // CGameEffect::ClearItemEffect: always
  fx.save_type = s.save_type;
  fx.save_bonus = s.save_bonus;
  fx.special = s.special;
  const uint64_t ps = source >= 0 ? LookupSprite(source) : 0;
  const EnginePoint from = ps ? EnginePoint{R<int32_t>(ps + sym::kOffSpritePos), R<int32_t>(ps + sym::kOffSpritePos + 4)}
                              : EnginePoint{-1, -1};
  const EnginePoint to{R<int32_t>(p + sym::kOffSpritePos), R<int32_t>(p + sym::kOffSpritePos + 4)};
  void* eff = reinterpret_cast<DecodeEffectFn>(sym::kImageBase + sym::kRvaDecodeEffect)(&fx, &from, source, &to, -1);
  if (!eff) return false;
  reinterpret_cast<AddEffectFn>(R<uint64_t>(vt + sym::kVtblAddEffect))(reinterpret_cast<void*>(p), eff,
                                                                         sym::kEffectListTimed, 0, 1);
  return true;
}

bool SpriteClass(int32_t id, uint8_t* cls, uint8_t* specific) {
  const uint64_t p = LookupSprite(id);
  if (!p) return false;
  *cls = R<uint8_t>(p + sym::kOffSpriteClass);
  *specific = R<uint8_t>(p + sym::kOffSpriteSpecific);
  return true;
}

// CDerivedStats starts with the general state (CGameEffectSleep::ApplyEffect lin 223AC0 L35: sprite dword 1018
// = +0xFE8 = kOffSpriteStatsA, |= 0x20).
int64_t SpriteState(int32_t id) {
  const uint64_t p = LookupSprite(id);
  return p ? static_cast<int64_t>(R<uint32_t>(Stats(p))) : -1;
}

std::vector<RollEvent> TakeRollEvents() {
  std::vector<RollEvent> out;
  out.swap(g_rolls);
  return out;
}

bool SetHpForTest(int32_t id, int hp, int max_hp) {
  const uint64_t p = LookupSprite(id);
  if (!p || hp < 1 || hp > 30000 || max_hp > 30000) return false;
  if (max_hp > 0) {
    // base max HP (+0x576, next to current HP: CGameAIBase::ExecuteAction win 14016C8E0 L2729 copies it to +0x574
    // on a full heal); the derived stats follow when the effects are re-evaluated, at once with +0x48BC
    W<int16_t>(p + sym::kOffSpriteHp + 2, static_cast<int16_t>(max_hp));
    W<int32_t>(p + sym::kOffSpriteForceEffects, 1);
  }
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

int NumAttacksStat(int32_t id) {
  static const int ok = AllOk({sym::kRvaThac0Ac, sym::kRvaHalfAttack}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  return p ? R<int16_t>(Stats(p) + sym::kOffStatNumAttacks) : -1;
}

int32_t DerivedStat(int32_t id, int stat) {
  static const int ok = AllOk({sym::kRvaStatAt}) ? 1 : 0;
  const uint64_t p = ok && stat >= 1 && stat <= 202 ? LookupSprite(id) : 0;
  if (!p) return INT32_MIN;
  return static_cast<int32_t>(
      reinterpret_cast<int64_t(__fastcall*)(uint64_t, int16_t)>(Va(sym::kRvaStatAt))(Stats(p), static_cast<int16_t>(stat)));
}

void SetHalfAttackReal(int32_t id, bool real) {
  static const int ok = AllOk({sym::kRvaHalfAttack}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  if (!p) return;
  const uint32_t v = R<uint32_t>(p + sym::kOffSpriteHalfAttack);
  if (((v & 1) == 0) != real) W<uint32_t>(p + sym::kOffSpriteHalfAttack, v + 1);
}

void KeepVisible(const std::vector<int32_t>& ids) {
  static const int ok = AllOk({sym::kRvaShowMonstersVis}) ? 1 : 0;
  if (!ok) return;
  for (int32_t id : ids) {
    const uint64_t p = LookupSprite(id);
    if (p && R<int16_t>(p + sym::kOffObjVisible) < sym::kVisibleCount) W<int16_t>(p + sym::kOffObjVisible, sym::kVisibleCount);
  }
}

void ResetBoredTimer() {
  static const int ok = AllOk({sym::kRvaBoredInc}) ? 1 : 0;
  const uint64_t c = ok ? R<uint64_t>(sym::kVaBaldurChitin) : 0;
  const uint64_t w = c && PlausiblePtr(c) ? R<uint64_t>(c + sym::kOffChitinWorldScreen) : 0;
  if (w && PlausiblePtr(w)) W<uint32_t>(w + sym::kOffWorldBored, 0);
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

int ActionBarListCount() {
  static const int ok = AllOk({sym::kRvaButtonArray, sym::kRvaBarList}) ? 1 : 0;
  if (!ok) return 0;
  const uint64_t list = R<uint64_t>(sym::kVaButtonList);
  if (!PlausiblePtr(list)) return 0;
  return R<int32_t>(list + sym::kOffPtrListCount);
}
bool ActionBarScroll(int index) {
  static const int ok = AllOk({sym::kRvaButtonArray, sym::kRvaBarScroll, sym::kRvaUpdateButtons}) ? 1 : 0;
  const uint64_t g = ok ? GameObject() : 0;
  if (!g || index < 0) return false;
  const uint64_t arr = g + sym::kOffGameButtonArray;
  W<int32_t>(arr + sym::kOffBarScroll, index);
  reinterpret_cast<void(__fastcall*)(uint64_t)>(Va(sym::kRvaUpdateButtons))(arr);
  return true;
}
int ActionBarStateId() {
  static const int ok = AllOk({sym::kRvaButtonArray, sym::kRvaUpdateButtons}) ? 1 : 0;
  const uint64_t g = ok ? GameObject() : 0;
  return g ? R<int32_t>(g + sym::kOffGameButtonArray + sym::kOffBarState) : -1;
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

namespace pstf::engine {
// --- sprite update, poses, bumping ---------------------------------------------------------------------
namespace {
SpriteUpdateFn g_update = nullptr;
void(__fastcall* g_update_orig)(void*) = nullptr;
BumpGuardFn g_bump = nullptr;
using BumpFn = int64_t(__fastcall*)(void*);
BumpFn g_bump_orig = nullptr;

void OrigUpdate(void* sprite) { g_update_orig(sprite); }
void __fastcall HookSpriteUpdate(void* sprite) {
  if (!g_update) return g_update_orig(sprite);
  g_update(sprite, R<int32_t>(reinterpret_cast<uint64_t>(sprite) + sym::kOffSpriteId), &OrigUpdate);
}
int64_t __fastcall HookBumpable(void* sprite) {
  if (g_bump && sprite && g_bump(R<int32_t>(reinterpret_cast<uint64_t>(sprite) + sym::kOffSpriteId))) return 0;
  return g_bump_orig(sprite);
}
bool PoseOk() {
  static const int ok = AllOk({sym::kRvaSlotAnimSetSeq, sym::kRvaFrameSet, sym::kRvaSeqLength, sym::kRvaSetSequence}) ? 1 : 0;
  return ok != 0;
}
uint64_t PstAnim(uint64_t p) {
  const uint64_t a = p ? R<uint64_t>(p + sym::kOffSpriteAnim) : 0;
  return PlausiblePtr(a) && R<uint64_t>(a) == sym::kVaAnimPstVtable ? a : 0;
}
}  // namespace

bool InstallSpriteUpdate(SpriteUpdateFn fn) {
  if (g_update_orig) {
    g_update = fn;
    return true;
  }
  const sym::Symbol* slot = Find(sym::kRvaSlotAIUpdate);
  if (!slot || !CheckSymbol(*slot) || !AllOk({sym::kRvaAIUpdate})) return false;
  const uint64_t was = ReplacePointer(*slot, reinterpret_cast<void*>(&HookSpriteUpdate));
  if (was != sym::kImageBase + sym::kRvaAIUpdate) return false;
  g_update_orig = reinterpret_cast<void(__fastcall*)(void*)>(was);
  g_update = fn;
  Log("combat: sprite update hook installed (CGameSprite::AIUpdate slot)");
  return true;
}

bool InstallBumpGuard(BumpGuardFn guard) {
  if (g_bump_orig) {
    g_bump = guard;
    return true;
  }
  const sym::Symbol* a = Find(sym::kRvaBumpCall1);
  const sym::Symbol* b = Find(sym::kRvaBumpCall2);
  if (!a || !b || !CheckSymbol(*a) || !CheckSymbol(*b)) return false;
  const uint64_t t1 = RedirectCall(*a, reinterpret_cast<void*>(&HookBumpable));
  const uint64_t t2 = RedirectCall(*b, reinterpret_cast<void*>(&HookBumpable));
  if (t1 != sym::kVaCheckBumpable || t2 != sym::kVaCheckBumpable) return false;
  g_bump_orig = reinterpret_cast<BumpFn>(t1);
  g_bump = guard;
  Log("combat: bump guard installed (2 CheckBumpable calls)");
  return true;
}

int16_t SpriteSequence(int32_t id) {
  const uint64_t p = LookupSprite(id);
  return p ? R<int16_t>(p + sym::kOffSpriteSequence) : -1;
}

bool InAttackRound(int32_t id) {
  static const int ok = AllOk({sym::kRvaAttackFudge}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  return p && R<int32_t>(p + sym::kOffSpriteAttackFudge) != 0;
}

int AttackRoundPos(int32_t id) {
  static const int ok = AllOk({sym::kRvaAttackRound}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  return p ? R<int16_t>(p + sym::kOffSpriteAttackRound) : -2;
}

void NewAttackRound(int32_t id) {
  static const int ok = AllOk({sym::kRvaAttackRound}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  if (p) W<uint16_t>(p + sym::kOffSpriteAttackRound, 0xFFFF);
}

void KeepAttackRound(int32_t id) {
  static const int ok = AllOk({sym::kRvaAttackFudge}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  if (p) W<int32_t>(p + sym::kOffSpriteAttackFudge, 1);
}

bool SetSpriteSequence(int32_t id, int16_t seq) {
  const uint64_t p = PoseOk() ? LookupSprite(id) : 0;
  if (!p) return false;
  reinterpret_cast<void(__fastcall*)(uint64_t, int16_t)>(Va(sym::kRvaSetSequence))(p, seq);
  return true;
}

int GameCursorMode() {
  static const int ok = AllOk({sym::kRvaMopCursor}) ? 1 : 0;
  const uint64_t g = ok ? GameObject() : 0;
  return g ? R<uint8_t>(g + sym::kOffGameCursorMode) : -1;
}
bool SetCursorMode(int mode) {
  static const int ok = AllOk({sym::kRvaMopCursor}) ? 1 : 0;
  const uint64_t g = ok ? GameObject() : 0;
  if (!g) return false;
  W<uint8_t>(g + sym::kOffGameCursorMode, static_cast<uint8_t>(mode));
  return true;
}
namespace {
bool MultiOk() {
  static const int ok = AllOk({sym::kRvaMultiFire, sym::kRvaMultiFireSpell, sym::kRvaGameSetState, sym::kRvaMopCursor}) ? 1 : 0;
  return ok != 0;
}
bool IsMultiSpell(int n) { return n == 2207 || n == 2405 || n == 2410; }
}  // namespace
void AreaListCount(int32_t id, int* front, int* back) {
  *front = *back = -1;
  const uint64_t p = LookupSprite(id);
  const uint64_t area = p ? R<uint64_t>(p + sym::kOffSpriteArea) : 0;
  if (!PlausiblePtr(area)) return;
  auto count = [&](uint32_t off) {
    int n = 0, guard = 0;
    for (uint64_t node = R<uint64_t>(area + off); node && PlausiblePtr(node) && guard < 4096; node = R<uint64_t>(node), ++guard)
      if (R<int32_t>(node + 0x10) == id) ++n;
    return n;
  };
  *front = count(sym::kOffAreaFrontHead);
  *back = count(sym::kOffAreaBackHead);
}
void MultiFireRaw(int32_t caster, int* left, int* spell) {
  const uint64_t p = MultiOk() ? LookupSprite(caster) : 0;
  *left = p ? R<int32_t>(p + sym::kOffSpriteMultiLeft) : -1;
  *spell = p ? R<int32_t>(p + sym::kOffSpriteMultiSpell) : -1;
}
int MultiFireLeft(int32_t caster) {
  const uint64_t p = MultiOk() ? LookupSprite(caster) : 0;
  // the spell number is set by ReadySpell (a bar cast); a scripted cast leaves 0 (clicks then do nothing)
  const int spell = R<int32_t>(p + sym::kOffSpriteMultiSpell);
  if (!IsMultiSpell(spell) && !(spell == 0 && !MopUp())) return 0;
  const int n = R<int32_t>(p + sym::kOffSpriteMultiLeft);
  return n > 0 && n < 100 ? n : 0;
}
bool MultiFire(int32_t caster, int32_t target, int spell) {
  const uint64_t t = MultiFireLeft(caster) > 0 ? LookupSprite(target) : 0;
  if (!t) return false;
  if (IsMultiSpell(spell)) W<int32_t>(LookupSprite(caster) + sym::kOffSpriteMultiSpell, spell);  // as ReadySpell
  using Fn = char(__fastcall*)(uint64_t, uint64_t, uint32_t);
  return reinterpret_cast<Fn>(Va(sym::kRvaMultiFire))(R<uint64_t>(sym::kVaPlanescape), t, static_cast<uint32_t>(caster)) != 0;
}
bool MultiFireDrop(int32_t caster) {
  const uint64_t p = MultiFireLeft(caster) > 0 ? LookupSprite(caster) : 0;
  if (!p) return false;
  W<int32_t>(p + sym::kOffSpriteMultiLeft, 0);
  const uint64_t g = GameObject();
  if (g && R<uint8_t>(g + sym::kOffGameCursorMode) == sym::kCursorModeTarget) {
    reinterpret_cast<void(__fastcall*)(uint64_t, int16_t, int32_t)>(Va(sym::kRvaGameSetState))(g, 0, 0);
    W<uint8_t>(g + sym::kOffGameCursorMode, 0);
  }
  return true;
}
bool MopClickTarget(int32_t caster, int32_t target) {
  static const int ok = AllOk({sym::kRvaMopTarget}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(caster) : 0;
  if (!p) return false;
  W<int32_t>(p + sym::kOffSpriteMopTarget, target + 1);
  return true;
}

bool MopUp() {
  static const int ok = AllOk({sym::kRvaMopFlag}) ? 1 : 0;
  const uint64_t pst = ok ? R<uint64_t>(sym::kVaPlanescape) : 0;
  return PlausiblePtr(pst) && R<int32_t>(pst + sym::kOffPstMopUp) != 0;
}

namespace {
MopShotFn g_mop_shot = nullptr;
uint64_t g_mop_transfer = 0;
int64_t __fastcall HookMopTransfer(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
  if (g_mop_shot && !g_mop_shot(a1)) return 0;
  return reinterpret_cast<int64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t)>(g_mop_transfer)(a1, a2, a3, a4);
}
}  // namespace

bool InstallMopShotHook(MopShotFn fn) {
  if (g_mop_transfer) {
    g_mop_shot = fn;
    return true;
  }
  const sym::Symbol* s = Find(sym::kRvaMopShotCall);
  if (!s || !AllOk({sym::kRvaMopShotCall, sym::kRvaMopFlag, sym::kRvaMopCursor})) return false;
  const uint64_t orig = RedirectCall(*s, reinterpret_cast<void*>(&HookMopTransfer));
  if (orig != sym::kImageBase + sym::kRvaMopTransfer) {
    Log("combat: Missile of Patience hook FAILED (%llX)", static_cast<unsigned long long>(orig));
    return false;
  }
  g_mop_transfer = orig;
  g_mop_shot = fn;
  Log("combat: Missile of Patience shot hook installed at %llX", reinterpret_cast<unsigned long long>(Va(sym::kRvaMopShotCall)));
  return true;
}

bool SetSpriteDirection(int32_t id, int dir) {
  static const int ok = AllOk({sym::kRvaSetDirection}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  if (!p || dir < 0 || dir > 15) return false;
  reinterpret_cast<void(__fastcall*)(uint64_t, int16_t)>(Va(sym::kRvaSetDirection))(p, static_cast<int16_t>(dir));
  return true;
}

int PoseLying(int32_t id, int back) {
  const uint64_t p = PoseOk() ? LookupSprite(id) : 0;
  const uint64_t a = PstAnim(p);
  if (!a) return 0;
  const int16_t cyc = R<int16_t>(a + sym::kOffAnimCycle);
  if (cyc < 10 || cyc > 12) {  // not the die cycle (a hit, a new order): lie down again
    using SetSeqFn = int64_t(__fastcall*)(uint64_t, int16_t);
    reinterpret_cast<SetSeqFn>(R<uint64_t>(R<uint64_t>(a) + sym::kVtblAnimSetSeq))(a, sym::kSeqDie);
  }
  const uint64_t cell = R<uint64_t>(a + sym::kOffAnimCell);
  if (!PlausiblePtr(cell)) return 0;
  using LenFn = int16_t(__fastcall*)(uint64_t, uint16_t);
  const int len = reinterpret_cast<LenFn>(Va(sym::kRvaSeqLength))(cell, R<uint16_t>(cell + sym::kOffCellSeq));
  if (len <= 0) return 0;
  const int f = std::max(0, len - 1 - std::max(0, back));
  reinterpret_cast<int64_t(__fastcall*)(uint64_t, uint16_t)>(Va(sym::kRvaFrameSet))(cell, static_cast<uint16_t>(f));
  return len;
}

bool PlayArmorHitSound(int32_t id, uint32_t damage_type) {
  static const int ok = AllOk({sym::kRvaHitSound, sym::kRvaHitSoundPst}) ? 1 : 0;
  const uint64_t p = ok ? LookupSprite(id) : 0;
  const uint64_t pst = R<uint64_t>(sym::kVaPlanescape);
  if (!p || !PlausiblePtr(pst)) return false;
  reinterpret_cast<void(__fastcall*)(uint64_t, uint32_t, uint64_t)>(Va(sym::kRvaHitSound))(pst, damage_type, p);
  return true;
}

std::string AnimProbe(int32_t id) {
  const uint64_t p = LookupSprite(id);
  const uint64_t a = PstAnim(p);
  char b[96];
  if (!a) {
    std::snprintf(b, sizeof b, "-1 -1 -1 %d", p ? R<int16_t>(p + sym::kOffSpriteSequence) : -1);
    return b;
  }
  const uint64_t cell = R<uint64_t>(a + sym::kOffAnimCell);
  std::snprintf(b, sizeof b, "%d %d %d %d", R<int32_t>(a + sym::kOffAnimRunFlag), R<int16_t>(a + sym::kOffAnimCycle),
                PlausiblePtr(cell) ? R<int16_t>(cell + sym::kOffCellFrame) : -1, R<int16_t>(p + sym::kOffSpriteSequence));
  return b;
}
}  // namespace pstf::engine

namespace pstf::engine {
// --- floating text --------------------------------------------------------------------------------------
namespace {
using AddFloaterFn = int64_t(__fastcall*)(void* area, void* msg, int replace, int to_log);
using DamageApplyFn = int64_t(__fastcall*)(void* effect, void* sprite);
using DtorFn = void(__fastcall*)(void*);
using SizedDeleteFn = void(__fastcall*)(void*, size_t);
AddFloaterFn g_add_floater = nullptr;
DamageApplyFn g_damage_apply = nullptr;
FloaterFn g_floater_fn = nullptr;
// The damage effect being applied (nested applies keep the outer one's after they return).
struct DamageCtx {
  int32_t target = -1;
  uint32_t bits = 0;
  bool on = false;
};
DamageCtx g_damage;

int64_t __fastcall HookDamageApply(void* effect, void* sprite) {
  const DamageCtx prev = g_damage;
  if (effect && sprite) {
    g_damage.on = true;
    g_damage.target = IdOf(sprite);
    g_damage.bits = R<uint32_t>(reinterpret_cast<uint64_t>(effect) + sym::kOffEffParam2) >> 16;
  }
  const int64_t r = g_damage_apply(effect, sprite);
  g_damage = prev;
  return r;
}

int64_t __fastcall HookAddFloater(void* area, void* msg, int replace, int to_log) {
  const uint64_t m = reinterpret_cast<uint64_t>(msg);
  if (g_floater_fn && m && PlausiblePtr(m)) {
    FloaterInfo f;
    f.owner = R<int32_t>(m + sym::kOffFloatOwner);
    f.color = R<uint32_t>(m + sym::kOffFloatColor);
    f.type = R<uint32_t>(m + sym::kOffFloatType);
    f.to_log = to_log;
    const uint64_t t = R<uint64_t>(m + sym::kOffFloatText);
    if (t >= 0x10000 && t < 0x00007FFFFFFF0000ull) {  // CString data: a heap pointer, not 8-aligned
      const char* s = reinterpret_cast<const char*>(t);
      size_t n = 0;
      while (n < 256 && s[n]) ++n;
      f.text.assign(s, n);
    }
    f.in_damage = g_damage.on;
    f.damage_target = g_damage.target;
    f.damage_bits = g_damage.bits;
    f.caller_rva = static_cast<uint32_t>(reinterpret_cast<uint64_t>(_ReturnAddress()) - sym::kImageBase);
    if (!to_log && g_floater_fn(f)) {
      reinterpret_cast<DtorFn>(sym::kVaFloaterDtor)(msg);
      reinterpret_cast<SizedDeleteFn>(sym::kVaSizedDelete)(msg, 0x80);
      return 0;
    }
  }
  return g_add_floater(area, msg, replace, to_log);
}
}  // namespace

bool InstallFloaterHook(FloaterFn fn) {
  if (g_add_floater) {
    g_floater_fn = fn;
    return true;
  }
  if (!AllOk({sym::kRvaAddFloater, sym::kRvaFloaterDtorCall, sym::kRvaFloaterDeleteCall, sym::kRvaDamageApply})) {
    Log("combat: floating text functions differ; the engine's rising text stays");
    return false;
  }
  const uint64_t d = InlineHook(*Find(sym::kRvaDamageApply), 5, reinterpret_cast<void*>(&HookDamageApply));
  if (!d) return false;
  g_damage_apply = reinterpret_cast<DamageApplyFn>(d);
  const uint64_t a = InlineHook(*Find(sym::kRvaAddFloater), 8, reinterpret_cast<void*>(&HookAddFloater));
  if (!a) return false;
  g_add_floater = reinterpret_cast<AddFloaterFn>(a);
  g_floater_fn = fn;
  Log("combat: floating text hook installed (AddToFloaterList, CGameEffectDamage::ApplyEffect)");
  return true;
}
}  // namespace pstf::engine
