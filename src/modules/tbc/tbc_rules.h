// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based combat: pure rule helpers (no engine access; unit-tested in tests/unit/test_tbc.cpp).
// Every constant cites the 3.2.2 evidence it comes from. Notes: docs/TBC_RESEARCH.md.
#pragma once
#include <cstdint>
#include <vector>

namespace pstf::tbc {

// --- time -------------------------------------------------------------------------------------
// Game ticks per game second. CGameAIBase::Wait (lin 60E9B0) multiplies seconds by 15; effect
// AddEffect paths divide tick durations by 0xF; CTimerWorld::UpdateTime (lin 345D00, win 1402B3490)
// wraps the day at 0x1A5E0 = 108000 ticks = 24 h * 300 s * 15.
constexpr uint32_t kTicksPerSecond = 15;
constexpr uint32_t kTicksPerDay = 108000;
// CGameAnimationType::ROUND_LENGTH default. CInfGame::LoadOptions (lin 3679E0, win 1402E6680):
// BGGetPrivateProfileInt("Game Options","Round Length", ENGINE_MODE==3 ? 51 : 100);
// PST:EE has engine_mode=3 (engine.lua). Win: 1402E776A `mov eax,33h; cmovz ebp,eax`, stored to
// win 14071F5FC (static initialiser 100 until LoadOptions runs). Baldur.lua can override it.
constexpr uint32_t kRoundTicksPst = 51;
constexpr uint32_t kRoundTicksOtherEE = 100;

// Effect durations are stored in ticks. How many whole combat rounds does a duration last?
// Rounded up: an effect with any time left in a round is still active during that round.
constexpr uint32_t RoundsForTicks(uint32_t ticks, uint32_t round_ticks) {
  return round_ticks == 0 ? 0 : (ticks + round_ticks - 1) / round_ticks;
}
constexpr uint32_t RoundsForSeconds(uint32_t seconds, uint32_t round_ticks) {
  return RoundsForTicks(seconds * kTicksPerSecond, round_ticks);
}

// Combat clock: one round per round end. On handback an actor's tick-based timers are shifted by
// (world_now - combat_clock) so nothing expires or fires early because the world kept running.
constexpr uint32_t CombatClock(uint32_t start_tick, uint32_t rounds_done, uint32_t round_ticks) {
  return start_tick + rounds_done * round_ticks;
}
constexpr int64_t RebaseDelta(uint32_t world_now, uint32_t combat_clock) {
  return static_cast<int64_t>(world_now) - static_cast<int64_t>(combat_clock);
}

// --- attacks per round ------------------------------------------------------------------------
// The engine's short encoding (CDerivedStats::NumAttacksShortToFloat, lin 17EA10):
//   0..5 = that many attacks, 6..10 = 0.5, 1.5, 2.5, 3.5, 4.5 (value - 5.5), > 10 = 0.
//   Negative values mirror (sign kept; we treat them as 0 attacks for scheduling).
// Returns attacks per round times two (so 1.5 APR = 3).
constexpr int AprHalvesFromStat(int16_t v) {
  if (v <= 0) return 0;
  if (v <= 5) return 2 * v;
  if (v <= 10) return 2 * v - 11;
  return 0;
}
// Fractional APR alternates: 3/2 = 2 swings on the creature's 1st, 3rd... round, 1 on the others.
// `round_index` counts the creature's own rounds in this fight from 0.
constexpr int AttacksThisRound(int apr_halves, uint32_t round_index) {
  return apr_halves / 2 + ((apr_halves & 1) && (round_index % 2 == 0) ? 1 : 0);
}

// --- initiative -------------------------------------------------------------------------------
// Rolled once per combat; lower acts first: d10 - dexterity reaction bonus (TURN_BASED plan §1.4).
// Ties: party first (BG3 feel: the player is not robbed of a turn), then lower roll, then id.
constexpr int Initiative(int d10, int dex_reaction_bonus) { return d10 - dex_reaction_bonus; }

struct InitKey {
  int initiative;
  bool party;
  int roll;
  int32_t id;
};
constexpr bool InitBefore(const InitKey& a, const InitKey& b) {
  if (a.initiative != b.initiative) return a.initiative < b.initiative;
  if (a.party != b.party) return a.party;
  if (a.roll != b.roll) return a.roll < b.roll;
  return a.id < b.id;
}

// Deterministic RNG for initiative (seeded per fight; tests pass fixed seeds). xorshift32.
struct Rng {
  uint32_t s;
  explicit constexpr Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
  constexpr uint32_t Next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  constexpr int Die(int sides) { return static_cast<int>(Next() % static_cast<uint32_t>(sides)) + 1; }
};

// --- movement ---------------------------------------------------------------------------------
struct Pt {
  int x, y;
};
constexpr bool operator==(const Pt& a, const Pt& b) { return a.x == b.x && a.y == b.y; }

// Clip a path (engine path points, world pixels) to `budget` pixels of travel. The preview and the
// confirmed walk must use this same result. The last point may be cut mid-segment.
// `spent` receives the length actually used (<= budget).
std::vector<Pt> ClipPath(const std::vector<Pt>& path, double budget, double* spent);

// Opportunity attack: a mover that starts inside `reach` of a hostile and ends outside it.
constexpr bool LeavesReach(Pt from, Pt to, Pt hostile, int reach) {
  auto in = [&](Pt p) {
    const long long dx = p.x - hostile.x, dy = p.y - hostile.y;
    return dx * dx + dy * dy <= static_cast<long long>(reach) * reach;
  };
  return in(from) && !in(to);
}

}  // namespace pstf::tbc
