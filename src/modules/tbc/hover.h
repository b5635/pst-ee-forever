// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based HUD helpers (pure, unit-tested): which creature the cursor is over, the chance to hit
// it, and the dice panel's timing.
#pragma once
#include <cstdint>
#include <vector>

namespace pstf::tbc {

// AD&D 2e as CGameSprite::Hit rolls it (lin 4C29F0): d20 + bonuses >= THAC0 - AC hits; a natural 1
// always misses and a natural 20 always hits, so the chance is 5..95 %.
inline int NeedsToHit(int thac0, int ac) { return thac0 - ac; }
inline int HitChancePercent(int thac0, int ac) {
  int faces = 21 - NeedsToHit(thac0, ac);  // faces of the d20 that hit
  if (faces < 1) faces = 1;
  if (faces > 19) faces = 19;
  return faces * 5;
}

// A creature stands at its feet point; its body is drawn above it. The cursor is over it when it is
// inside a box 26 px each side and from 90 px above to 14 px below the feet (PST character size at
// zoom 1). Of several, the one whose feet are nearest the cursor wins.
struct HoverCandidate {
  int32_t id = -1;
  int x = 0, y = 0;
};
inline int32_t PickHovered(const std::vector<HoverCandidate>& c, int cx, int cy) {
  int32_t best = -1;
  long long best_d = -1;
  for (const auto& h : c) {
    const int dx = cx - h.x, dy = cy - h.y;
    if (dx < -26 || dx > 26 || dy < -90 || dy > 14) continue;
    const long long d = 1LL * dx * dx + 1LL * (dy + 38) * (dy + 38);  // body centre ~38 px up
    if (best_d < 0 || d < best_d) best_d = d, best = h.id;
  }
  return best;
}

// Dice panel: the die tumbles, lands, the verdict shows, then it fades (ms since the roll).
struct DiceTiming {
  static constexpr uint32_t kTumble = 650, kVerdict = 800, kHold = 2600, kFade = 400;
  static bool Visible(uint32_t ms) { return ms < kHold + kFade; }
  static bool Landed(uint32_t ms) { return ms >= kTumble; }
  static bool Verdict(uint32_t ms) { return ms >= kVerdict; }
  static double Alpha(uint32_t ms) { return ms <= kHold ? 1.0 : ms >= kHold + kFade ? 0.0 : 1.0 - (ms - kHold) / double(kFade); }
  // While it tumbles the face shown changes every 60 ms (deterministic from the roll and time).
  static int Face(uint32_t ms, int roll) { return Landed(ms) ? roll : 1 + static_cast<int>((ms / 60 * 7 + roll * 3) % 20); }
};

// Initiative portrait: red fills from the bottom with the share of HP lost (BG3), no bar.
inline double DamageShare(int hp, int max_hp) {
  if (max_hp <= 0) return 0.0;
  if (hp <= 0) return 1.0;
  if (hp >= max_hp) return 0.0;
  return 1.0 - static_cast<double>(hp) / max_hp;
}

}  // namespace pstf::tbc
