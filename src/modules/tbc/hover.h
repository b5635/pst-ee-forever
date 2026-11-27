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

// The creature the hover card is about: the one the engine picked under the mouse (engine::PickedId, the object a
// click selects or attacks: the cursor is inside its selection circle), when it is one of the candidates (alive in
// the leader's area; combatants only in a fight). -1 otherwise (nothing, a door, a container, a bystander).
inline int32_t HoverPick(const std::vector<int32_t>& candidates, int32_t picked) {
  if (picked == -1) return -1;
  for (int32_t id : candidates)
    if (id == picked) return id;
  return -1;
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

// Hover card (BG3: "Anders Lv. 5" over "Sword of Tyr"): the level is the highest of a creature's class levels
// (STATS.IDS LEVEL/LEVEL2/LEVEL3: the Nameless One keeps his apart); the line under the name is a party member's
// class (CLASS.IDS, PST calls clerics priests), else how the creature stands towards the party.
inline int HoverLevel(int l1, int l2, int l3) {
  int l = l1 > l2 ? l1 : l2;
  return l > l3 ? l : l3;
}
inline const char* HoverSubtitle(int cls, uint8_t ea, bool party) {
  if (party) {
    switch (cls) {
      case 1: return "Mage";
      case 2: return "Fighter";
      case 3: return "Priest";
      case 4: return "Thief";
      case 7: return "Fighter / Mage";
      case 8: return "Fighter / Priest";
      case 9: return "Fighter / Thief";
      case 13: return "Mage / Thief";
      default: break;
    }
    return "Companion";
  }
  if (ea >= 200) return "Hostile";
  if (ea <= 30) return "Ally";
  return "Neutral";
}

}  // namespace pstf::tbc
