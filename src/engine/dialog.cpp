// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/dialog.h"

#include <windows.h>

#include "core/log.h"
#include "core/patch.h"
#include "engine/party.h"
#include "engine/sprite_ptr.h"
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

using DisplayFn = void(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
using HoldFn = uint32_t(__fastcall*)(uint64_t reply, uint64_t sprite);

DisplayFn g_display = nullptr;
HoldFn g_hold = nullptr;
DialogShownFn g_shown = nullptr;
uint32_t g_serial = 0;
uint64_t g_shown_entry = 0;  // only while the shown callback runs (inside Display)
int32_t g_sprite_id = -1;  // the object the node was shown for (an id: looked up again on use)

uint64_t WorldScreen() {
  const uint64_t c = R<uint64_t>(sym::kVaBaldurChitin);
  return PlausiblePtr(c) ? R<uint64_t>(c + sym::kOffChitinWorldScreen) : 0;
}

uint64_t DialogSprite() {
  const uint64_t w = WorldScreen();
  if (!PlausiblePtr(w) || !R<uint8_t>(w + sym::kOffWorldInDialog)) return 0;
  const uint64_t d = R<uint64_t>(w + sym::kOffWorldDialogSprite);
  return PlausiblePtr(d) && R<int32_t>(d + sym::kOffDlgActive) ? d : 0;
}

uint64_t CurrentEntry() {
  if (g_shown_entry) return g_shown_entry;
  const uint64_t d = DialogSprite();
  if (!d) return 0;
  const int32_t n = R<int32_t>(d + sym::kOffDlgEntryCount), cur = R<int32_t>(d + sym::kOffDlgCurrent);
  const uint64_t arr = R<uint64_t>(d + sym::kOffDlgEntries);
  if (cur < 0 || cur >= n || n > 100000 || !PlausiblePtr(arr)) return 0;
  const uint64_t e = R<uint64_t>(arr + 8ull * cur);
  return PlausiblePtr(e) ? e : 0;
}

uint64_t Reply(uint64_t entry, int index) {
  const int32_t n = R<int32_t>(entry + sym::kOffEntryReplyCount);
  const uint64_t arr = R<uint64_t>(entry + sym::kOffEntryReplies);
  if (index < 0 || index >= n || n > 1000 || !PlausiblePtr(arr)) return 0;
  const uint64_t r = R<uint64_t>(arr + 8ull * index);
  return PlausiblePtr(r) ? r : 0;
}

uint32_t AbilityOffset(int stat) {
  switch (stat) {
    case 36: return sym::kOffStatStr;
    case 38: return sym::kOffStatInt;
    case 39: return sym::kOffStatWis;
    case 40: return sym::kOffStatDex;
    case 41: return sym::kOffStatCon;
    case 42: return sym::kOffStatChr;
    default: return 0;
  }
}

// The stats struct CheckStat reads for the Protagonist (CInfGame fixed party slot 0 = GetProtagonist in
// PST, lin 38A3F0 `this + 10362` = +0xA1E8, moved by the 8-member layout).
uint64_t ProtagonistStats() {
  const uint64_t g = GameObject();
  if (!g) return 0;
  const int32_t id = R<int32_t>(g + Party().fixed_off);
  const uint64_t s = id == -1 ? 0 : LookupSprite(id);
  if (!s) return 0;
  return s + (R<int32_t>(s + sym::kOffSpriteStatsSel) ? sym::kOffSpriteStatsA : sym::kOffSpriteStatsB);
}

void __fastcall HookDisplay(uint64_t entry, uint64_t sprite, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                            uint64_t a7, uint64_t a8) {
  ++g_serial;
  g_sprite_id = PlausiblePtr(sprite) ? R<int32_t>(sprite + sym::kOffSpriteId) : -1;
  g_display(entry, sprite, a3, a4, a5, a6, a7, a8);
  if (!g_shown) return;
  if (CurrentEntry() != entry) {
    static int logged = 0;
    if (logged++ < 5)
      Log("dialog: Display entry %llX is not the dialogue's current entry %llX", static_cast<unsigned long long>(entry),
          static_cast<unsigned long long>(CurrentEntry()));
  }
  g_shown_entry = entry;
  g_shown();
  g_shown_entry = 0;
}

}  // namespace

bool InstallDialogHooks(DialogShownFn shown) {
  if (g_display) {
    g_shown = shown;
    return true;
  }
  const sym::Symbol* disp = nullptr;
  const sym::Symbol* hold = nullptr;
  for (const auto& s : sym::kAll) {
    if (s.rva == sym::kRvaDialogDisplay) disp = &s;
    if (s.rva == sym::kRvaDialogHold) hold = &s;
  }
  if (!disp || !hold || !CheckSymbol(*disp) || !CheckSymbol(*hold)) return false;
  g_hold = reinterpret_cast<HoldFn>(Va(sym::kRvaDialogHold));
  g_shown = shown;
  const uint64_t tramp = InlineHook(*disp, sym::kDialogDisplayCopy, reinterpret_cast<void*>(&HookDisplay));
  if (!tramp) return false;
  g_display = reinterpret_cast<DisplayFn>(tramp);
  Log("dialog: Display hook installed at %llX", static_cast<unsigned long long>(sym::kImageBase + disp->rva));
  return true;
}

bool DialogActive() { return CurrentEntry() != 0; }
uint32_t DialogNodeSerial() { return CurrentEntry() ? g_serial : 0; }

std::vector<DialogReply> DialogReplies() {
  std::vector<DialogReply> out;
  const uint64_t e = CurrentEntry();
  if (!e) return out;
  const int32_t n = R<int32_t>(e + sym::kOffEntryReplyCount);
  {
    static int logged = 0;
    if (logged++ < 3) Log("dialog: entry %llX: %d replies", static_cast<unsigned long long>(e), n);
  }
  for (int i = 0; i < n && i < 1000; ++i) {
    const uint64_t r = Reply(e, i);
    if (!r) continue;
    DialogReply d;
    d.index = i;
    d.flags = R<uint32_t>(r + sym::kOffReplyFlags);
    d.strref = R<int32_t>(r + sym::kOffReplyText);
    d.number = R<int8_t>(r + sym::kOffReplyNumber);
    const uint64_t c = R<uint64_t>(r + sym::kOffReplyCond);
    if ((d.flags & 2) && PlausiblePtr(c & ~7ull)) {
      const char* p = reinterpret_cast<const char*>(c);
      size_t len = 0;
      while (len < 4096 && p[len]) ++len;
      d.cond.assign(p, len);
    }
    out.push_back(d);
  }
  return out;
}

uint32_t DialogNumberColor() {
  const uint64_t d = DialogSprite();
  return d ? R<uint32_t>(d + sym::kOffDlgNumColor) & 0xFFFFFF : 0xFFFFFF;
}

// Display: 2174719 (0x212EFF) in a dialogue, 12507095 otherwise (win 1401AC7D8, lin L430).
uint32_t DialogTextColor() {
  const uint64_t w = WorldScreen();
  return PlausiblePtr(w) && R<uint8_t>(w + sym::kOffWorldInDialog) ? 0x212EFF : 0xBED7D7;
}

int DialogReplyHoldsWith(int index, const std::vector<std::pair<int, int>>& set) {
  const uint64_t e = CurrentEntry();
  const uint64_t r = e ? Reply(e, index) : 0;
  const uint64_t who = g_sprite_id == -1 ? 0 : LookupObject(g_sprite_id);
  if (!r || !who || !g_hold) return -1;
  const uint64_t st = set.empty() ? 0 : ProtagonistStats();
  if (!set.empty() && !st) return -1;
  int16_t keep[8] = {};
  const size_t n = set.size() < 8 ? set.size() : 8;
  for (size_t i = 0; i < n; ++i) {
    const uint32_t off = AbilityOffset(set[i].first);
    if (!off) return -1;
    keep[i] = R<int16_t>(st + off);
  }
  for (size_t i = 0; i < n; ++i) W<int16_t>(st + AbilityOffset(set[i].first), static_cast<int16_t>(set[i].second));
  const uint32_t ok = g_hold(r, who);
  for (size_t i = n; i-- > 0;) W<int16_t>(st + AbilityOffset(set[i].first), keep[i]);  // reverse: same stat twice
  return ok ? 1 : 0;
}

int DialogReplyHolds(int index, int stat, int value) {
  if (!stat) return DialogReplyHoldsWith(index, {});
  return DialogReplyHoldsWith(index, {{stat, value}});
}

void SetDialogReplyNumber(int index, int number) {
  const uint64_t e = CurrentEntry();
  const uint64_t r = e ? Reply(e, index) : 0;
  if (!r) return;
  W<int8_t>(r + sym::kOffReplyNumber, static_cast<int8_t>(number));
  if (number > 0) W<int32_t>(r + sym::kOffReplySelected, 0);
}

int ProtagonistAbility(int stat) {
  const uint32_t off = AbilityOffset(stat);
  const uint64_t st = off ? ProtagonistStats() : 0;
  return st ? R<int16_t>(st + off) : -1;
}

}  // namespace pstf::engine
