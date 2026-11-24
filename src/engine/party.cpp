// SPDX-License-Identifier: GPL-2.0-or-later
// 8-member party: engine adapter. See engine/party_layout.h for the layout and evidence.
#include "engine/party.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "core/patch.h"
#include "core/patch_math.h"
#include "engine/party_layout.h"
#include "engine/sprite_ptr.h"
#include "engine/sprites.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {

PartyLayout g_layout{sym::party::kOldFixed, sym::party::kOldOrder, sym::party::kCount, 6};

const sym::Symbol* Find(uint32_t rva) {
  for (const auto& s : sym::kAll)
    if (s.rva == rva) return &s;
  return nullptr;
}

void ClearNewSlots(uint8_t* game, int from) {
  for (int i = from; i < sym::party::kMax; ++i) {
    *reinterpret_cast<int32_t*>(game + sym::party::kNewFixed + 4 * i) = -1;
    *reinterpret_cast<int32_t*>(game + sym::party::kNewOrder + 4 * i) = -1;
  }
}

// CInfGame::CInfGame(this) -> this. The ctor fills the old arrays (win 1402CF4F9 loop) and, through the
// retargeted lea, part of the new ones; every new slot starts at -1 here.
using CtorFn = uint8_t*(__fastcall*)(uint8_t*);
CtorFn g_ctor = nullptr;
uint8_t* __fastcall GameCtor(uint8_t* self) {
  uint8_t* r = g_ctor(self);
  if (self) ClearNewSlots(self, 0);
  return r;
}

// CInfGame::DestroyGame(this, uchar, uchar): its qword stores clear fixed 0..5 only.
using DestroyFn = uint64_t(__fastcall*)(uint8_t*, uint64_t, uint64_t);
DestroyFn g_destroy = nullptr;
uint64_t __fastcall DestroyGame(uint8_t* self, uint64_t a, uint64_t b) {
  const uint64_t r = g_destroy(self, a, b);
  if (self) ClearNewSlots(self, 6);
  return r;
}

// CMultiplayerSettings::GetCharacterControlledByPlayer(this, slot): its i32[6]; slots 6..7 = slot 0
// (single player: one player controls everyone).
using CtrlFn = int64_t(__fastcall*)(uint8_t*, int32_t);
CtrlFn g_ctrl = nullptr;
int64_t __fastcall GetControlled(uint8_t* self, int32_t slot) { return g_ctrl(self, slot >= 6 ? 0 : slot); }

// memset(entry5, 0, 0x160) in CInfGame::Marshal: entries 6..7 (0x168 apart) get zeroed with it.
void* __fastcall MarshalMemset(uint8_t* entry5, int value, size_t n) {
  std::memset(entry5, value, n);
  std::memset(entry5 + 0x168, 0, 2 * 0x168);
  return entry5;
}

}  // namespace

const PartyLayout& Party() { return g_layout; }

std::string PartyDump() {
  const uint64_t chitin = *reinterpret_cast<const uint64_t*>(sym::kVaBaldurChitin);
  const uint64_t game = chitin ? *reinterpret_cast<const uint64_t*>(chitin + sym::kOffChitinGame) : 0;
  if (!game) return "";
  const auto* g = reinterpret_cast<const uint8_t*>(game);
  const PartyLayout& L = g_layout;
  char b[64];
  std::snprintf(b, sizeof b, "max=%d count=%d order=", L.max, *reinterpret_cast<const int16_t*>(g + L.count_off));
  std::string out = b;
  for (int pass = 0; pass < 2; ++pass) {
    const uint32_t off = pass == 0 ? L.order_off : L.fixed_off;
    if (pass) out += " fixed=";
    for (int i = 0; i < L.max; ++i) {
      std::snprintf(b, sizeof b, "%s%d", i ? "," : "", *reinterpret_cast<const int32_t*>(g + off + 4 * i));
      out += b;
    }
  }
  return out;
}

std::string PartyNames() {
  std::string out;
  for (int32_t id : PartyIds()) {
    const uint64_t p = LookupSprite(id);
    if (!out.empty()) out += ",";
    if (!p) {
      out += "?";
      continue;
    }
    const char* c = reinterpret_cast<const char*>(p + 0x478);  // char[32], inline
    for (int i = 0; i < 32 && c[i]; ++i) out += c[i];
  }
  return out;
}

int EvalTrigger(int32_t who, uint16_t id, int32_t param1, const char* object_name, const char* string1) {
  static const bool ok = [] {
    for (uint32_t r : {sym::kRvaTriggerCtor, sym::kRvaEvalTrigger, sym::kRvaCStringAssign}) {
      const sym::Symbol* s = Find(r);
      if (!s || !CheckSymbol(*s)) return false;
    }
    return true;
  }();
  const uint64_t sprite = ok ? LookupSprite(who) : 0;
  if (!sprite) return -1;
  // The engine's CAITrigger is 0x40 bytes; the buffer is larger and 16-aligned. Its strings are
  // engine-allocated and leak with the buffer (probe only, a few bytes per call).
  alignas(16) static uint8_t trig[0x100];
  std::memset(trig, 0, sizeof trig);
  reinterpret_cast<void(__fastcall*)(void*, int16_t, int32_t)>(Va(sym::kRvaTriggerCtor))(trig, static_cast<int16_t>(id), param1);
  if (object_name && *object_name)
    reinterpret_cast<void(__fastcall*)(void*, const char*)>(Va(sym::kRvaCStringAssign))(trig + 8, object_name);
  if (string1 && *string1)
    reinterpret_cast<void(__fastcall*)(void*, const char*)>(Va(sym::kRvaCStringAssign))(trig + 0x30, string1);
  return reinterpret_cast<int(__fastcall*)(uint64_t, const void*)>(Va(sym::kRvaEvalTrigger))(sprite, trig) & 0xFF;
}

int CanSaveGame(uint32_t* reason) {
  const sym::Symbol* s = Find(sym::kRvaCanSaveGame);
  const uint64_t chitin = *reinterpret_cast<const uint64_t*>(sym::kVaBaldurChitin);
  const uint64_t game = chitin ? *reinterpret_cast<const uint64_t*>(chitin + sym::kOffChitinGame) : 0;
  if (!s || !game || !CheckSymbol(*s)) return -1;
  *reason = 0xFFFFFFFFu;
  return reinterpret_cast<uint8_t(__fastcall*)(uint64_t, uint32_t*, uint8_t, uint8_t, uint8_t)>(Va(s->rva))(
      game, reason, 0, 0, 1);
}

bool InstallParty8() {
  uint32_t bad = 0;
  if (const char* why = party::CheckTables(&bad)) {
    Log("party: site table inconsistent at %llX: %s; party module off",
        static_cast<unsigned long long>(sym::kImageBase + bad), why);
    return false;
  }
  const sym::Symbol* ctor = Find(sym::kRvaGameCtorCall);
  const sym::Symbol* destroy = Find(sym::kRvaDestroyGame);
  const sym::Symbol* ctrl = Find(sym::kRvaGetControlled);
  const sym::Symbol* mset = Find(sym::kRvaMarshalMemsetCall);
  if (!ctor || !destroy || !ctrl || !mset || !CheckSymbol(*ctor) || !CheckSymbol(*destroy) || !CheckSymbol(*ctrl) ||
      !CheckSymbol(*mset)) {
    Log("party: hook symbols differ from the manifest; party module off");
    return false;
  }
  int n = party::ApplyAll([](uint32_t rva, const uint8_t* b, size_t len) { return BytesEqual(Va(rva), b, len); },
                          [](uint32_t rva, const uint8_t* b, size_t len) { WriteBytesAt(rva, b, len); }, &bad);
  if (n < 0) {
    Log("party: bytes at %llX differ from the site table; party module off (nothing patched)",
        static_cast<unsigned long long>(sym::kImageBase + bad));
    return false;
  }
  g_ctor = reinterpret_cast<CtorFn>(RedirectCall(*ctor, reinterpret_cast<void*>(&GameCtor)));
  g_destroy = reinterpret_cast<DestroyFn>(InlineHook(*destroy, 15, reinterpret_cast<void*>(&DestroyGame)));
  g_ctrl = reinterpret_cast<CtrlFn>(InlineHook(*ctrl, 6, reinterpret_cast<void*>(&GetControlled)));
  const bool mset_ok = RedirectCall(*mset, reinterpret_cast<void*>(&MarshalMemset)) != 0;
  if (!g_ctor || !g_destroy || !g_ctrl || !mset_ok) {
    // The sites are already written; without the ctor hook slots 6..7 would hold heap garbage.
    Log("party: FATAL hook install failed after patching (ctor %p destroy %p ctrl %p)", g_ctor, g_destroy, g_ctrl);
    return false;
  }
  g_layout = {sym::party::kNewFixed, sym::party::kNewOrder, sym::party::kCount, sym::party::kMax};
  Log("party: 8-member party on: %d sites + patches applied, arrays at +0x%X/+0x%X, CInfGame 0x%X bytes", n,
      sym::party::kNewFixed, sym::party::kNewOrder, sym::party::kNewSize);
  return true;
}

}  // namespace pstf::engine
