// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based combat log: one line per event in <game>\pst_forever\logs\combat.log, made to be read
// after a fight (by a person or an agent):
//   12:15:23.805 w808714 R1 +0012 | TURN   Dak'kon (880088) hp 35/42, move scale 6 -> 15
// Columns: wall clock, world tick, round, world ticks into the current turn, event kind, text.
// Pure formatting here (unit-tested); the module owns the file.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace pstf::tbc {

// PST:EE ACTION.IDS names of the actions a fight shows most; others print as "action N".
inline std::string ActionName(uint16_t id) {
  switch (id) {
    case 0: return "none";
    case 3: return "Attack";
    case 22: return "MoveToObject";
    case 23: return "MoveToPoint";
    case 29: return "RunAwayFrom";
    case 31: return "Spell";
    case 34: return "UseItem";
    case 63: return "Wait";
    case 83: return "SmallWait";
    case 84: return "Face";
    case 85: return "RandomWalk";
    case 86: return "SetInterrupt";
    case 95: return "SpellPoint";
    case 98: return "AttackNoSound";
    case 105: return "AttackOneRound";
    case 113: return "ForceSpell";
    case 114: return "ForceSpellPoint";
    case 130: return "RandomTurn";
    case 134: return "AttackReevaluate";
    case 181: return "ReallyForceSpell";
    case 191: return "SpellNoDec";
    case 337: return "ReallyForceSpellPoint";
    case 355: return "RunAwayFromNoLeaveArea";
    case 368: return "WaitRandom";
    case 401: return "RunningAttackNoSound";
    case 449: return "ReturnToSavedPlace";
    default: return "action " + std::to_string(id);
  }
}

// "12:15:23.805 w808714 R1 +0012 | TURN   text"
inline std::string CombatLogLine(int hh, int mm, int ss, int ms, uint32_t world, uint32_t round, uint32_t into_turn,
                                 const char* kind, const std::string& text) {
  char head[96];
  std::snprintf(head, sizeof head, "%02d:%02d:%02d.%03d w%u R%u +%04u | %-6s ", hh, mm, ss, ms, world, round, into_turn,
                kind);
  return head + text;
}

}  // namespace pstf::tbc
