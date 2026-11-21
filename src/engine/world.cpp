// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/world.h"

#include "core/patch.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {
template <class T>
T Read(uint64_t va) {
  return *reinterpret_cast<const volatile T*>(va);
}
}  // namespace

bool WorldApiAvailable() {
  static const int ok = [] {
    for (const auto& s : sym::kAll) {
      if (s.rva == sym::kRvaRoundModeCmp || s.rva == sym::kRvaRoundStore ||
          s.rva == sym::kRvaWorldTimeChain) {
        if (!CheckSymbol(s)) return 0;
      }
    }
    return 1;
  }();
  return ok != 0;
}

uint32_t RoundLengthTicks() { return WorldApiAvailable() ? Read<uint32_t>(sym::kVaRoundLength) : 0; }

int EngineMode() { return WorldApiAvailable() ? Read<int32_t>(sym::kVaEngineMode) : -1; }

bool WorldTime(uint32_t* ticks, bool* running) {
  if (!WorldApiAvailable()) return false;
  const uint64_t chitin = Read<uint64_t>(sym::kVaBaldurChitin);
  if (!chitin) return false;
  const uint64_t game = Read<uint64_t>(chitin + sym::kOffChitinGame);
  if (!game) return false;
  const uint64_t timer = game + sym::kOffGameWorldTimer;
  if (ticks) *ticks = Read<uint32_t>(timer);
  if (running) *running = Read<uint8_t>(timer + 4) != 0;
  return true;
}

}  // namespace pstf::engine
