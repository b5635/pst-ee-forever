// SPDX-License-Identifier: GPL-2.0-or-later
// Pure decision logic for examine voice-over (unit-tested, no engine access).
#pragma once
#include <cstdint>

namespace pstf::voice {

constexpr uint32_t kNoStrref = 0xFFFFFFFFu;

struct ReplayGuard {
  uint32_t last_strref = kNoStrref;
  uint64_t busy_until_ms = 0;
};

// Re-clicking the same info point while its line is still playing must not stack a
// second copy. A different line, or the same one after it finished, plays.
inline bool ShouldPlay(ReplayGuard& g, uint32_t strref, uint64_t now_ms, uint32_t duration_ms) {
  if (strref == g.last_strref && now_ms < g.busy_until_ms) return false;
  g.last_strref = strref;
  g.busy_until_ms = now_ms + duration_ms;
  return true;
}

}  // namespace pstf::voice
