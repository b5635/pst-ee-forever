// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based combat: when does the active creature's turn end? (pure, unit-tested)
//
// A turn is one engine combat round of the creature's own activity: while it is the only combatant
// whose ProcessAI runs, the engine executes its actions with its own rules and timing (attack
// round with native APR and weapon speed, casting time, walking speed). Only game ticks in which
// the creature is busy (has a current or queued action) are counted, so a player who is thinking
// spends nothing: the turn waits for orders, as in BG3. The turn ends when
//   - the round budget (ROUND_LENGTH ticks of busy time) is spent, or
//   - the player presses End Turn (handled by the caller), or
//   - an AI creature is idle: it finished what it did, or decided to do nothing, or
//   - an AI creature hits the hard cap (stuck script; never block the fight).
#pragma once
#include <cstdint>

namespace pstf::tbc {

enum class TurnEnd : uint8_t { No, Budget, Idle, Cap };

struct TurnClock {
  static constexpr uint32_t kAiThinkTicks = 30;  // 2 game seconds for the script to pick an action
  static constexpr uint32_t kIdleTicks = 8;      // idle this long after acting = done

  uint32_t budget = 51;
  bool party = false;
  uint32_t busy = 0;    // busy ticks spent
  uint32_t idle = 0;    // consecutive idle ticks
  uint32_t total = 0;   // all ticks since the turn started
  bool acted = false;   // has been busy at least once

  TurnClock() = default;
  TurnClock(uint32_t round_ticks, bool is_party) : budget(round_ticks), party(is_party) {}

  TurnEnd Feed(uint32_t dticks, bool is_busy) {
    total += dticks;
    if (is_busy) {
      busy += dticks;
      idle = 0;
      acted = true;
    } else {
      idle += dticks;
    }
    if (busy >= budget) return TurnEnd::Budget;
    if (party) return TurnEnd::No;
    if (idle >= kIdleTicks && (acted || total >= kAiThinkTicks)) return TurnEnd::Idle;
    if (total >= 3 * budget + kAiThinkTicks) return TurnEnd::Cap;
    return TurnEnd::No;
  }
};

}  // namespace pstf::tbc
