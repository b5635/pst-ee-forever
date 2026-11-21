// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/screens.h"

#include <cstdint>

#include "core/patch.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {
uint64_t Q(uint64_t va) { return *reinterpret_cast<const volatile uint64_t*>(va); }
uint64_t Chitin() { return Q(sym::kVaBaldurChitin); }
}  // namespace

const char* ScreenName(Screen s) {
  switch (s) {
    case Screen::None: return "none";
    case Screen::Start: return "start";
    case Screen::Load: return "load";
    case Screen::World: return "world";
    default: return "other";
  }
}

bool ScreensAvailable() {
  static const int ok = [] {
    const uint32_t need[] = {sym::kRvaActiveEngineCmp, sym::kRvaGetEngineStart,
                             sym::kRvaGetEngineWorld, sym::kRvaContinueLoadScreen,
                             sym::kRvaOnContinueGame, sym::kRvaWorldTimeChain};
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

Screen ActiveScreen() {
  if (!ScreensAvailable()) return Screen::None;
  const uint64_t c = Chitin();
  if (!c) return Screen::None;
  const uint64_t a = Q(c + sym::kOffChitinActiveEngine);
  if (!a) return Screen::None;
  if (a == Q(c + sym::kOffChitinStartScreen)) return Screen::Start;
  if (a == Q(c + sym::kOffChitinWorldScreen)) return Screen::World;
  if (a == Q(c + sym::kOffChitinLoadScreen)) return Screen::Load;
  return Screen::Other;
}

bool ContinueGame() {
  if (!ScreensAvailable()) return false;
  const uint64_t c = Chitin();
  const uint64_t start = c ? Q(c + sym::kOffChitinStartScreen) : 0;
  if (!start) return false;
  using Fn = uint8_t(__fastcall*)(uint64_t);
  return reinterpret_cast<Fn>(Va(sym::kRvaOnContinueGame))(start) != 0;
}

}  // namespace pstf::engine
