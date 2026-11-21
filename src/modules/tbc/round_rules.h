// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based effect timing rules (pure, unit-tested).
#pragma once
#include <cstdint>

namespace pstf::tbc {

constexpr int32_t kRoundTicks5s = 75;  // 5 game seconds x 15 ticks: one combat round of effect time

// A delayed effect (duration types 6/7/8) seen for the first time with `left` ticks to its onset:
//   - on a 5-second step (75, 150, ...: PST's own damage-over-time series, e.g. Adder's Kiss' later
//     hits) it is round-clocked: it lands at the start of the victim's turn that many rounds on;
//   - otherwise, if it is less than a round and a half away (Cone of Cold's 100-tick onset, a
//     projectile's impact), it is the spell resolving: it runs on world time now and the caster's turn
//     waits for it.
inline bool OnsetResolvesNow(int32_t left) {
  if (left <= 0) return true;
  if (left >= kRoundTicks5s + kRoundTicks5s / 2) return false;
  const int32_t m = left % kRoundTicks5s;
  const bool on_step = m <= 3 || m >= kRoundTicks5s - 3;  // a tick or two may pass before we see it
  return !on_step;
}

}  // namespace pstf::tbc
