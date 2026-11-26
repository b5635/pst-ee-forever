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
    case 7: return "CreateCreature";
    case 8: return "Dialogue";
    case 10: return "Enemy";
    case 17: return "Help";
    case 30: return "SetGlobal";
    case 36: return "Continue";
    case 60: return "ChangeAIScript";
    case 61: return "StartTimer";
    case 108: return "EscapeArea";
    case 109: return "IncrementGlobal";
    case 111: return "DestroySelf";
    case 115: return "SetGlobalTimer";
    case 146: return "Polymorph";
    case 153: return "ChangeEnemyAlly";
    case 229: return "FaceObject";
    case 369: return "StartRandomTimer";
    case 392: return "ChangeTeam";
    case 400: return "RunningAttack";
    case 404: return "BitSet";
    case 418: return "StickySinisterPoof";
    case 425: return "FloatMessage";
    case 433: return "ForceAIScript";
    case 454: return "PlaySequence";
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

// Script bookkeeping a dropped queue loses for good (script audit, docs/SCENARIO_TEST_PLAN.md): variables,
// script/side changes, spawns, self-removal, dialogue. When an AI turn ends with one of these still queued, a
// real-time script's sequence (Spell; Wait; IncrementGlobal) would never finish.
inline bool IsStateAction(uint16_t id) {
  switch (id) {
    case 7: case 8: case 10: case 30: case 60: case 61: case 109: case 111: case 115: case 146: case 153:
    case 369: case 392: case 404: case 405: case 418: case 433: case 438:
      return true;
    default:
      return false;
  }
}
// "Spell, Wait, IncrementGlobal" of a queue (current action first); `lost` = how many are IsStateAction.
inline std::string QueueText(const uint16_t* ids, int n, int* lost = nullptr) {
  std::string t;
  int l = 0;
  for (int i = 0; i < n; ++i) {
    if (i) t += ", ";
    t += ActionName(ids[i]);
    if (IsStateAction(ids[i])) ++l;
  }
  if (lost) *lost = l;
  return t;
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
