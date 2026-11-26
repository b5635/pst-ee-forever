// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based combat maneuvers (pure, unit-tested): Run, Withdraw, Block, Trip/Knockdown and attacks of
// opportunity. Rules ported from the GemRB fork (D:\gemrb gemrb/core/TurnBasedCombat/CombatDirector.cpp
// ApplyRun/ApplyWithdraw/ApplyBlock/TryBlockHit/ResolveTakedown/ThreatsTo/ChooseAIManeuvers); the engine
// still makes every roll that a script can see (the attack roll, the save vs death), we only decide which
// maneuver runs and what a blocked or tripping hit does.
//
//   Run       spends the action; remaining movement += max movement (31/31 -> 62/31, 0/31 -> 31/31).
//   Withdraw  the party side only (enemies never withdraw); spends the action; a third of the remaining movement is left (2/3 go); walks (walk animation and speed); no attacks
//             of opportunity until its next turn. Refused with no movement left (a wasted action).
//   Block     fighters (the Nameless One while a fighter; not Morte or Nordom); spends the action; the attacks left this round
//             become block stacks until its next turn. Each melee hit on it uses one stack: a d20 with its
//             own THAC0 vs AC 4 (1 fails, 20 succeeds) that also comes up at least the attack's d20 stops it.
//   Trip      fighters (the Nameless One while a fighter): the first melee swing of the attack round gets -2 to hit
//             (THAC0 + 2); a hit does no damage, the target saves vs death or is knocked down until its
//             next turn (AC -4 = +4 to hit it, opcode 0; it falls with its die cycle and gets up with it
//             played backwards), when it stands up and loses that turn's action. Armed by the button,
//             cancelled by clicking it again (then the swing is an ordinary attack).
//   Knockdown the same for AI creatures outside the party with THAC0 15 or better, without the -2.
//   AoO       a creature that starts a walk in an enemy's melee reach (the engine's own Attack range test,
//             in search-map cells) along a path that leaves it, without Withdraw, first turns toward its way
//             and waits: every such enemy swings once, one after the other, then it walks. A walker that only
//             passes through a reach later on its way provokes nothing. One per reactor until its own
//             next turn, a single swing whatever its APR; prone (asleep) reactors do not react, nor do Nordom
//             (crossbows) and Ignus (fire): neither holds a melee threat.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "modules/tbc/economy.h"

namespace pstf::tbc {

enum class Takedown : uint8_t { None, Trip, Knockdown };

// What one creature carries from its own turn start to the next one.
struct Stance {
  bool ran = false;
  bool withdrawn = false;  // immune to attacks of opportunity; walks
  int block_stacks = 0;
  Takedown takedown = Takedown::None;  // armed for the first swing of the next attack round
  bool prone = false;                  // knocked down (AC -4 = +4 to hit it, lying in its die cycle)
  bool reaction_used = false;          // its attack of opportunity until its next turn
};

// A new turn of its own: budgets and stances go, a prone creature is still prone (it stands up now).
inline void NewTurn(Stance* s) {
  const bool prone = s->prone;
  *s = Stance{};
  s->prone = prone;
}

// CLASS.IDS (PST:EE): FIGHTER 2, PALADIN 6, FIGHTER_MAGE 7, FIGHTER_CLERIC 8, FIGHTER_THIEF 9,
// FIGHTER_MAGE_THIEF 10, RANGER 12, FIGHTER_DRUID 16, FIGHTER_MAGE_CLERIC 17.
inline bool FighterClass(uint8_t cls) {
  switch (cls) {
    case 2: case 6: case 7: case 8: case 9: case 10: case 12: case 16: case 17:
      return true;
    default:
      return false;
  }
}
// SPECIFIC.IDS: NAMELESS_ONE 2, MORTE 9. The Nameless One's CLASS byte is his current class (FIGHTER 2,
// MAGE 1, THIEF 4: the trainers' replies test Class(Protagonist,FIGHTER), DDAKKON/DANNAH/DGRACE), so he
// trips and blocks while he is a fighter, like anyone else.
constexpr uint8_t kSpecificNameless = 2, kSpecificNordom = 4, kSpecificIgnus = 5, kSpecificMorte = 9;
inline bool MayTrip(uint8_t cls, uint8_t /*specific*/) { return FighterClass(cls); }
inline bool MayBlock(uint8_t cls, uint8_t specific) {
  return MayTrip(cls, specific) && specific != kSpecificMorte && specific != kSpecificNordom;
}
// Attacks of opportunity: everyone but Nordom and Ignus (SPECIFIC.IDS 4, 5).
inline bool MayOpportunity(uint8_t specific) { return specific != kSpecificNordom && specific != kSpecificIgnus; }
constexpr int kKnockdownThac0 = 15;
inline bool MayKnockdown(bool party, int thac0) { return !party && thac0 <= kKnockdownThac0; }

// The action is free: not used and nothing running.
inline bool ActionFree(const Economy& e) { return !e.action_used && !e.acting; }

inline bool CanRun(const Economy& e) { return ActionFree(e); }
inline bool ApplyRun(Economy* e, Stance* s) {
  if (!CanRun(*e)) return false;
  e->move_ft += e->max_ft;
  e->action_used = true;
  e->walk_only = true;  // its attack order walks up, no swing
  s->ran = true;
  return true;
}

inline bool CanWithdraw(const Economy& e) { return ActionFree(e) && e.MoveLeft(); }
inline bool ApplyWithdraw(Economy* e, Stance* s) {
  if (!CanWithdraw(*e)) return false;
  e->move_ft /= 3.0;  // 2/3 of what is left go (31 -> 10.3)
  e->action_used = true;
  s->withdrawn = true;
  return true;
}

// attacks_left: the swings this attack round would have (Economy::want_swings; 1 when unknown).
inline bool CanBlock(const Economy& e, const Stance& s, bool may_block, int attacks_left) {
  return may_block && ActionFree(e) && !s.prone && attacks_left > 0;
}
inline bool ApplyBlock(Economy* e, Stance* s, bool may_block, int attacks_left) {
  if (!CanBlock(*e, *s, may_block, attacks_left)) return false;
  s->block_stacks += attacks_left;
  s->takedown = Takedown::None;
  e->action_used = true;
  return true;
}

// Trip is armed before the attack (clicking it again disarms).
inline bool CanTrip(const Economy& e, const Stance& s, bool may_trip) {
  return may_trip && !s.prone && (s.takedown == Takedown::Trip || (ActionFree(e) && s.takedown == Takedown::None));
}
inline bool ToggleTrip(const Economy& e, Stance* s, bool may_trip) {
  if (!CanTrip(e, *s, may_trip)) return false;
  s->takedown = s->takedown == Takedown::Trip ? Takedown::None : Takedown::Trip;
  return true;
}
inline bool ArmKnockdown(const Economy& e, Stance* s, bool may_knockdown) {
  if (!may_knockdown || s->prone || !ActionFree(e) || s->takedown != Takedown::None) return false;
  s->takedown = Takedown::Knockdown;
  return true;
}
// THAC0 added for the takedown swing (THAC0 rules: higher is worse; trip is -2 to hit).
inline int TakedownThac0(Takedown k) { return k == Takedown::Trip ? 2 : 0; }
inline const char* TakedownName(Takedown k) { return k == Takedown::Knockdown ? "Knockdown" : "Trip"; }

// GemRB TryBlockHit (THAC0 rules, IsReverseToHit): the block d20 + AC 4 must beat the defender's
// THAC0 (natural 1 fails, natural 20 succeeds), and come up at least the attacker's d20.
constexpr int kBlockAc = 4;
inline bool BlockRollHits(int block_roll, int defender_thac0) {
  if (block_roll <= 1) return false;
  if (block_roll >= 20) return true;
  return block_roll + kBlockAc > defender_thac0;
}
inline bool BlockStops(int block_roll, int defender_thac0, int attack_roll) {
  return BlockRollHits(block_roll, defender_thac0) && block_roll >= attack_roll;
}

// --- melee reach: CGameSprite::Attack (lin 4ED490 L290-353, win sub_1403FD990 L261-294) -------------------
// Cells are search-map squares (16 x 12 px). range = the weapon ability's range field + 1 (melee 1 -> 2),
// personal spaces of both (the animation's GetPersonalSpace): reach = range + ((ps_a - 1) >> 1) +
// ((ps_b - 1) >> 1) - 1 cells. Melee distance is CAIUtil::CountSquares: the larger of the cell deltas
// (Chebyshev), in reach when reach^2 >= that^2; an attacker already in its attack round (sprite+0x49BC, set
// by Swing) gets RANGE_FUDGE = 1 more cell.
constexpr int kMeleeRangeField = 1;
constexpr int kRangeFudge = 1;
inline int ReachCells(int range_field, int ps_a, int ps_b) {
  return range_field + 1 + ((ps_a - 1) >> 1) + ((ps_b - 1) >> 1) - 1;
}
inline int CellDist2(int ax, int ay, int bx, int by) {
  int dx = ax / 16 - bx / 16, dy = ay / 12 - by / 12;
  dx = dx < 0 ? -dx : dx, dy = dy < 0 ? -dy : dy;
  const int d = dx > dy ? dx : dy;
  return d * d;
}
inline bool InReach(int ax, int ay, int bx, int by, int reach) { return reach * reach >= CellDist2(ax, ay, bx, by); }

// Attack of opportunity on this frame: the mover was in the reactor's reach and is not now.
inline bool ProvokesNow(bool was_in_reach, bool in_reach, bool mover_withdrawn, const Stance& reactor) {
  return was_in_reach && !in_reach && !mover_withdrawn && !reactor.reaction_used && !reactor.prone;
}

// The mover's next steps along its path (path points in world px, from the next one it has not reached;
// the engine keeps waypoints, so a straight stretch is one point far away): does it leave the reactor's
// reach within `ahead_px` of walking? Walked along the segments in 4 px steps. A running step covers more
// than one search cell, so looking only at where it stands lets a runner jump out of reach between checks.
inline bool StepLeavesReach(int rx, int ry, int mx, int my, const std::vector<std::pair<int, int>>& path, int reach,
                            double ahead_px) {
  if (!InReach(rx, ry, mx, my, reach)) return false;
  double walked = 0;
  double px = mx, py = my;
  for (const auto& p : path) {
    const double dx = p.first - px, dy = p.second - py;
    const double len = std::sqrt(dx * dx + dy * dy * 16.0 / 9.0);  // 16 x 12 px cells: y counts 4/3
    const int steps = std::max(1, static_cast<int>(len / 4.0));
    for (int i = 1; i <= steps; ++i) {
      const double t = static_cast<double>(i) / steps;
      if (!InReach(rx, ry, static_cast<int>(px + dx * t), static_cast<int>(py + dy * t), reach)) return true;
      if (walked + len * t >= ahead_px) return false;
    }
    walked += len;
    px = p.first, py = p.second;
  }
  return false;
}
// The whole path (the walk about to start): does it leave the reactor's reach anywhere?
inline bool PathLeavesReach(int rx, int ry, int mx, int my, const std::vector<std::pair<int, int>>& path, int reach) {
  return StepLeavesReach(rx, ry, mx, my, path, reach, 1e9);
}
// The engine's 16 directions (CGameSprite::SetDirection): 0 south, 4 west, 8 north, 12 east (screen y down).
inline int Direction16(int dx, int dy) {
  if (dx == 0 && dy == 0) return 0;
  const double a = std::atan2(static_cast<double>(-dx), static_cast<double>(dy));  // 0 south, +pi/2 west
  int d = static_cast<int>(std::lround(a / (3.14159265358979323846 / 8.0)));
  return ((d % 16) + 16) % 16;
}
// Movers without a walk path (floating) are seen one tick late: in reach last tick, out now but still within
// one more cell (the reactor swings with the engine's RANGE_FUDGE).
inline bool LeftReachLate(bool was_in, bool in_now, bool in_fudge_reach) { return was_in && !in_now && in_fudge_reach; }
constexpr double kAooLookAheadPx = 48.0;  // three cells: more than one running step (15 move scale)

// A reactor in one of these states cannot swing (STATE.IDS: sleeping, panic, stunned, helpless, frozen, stone,
// dead), as GemRB ThreatsTo skips helpless, sleeping and panicked reactors.
constexpr uint32_t kNoReactionStates = 0x1 | 0x4 | 0x8 | 0x20 | 0x40 | 0x80 | 0x800;
inline bool CanReact(int64_t state) { return state >= 0 && (static_cast<uint32_t>(state) & kNoReactionStates) == 0; }

// Sprite updates per tick for a reactor until its swing (presentation pace: the engine still times the swing).
constexpr int kReactionPace = 3;
// The AoO swing in progress: the reactor attacks once, then gives the turn back.
struct Reaction {
  static constexpr uint32_t kTailTicks = 10;   // after its swing, the animation finishes
  static constexpr uint32_t kCapTicks = 90;    // no swing in 6 s (at kReactionPace: a whole attack round): give up
  int32_t reactor = -1, mover = -1;
  uint32_t ticks = 0, since_swing = 0;
  bool swung = false;
  bool Active() const { return reactor != -1; }
  // Per world tick; true = over.
  bool Feed(uint32_t dt, bool swing_now) {
    ticks += dt;
    if (swing_now) swung = true, since_swing = 0;
    else if (swung) since_swing += dt;
    return (swung && since_swing >= kTailTicks) || ticks >= kCapTicks;
  }
};

// --- the trip swing's pace -------------------------------------------------------------------------------
// The armed swing starts (the attacker's sequence turns into an attack): the trip sound plays, the wind-up
// runs at half speed (its sprite updates every other tick) up to the hit; then the follow-through runs 2x
// (two updates every tick) until the attack sequence ends. In game the hit lands at frame 8 of 15: 16 ticks
// of wind-up, then the 7 follow-through frames in 4 ticks (1.5x was 6 ticks: too close to normal to read). Presentation only: the engine's own roll
// decides, and the slowed wind-up shifts the swing in time, not its rules.
struct TripPace {
  enum class Phase : uint8_t { None, Windup, Follow } phase = Phase::None;
  int32_t id = -1;
  uint32_t ticks = 0;
  static constexpr uint32_t kCapTicks = 90;  // never more than 6 s of slowed time
  bool Active() const { return phase != Phase::None; }
  void Start(int32_t who) { phase = Phase::Windup, id = who, ticks = 0; }
  void Resolved() {
    if (phase == Phase::Windup) phase = Phase::Follow, ticks = 0;
  }
  void Stop() { *this = TripPace{}; }
  // Sprite updates for this world tick (0, 1 or 2), and whether the pace is over (attack sequence gone).
  int Updates(bool attacking) {
    if (!Active()) return 1;
    ++ticks;
    if (!attacking || ticks > kCapTicks) {
      Stop();
      return 1;
    }
    if (phase == Phase::Windup) return (ticks & 1) ? 0 : 1;  // half speed
    return 2;                                                  // 2x
  }
};

// --- prone pose: the die cycle, held on its last frame, played backwards to stand up -------------------
// `back` = frames before the last one of the die cycle (engine::PoseLying).
struct PronePose {
  enum class Phase : uint8_t { None, Falling, Down, Rising } phase = Phase::None;
  uint32_t ticks = 0;
  int len = 0;                       // the die cycle's frames (0 until known)
  static constexpr int kTicksPerFrame = 1;
  void Fall() { phase = Phase::Falling, ticks = 0; }
  void Rise() {
    if (phase != Phase::None) phase = Phase::Rising, ticks = 0;
  }
  bool Busy() const { return phase == Phase::Falling || phase == Phase::Rising; }
  // Frames back from the last one for this tick; -1 = no pose (standing).
  int Back() const {
    const int n = len > 0 ? len : 1;
    const int f = static_cast<int>(ticks) / kTicksPerFrame;
    switch (phase) {
      case Phase::Falling: return std::max(0, n - 1 - f);
      case Phase::Down: return 0;
      case Phase::Rising: return f >= n ? -1 : f;
      default: return -1;
    }
  }
  void Tick(int frames) {
    if (frames > 0) len = frames;
    ++ticks;
    if (phase == Phase::Falling && static_cast<int>(ticks) / kTicksPerFrame >= std::max(1, len)) phase = Phase::Down;
    if (phase == Phase::Rising && static_cast<int>(ticks) / kTicksPerFrame > std::max(1, len)) phase = Phase::None;
  }
};

// Knocked down: attackers get +4 to hit (the target's AC worsens by 4: opcode 0 AC modifier -4, all weapon
// types), as long as it lies there (until its next turn start: kProneSeconds of its own round clock).
constexpr int kProneAcPenalty = 4;
constexpr uint32_t kProneSeconds = 5;  // the round clock steps 75 ticks = 5 s at its turn start

// An AI creature walking to attack that runs out of movement with its action still free runs (spends the
// action on the extra movement) instead of stopping short.
inline bool AiRunsWhenShort(const Economy& e, bool attack_intent, bool may_run) {
  return may_run && !e.party && attack_intent && ActionFree(e) && !e.MoveLeft();
}

// --- directed AI: which maneuver an AI creature takes this turn (GemRB ChooseAIManeuvers, extended) ------
enum class AiMove : uint8_t { None, Run, Withdraw, Block, Trip, Knockdown };
struct AiView {
  bool melee_intent = false;  // its action is an attack (it will close in and swing)
  bool flee_intent = false;   // its action walks away (RunAwayFrom...)
  int gap_cells = 0;          // cells to the nearest enemy (Euclidean, cell deltas)
  double path_ft = 0;         // its walk path's length (1 ft = 1 cell; 0 = not walking yet)
  int reach_cells = 2;        // its melee reach to that enemy (ReachCells)
  double move_ft = 31.0;      // movement left (1 ft = 1 cell)
  int adjacent_foes = 0;      // enemies whose reach it stands in
  int hp = 1, max_hp = 1;
  bool may_trip = false, may_block = false, may_knockdown = false;
  bool may_withdraw = false;  // the party side only: enemies never withdraw
  int attacks = 1;            // swings this round
  bool action_free = true;
  int roll100 = 100;          // 1..100, the random part (takedowns 20 %)
};
// The length of a walk path in feet (1 ft = 16 x 12 px, a search cell), from (x, y) along the waypoints.
inline double PathFeet(int x, int y, const std::vector<std::pair<int, int>>& path) {
  double ft = 0, px = x, py = y;
  for (const auto& p : path) {
    const double dx = (p.first - px) / 16.0, dy = (p.second - py) / 12.0;
    ft += std::sqrt(dx * dx + dy * dy);
    px = p.first, py = p.second;
  }
  return ft;
}
constexpr int kAiTakedownPercent = 20;
constexpr int kAiBlockHpPercent = 35;
inline AiMove ChooseAiMove(const AiView& v) {
  if (!v.action_free) return AiMove::None;
  // Walking away from someone in reach: withdraw rather than eat a free swing.
  if (v.may_withdraw && v.flee_intent && v.adjacent_foes > 0 && v.move_ft > 0.05) return AiMove::Withdraw;
  if (!v.melee_intent) return AiMove::None;
  const bool in_reach = v.gap_cells <= v.reach_cells;
  // Run only when a walk cannot close the gap this turn (it then spends the action on the run): the walk path
  // it took (around walls and other creatures), else the straight gap.
  const double need = v.path_ft > 0 ? v.path_ft - (v.reach_cells - 1) : static_cast<double>(v.gap_cells - v.reach_cells);
  if (!in_reach && need > v.move_ft) return AiMove::Run;
  if (!in_reach) return AiMove::None;
  // Hurt and surrounded: a fighter turns its swings into blocks.
  if (v.may_block && v.adjacent_foes >= 2 && v.hp * 100 < kAiBlockHpPercent * v.max_hp && v.attacks > 0)
    return AiMove::Block;
  if (v.roll100 <= kAiTakedownPercent) {
    if (v.may_knockdown) return AiMove::Knockdown;
    if (v.may_trip) return AiMove::Trip;
  }
  return AiMove::None;
}

// PST:EE ACTION.IDS: RunAwayFrom 29, RunAwayFromNoInterrupt 184, RunAwayFromNoInterruptNoLeaveArea 354,
// RunAwayFromNoLeaveArea 355.
inline bool IsFleeAction(uint16_t id) { return id == 29 || id == 184 || id == 354 || id == 355; }
// Attack 3, GroupAttack 94, AttackNoSound 98, AttackOneRound 105, AttackReevaluate 134, RunningAttack 400,
// RunningAttackNoSound 401.
inline bool IsAttackAction(uint16_t id) {
  return id == 3 || id == 94 || id == 98 || id == 105 || id == 134 || id == 400 || id == 401;
}

}  // namespace pstf::tbc
