// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/tick.h"

#include <vector>

#include "core/log.h"
#include "core/patch.h"
#include "engine/symbols.h"

namespace pstf {
namespace {
using SyncFn = void(__fastcall*)(void* chitin);
SyncFn g_sync = nullptr;
std::vector<TickFn> g_handlers;
uint64_t g_frame = 0;
int g_state = 0;  // 0 not tried, 1 installed, -1 failed

void __fastcall FrameTick(void* chitin) {
  g_sync(chitin);
  ++g_frame;
  for (TickFn fn : g_handlers) fn(g_frame);
}

bool Install() {
  for (const auto& s : sym::kAll) {
    if (s.rva != sym::kRvaFrameTickCall) continue;
    const uint64_t original = RedirectCall(s, reinterpret_cast<void*>(&FrameTick));
    if (original != sym::kVaChitinSyncUpdate) {
      Log("tick: frame call site not patched (target %llX)", static_cast<unsigned long long>(original));
      return false;
    }
    g_sync = reinterpret_cast<SyncFn>(original);
    Log("tick: frame tick installed at %llX", static_cast<unsigned long long>(sym::kImageBase + s.rva));
    return true;
  }
  return false;
}
}  // namespace

bool AddTickHandler(TickFn fn) {
  if (g_state == 0) g_state = Install() ? 1 : -1;
  if (g_state < 0) return false;
  g_handlers.push_back(fn);
  return true;
}

}  // namespace pstf
