// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based combat: the BG3-style action economy of one creature turn (pure, unit-tested).
//
// A turn has a movement budget in feet (default 31) and ONE action: an attack round, a cast, an item
// use, a talk... anything that is not walking. The engine still runs the action with its own rules
// (APR, weapon speed, casting time, to-hit, damage): we only gate *what* may start and *when* it stops.
//
// Fed once per frame with what the engine shows for the active creature:
//   moved_ft  feet walked since the last frame (position delta on the isometric metric, below)
//   has_path  the engine has a walk path for it (CGameSprite +0x4470)
//   action    its current action id (PST:EE ACTION.IDS; 0 = none)
// Rules:
//   - walking spends movement; when movement is gone the walk is stopped (path dropped) and no new
//     walk may run. A path that still needs movement it does not have is cancelled ("can't reach").
//   - a non-move action that runs while the creature is not walking IS the action of the turn. It
//     ends when the engine finishes it, or after one round of ticks (an attack action repeats rounds
//     forever otherwise; a cast is left to finish, kCastCapRounds at most). Any later non-move action
//     is cancelled ("no action left").
//   - party turns end when the action is used and movement is gone (or on End Turn); AI turns also end
//     once they idle, or at a hard cap.
#pragma once
#include <cmath>
#include <cstdint>

namespace pstf::tbc {

// PST:EE ACTION.IDS (IESDP psteeactions): walking and waiting actions do not use the action.
inline bool IsMoveAction(uint16_t id) {
  switch (id) {
    case 22:   // MoveToObject
    case 23:   // MoveToPoint
    case 29:   // RunAwayFrom
    case 37:   // FollowPath
    case 85:   // RandomWalk
    case 89:   // Follow
    case 90:   // MoveToPointNoRecticle
    case 107:  // MoveToOffset
    case 180:  // MoveToObjectFollow
    case 184:  // RunAwayFromNoInterrupt
    case 207:  // MoveToPointNoInterrupt
    case 208:  // MoveToObjectNoInterrupt
    case 215:  // FollowObjectFormation
    case 297:  // MoveToSavedLocation
    case 386:  // MoveToObjectOffset
    case 108:  // EscapeArea
    case 200:  // RandomWalkContinuous
    case 329:  // TriggerWalkTo
    case 355:  // RunAwayFromNoLeaveArea
    case 397:  // RunToObjectFollow
    case 410:  // RandomWalkTime
    case 411:  // RandomWalkContinuousTime
    case 447:  // TimedMoveToPoint
    case 449:  // ReturnToSavedPlace
      return true;
    default:
      return false;
  }
}
// Script bookkeeping that costs no action (PST:EE ACTION.IDS): NoAction, Help, SetGlobal, Turn,
// Continue, StartTimer, Wait, Face, SmallWait, SetInterrupt, RandomTurn, the DisplayString family,
// WaitRandom.
inline bool IsFreeScriptAction(uint16_t id);
inline bool IsIdleAction(uint16_t id) {
  switch (id) {
    case 0: case 17: case 30: case 33: case 36: case 61: case 63: case 83: case 84: case 86: case 130:
    case 151: case 262: case 269: case 292: case 311: case 342: case 346: case 368:
      return true;
    default:
      return IsFreeScriptAction(id);
  }
}

// Script bookkeeping and cosmetics the engine does at once (instant, no time in real time either): CreateCreature,
// Enemy, ChangeAIScript, IncrementGlobal, DestroySelf, SetGlobalTimer, Polymorph, ChangeEnemyAlly, StartRandomTimer,
// ChangeTeam, BitSet, BitClear, ForceAIScript; GiveItem, PlaySound, CreateItem, DropInventory, ChangeSpecifics,
// ChangeAnimation, FaceObject, PlaySong, StickySinisterPoof, FloatMessage, SetDoorLocked, ChangeColor, SetAnimState,
// PlaySequence. None is the turn's action ("Spell; Wait; IncrementGlobal" steps, Soego's poof-and-recolour before he
// attacks, Shilandra's patrol), and one still waiting at a turn end runs at the creature's next turn.
inline bool IsFreeScriptAction(uint16_t id) {
  switch (id) {
    case 7: case 10: case 15: case 26: case 60: case 82: case 109: case 111: case 115: case 119: case 146: case 153:
    case 157: case 194: case 229: case 317: case 369: case 392: case 404: case 405: case 418: case 425: case 433:
    case 439: case 440: case 453: case 454:
      return true;
    default:
      return false;
  }
}

// Ambient idling from a creature's script (GemRB fork CombatDirector::WaitSeconds, same idea): Wait,
// SmallWait, WaitRandom, RandomWalk, RandomTurn. In an AI's turn it never spends time: before the
// creature has done anything it is replaced by an attack (the nudge), after that it ends the turn.
// Casts the engine queues itself when an item or effect fires a spell (op146 "cast spell": the
// Whispering Flask makes its user ForceSpell Aid SPPR201): ForceSpell, ForceSpellPoint,
// ReallyForceSpell, ReallyForceSpellPoint.
inline bool IsForcedCast(uint16_t id) { return id == 113 || id == 114 || id == 181 || id == 337; }

// Spell casts (PST:EE ACTION.IDS): Spell, SpellPoint, ForceSpell(Point), ReallyForceSpell(Point), SpellNoDec,
// SpellPointNoDec, ReallyForceSpellDead, ForceSpellRange, ForceSpellPointRange, ForceSpellRESNoFeedback. The
// engine ends them itself once the spell went off; a long casting time (Rune of Torment, speed 9) outlasts a round.
inline bool IsCastAction(uint16_t id) {
  switch (id) {
    case 31: case 95: case 113: case 114: case 181: case 191: case 192: case 240: case 318: case 319: case 337:
    case 466:
      return true;
    default:
      return false;
  }
}

inline bool IsWaitAction(uint16_t id) {
  switch (id) {
    case 63: case 83: case 368: case 85: case 130: case 200: case 410: case 411:  // waits, random walks/turns
      return true;
    default:
      return false;
  }
}

// Script sequences across turns (docs/SCENARIO_TEST_PLAN.md, audit finding 1). `q` = current action then the queued
// ones (engine::ActionQueue). An AI creature's turn end keeps what is queued behind the running action for its next
// turn (held creatures run no AI, so nothing of it runs in between): "Spell; Wait(1); IncrementGlobal" goes on with
// the IncrementGlobal. Party members and a creature running our nudge order are cleared as before.
inline bool KeepQueueAtTurnEnd(bool party, bool nudged, const uint16_t* q, int n) {
  if (party || nudged || n < 2) return false;
  for (int i = 1; i < n; ++i)
    if (q[i] != 0) return true;
  return false;
}
// The running action at an AI's turn end is script bookkeeping that has not run (the engine does it at once on its
// next AI update): it stays, with the queue (combat_log.h IsStateAction, instant per IsIdleAction).
inline bool KeepCurrentAtTurnEnd(uint16_t current) {
  return current == 30 || current == 61 || IsFreeScriptAction(current);  // + SetGlobal, StartTimer
}
// A Wait before the creature's first action this turn, with a real action queued behind it: the Wait is skipped
// (time passes in the others' turns) instead of the nudge replacing the whole sequence with an attack.
inline bool SkipLeadWait(const uint16_t* q, int n) {
  if (n < 2 || !IsWaitAction(q[0])) return false;
  for (int i = 1; i < n; ++i)
    if (q[i] != 0 && !IsWaitAction(q[i])) return !IsMoveAction(q[i]);  // a patrol route (Wait; MoveToPoint ...): nudged
  return false;
}

// One search-map cell is 16 x 12 pixels and one foot (Feet2Pixels in GemRB; the engine's own
// "nearby" ellipse dx^2 + 16 dy^2 / 9 uses the same 4:3 aspect).
constexpr double kPxPerFootX = 16.0, kPxPerFootY = 12.0;
inline double FeetBetween(int x1, int y1, int x2, int y2) {
  const double fx = (x2 - x1) / kPxPerFootX, fy = (y2 - y1) / kPxPerFootY;
  return std::sqrt(fx * fx + fy * fy);
}

enum class Stop : uint8_t { None, OutOfMove, NoAction, RoundDone };

struct EconomyStep {
  Stop stop = Stop::None;  // caller: ClearAI(0) + DropPath, tell the player why
  bool hold_walk = false;  // caller: skip AIUpdateWalk for this creature this frame
  bool end_turn = false;   // nothing left to do (or AI idle / cap)
  bool nudge = false;      // AI: its own script chose nothing in time; caller orders an attack (once)
};

struct Economy {
  static constexpr uint32_t kAiThinkTicks = 15;  // AI: 1 s to pick something
  static constexpr uint32_t kIdleTicks = 8;      // AI: idle this long after doing something = done
  static constexpr double kTeleportFt = 20.0;    // a jump this long in one frame is not walking
  static constexpr uint32_t kNudgeTicks = 20;    // AI: after a nudge, this long to start acting

  double max_ft = 31.0;
  double move_ft = 31.0;
  uint32_t round_ticks = 51;
  bool party = false;
  bool action_used = false;
  bool acting = false;      // the action is running now
  uint32_t act_ticks = 0;
  uint32_t total = 0, idle = 0;
  bool did_something = false;
  uint16_t last_action = 0;
  uint32_t since_act = 1000;  // ticks since the action stopped running
  static constexpr uint32_t kFollowUpTicks = 30;  // a forced cast this soon after is part of it
  bool nudged = false;
  // Attack rounds with the engine's own APR (tbc_rules.h AttacksThisRound): when `want_swings` is set
  // (> 0) an attack action ends once that many swings have landed and kSwingTailTicks have passed
  // after the last one (the swing animation finishes), not after a fixed round of ticks; with fewer
  // swings it may run up to two rounds (the engine's next round brings the missing swing).
  static constexpr uint32_t kSwingTailTicks = 8;
  static constexpr uint32_t kAiSettleTicks = 4;  // AI: after its action, this long and the turn is over
  static constexpr uint32_t kCastCapRounds = 4;  // a cast is never cut by the round; this long only if it hangs
  int want_swings = 0;
  uint16_t act_action = 0;  // the action that took the turn's action (3 = Attack)
  int swings = 0;
  uint32_t since_swing = 0;
  void Swing() {
    ++swings;
    since_swing = 0;
  }
  int SwingsLeft() const { return want_swings > swings ? want_swings - swings : 0; }

  Economy() = default;
  Economy(double feet, uint32_t round, bool is_party)
      : max_ft(feet), move_ft(feet), round_ticks(round), party(is_party) {}

  bool MoveLeft() const { return move_ft > 0.05; }
  // The directed AI has not decided yet (its attack has no walk path yet): the action does not start.
  bool defer_act = false;
  // Ran (the action went on movement): an attack order only walks up; between its path parts (no path for a
  // tick or two) it is not an action. No path for kWalkOnlyTicks = it arrived: the action is gone, it stops.
  bool walk_only = false;
  uint32_t pathless = 0;
  static constexpr uint32_t kWalkOnlyTicks = 4;
  double walked_ft = 0;  // feet walked this turn (a run adds movement, so max - left is not it)
  // Panicked, confused or berserk (round_rules.h EngineDrivesAi): the engine's AI moves it (Panic's flee walk comes
  // after the Panic action). Its walk is not held after that action; the turn ends when its feet are spent or it
  // stands idle, not on the settle rule (a panicked thug stood still every turn of Horror).
  bool engine_driven = false;

  // The engine cast a spell for it this turn (its "casts" feedback). A cast with no casting time (Identify,
  // speed 0) starts and ends inside one engine update, so Feed never sees its action: it still spends the action.
  // -> true when this took the action.
  bool SawCast() {
    if (action_used) return false;
    action_used = true;
    act_action = 191;  // SpellNoDec: a cast
    did_something = true;
    return true;
  }

  EconomyStep Feed(uint32_t dt, double moved_ft, bool has_path, uint16_t action) {
    EconomyStep out;
    total += dt;
    since_swing += dt;
    if (!party && IsWaitAction(action) && !engine_driven) {
      // Waits are ignored: no movement, no time. No action yet (it may have walked or fled): it picks
      // one, the nearest foe; acted (also when its script cut a running attack short with a wait),
      // or still waiting after that order: the turn is over (GemRB fork ActStep).
      acting = false;
      out.hold_walk = !MoveLeft();
      if (!action_used && !nudged) {
        nudged = true;
        out.nudge = true;
        idle = 0;
      } else {
        out.end_turn = true;
      }
      last_action = action;
      return out;
    }
    if (moved_ft > 0 && moved_ft < kTeleportFt) {
      walked_ft += moved_ft;
      move_ft = move_ft > moved_ft ? move_ft - moved_ft : 0.0;
      did_something = true;
    }
    const bool walk_action = IsMoveAction(action), idle_action = IsIdleAction(action);
    if (has_path) pathless = 0;
    if (has_path && !MoveLeft()) {
      out.stop = Stop::OutOfMove;  // walking (to a point or to a target) with no feet left
      if (acting) acting = false;   // e.g. chasing a target mid-round: the action stays used
    } else if (!idle_action && !walk_action && !has_path && !defer_act) {
      if (!acting) {
        if (walk_only && MoveLeft() && pathless < kWalkOnlyTicks) {
          ++pathless;  // a runner's attack between its path parts: it walks on
        } else if (action_used && IsForcedCast(action) && since_act <= kFollowUpTicks) {
          acting = true;  // the item's (or spell's) own follow-up cast: the same action goes on
          act_ticks = 0;
        } else if (action_used) {
          out.stop = Stop::NoAction;
        } else {
          acting = true;
          action_used = true;
          act_action = action;
          act_ticks = 0;
          did_something = true;
        }
      }
    }
    if (acting && out.stop == Stop::None) {
      // Walking up to the target (an attack that first closes in) is movement, not the round:
      // the round's ticks count only while the creature stands.
      if (!has_path && !(moved_ft > 0 && moved_ft < kTeleportFt)) act_ticks += dt;
      if (idle_action || walk_action) {
        acting = false;  // the engine finished it (spell cast, item used)
      } else if (want_swings > 0 && swings > 0) {
        if ((swings >= want_swings && since_swing >= kSwingTailTicks) || act_ticks >= 2 * round_ticks) {
          acting = false;
          out.stop = Stop::RoundDone;  // this round's swings are done
        }
      } else if (act_ticks >= (IsCastAction(action) ? kCastCapRounds * round_ticks : round_ticks)) {
        acting = false;
        out.stop = Stop::RoundDone;  // one attack round, then stop repeating (a cast: only if it hangs)
      }
    }
    since_act = acting ? 0 : since_act + dt;
    last_action = action;
    const bool busy = acting || has_path || (!idle_action && out.stop == Stop::None);
    idle = busy ? 0 : idle + dt;
    out.hold_walk = !MoveLeft() || (!party && action_used && !acting && !walk_only && !engine_driven);  // AI: no wandering off after it
    const bool spent = action_used && !acting && !MoveLeft();
    if (party) {
      out.end_turn = spent && !has_path;
    } else {
      if (!did_something && !nudged && total >= kAiThinkTicks) {
        nudged = true;
        out.nudge = true;
        idle = 0;
      }
      // Gave up: nudged and still nothing in hand (no action, no path) for kNudgeTicks.
      const bool gave_up = nudged && !did_something && idle >= kNudgeTicks;
      // Sure of itself: once its action is over, the AI's turn is (no script wandering afterwards).
      if (engine_driven) {
        out.end_turn = (spent && !has_path) || (!MoveLeft() && !has_path) ||
                       (idle >= kIdleTicks && did_something && !acting && !has_path) || gave_up ||
                       total >= 3 * round_ticks + kAiThinkTicks + kNudgeTicks;
        return out;
      }
      out.end_turn = (spent && !has_path) || (action_used && !acting && !walk_only && since_act >= kAiSettleTicks) ||
                     (idle >= kIdleTicks && did_something && !acting) || gave_up ||
                     (out.stop == Stop::OutOfMove && action_used) ||
                     total >= 3 * round_ticks + kAiThinkTicks + kNudgeTicks;
    }
    return out;
  }
};

// A turn that wants to end (End Turn, or the economy is done) waits until its spell has finished for this round:
// the cast animation is over, its projectile has appeared and landed, a landed cloud/swarm has had this round's
// pulse, and every onset that resolves in this turn (Cone of Cold's 100 ticks after landing) has gone off. One
// safety cap for all of it, so a hung spell never freezes the fight: kSpellHoldCapTicks after the end was asked.
enum class SpellWait { None, Casting, Appearing, Flying, Pulsing, Landing, Capped };
struct SpellWaitIn {
  bool casting = false;       // the creature's action is a cast still running (Economy::acting, IsCastAction)
  bool cast_this_turn = false;  // it spent its action on something a projectile can follow (not a melee attack)
  uint32_t since_action = 1000;  // ticks since its action stopped running
  int flying = 0;             // its projectiles in the air
  int lingering = 0;          // its landed clouds/swarms with pulse ticks left this round
  int onsets = 0;             // delayed effects that resolve in this turn
  uint32_t waited = 0;        // ticks the end has waited so far
  uint32_t since_flying = 1000;  // ticks since its last projectile was seen in the air
  bool cinematic = false;     // one of its projectiles owns the engine's game freeze (the world is frozen anyway)
};
inline constexpr uint32_t kSpellAppearTicks = 25;  // a projectile appears up to a second after the cast animation
inline constexpr uint32_t kSpellHoldCapTicks = Economy::kCastCapRounds * 75;  // 4 rounds of 75 ticks
// A projectile's effects reach its target a tick or two after the projectile object is gone (Deathbolt: its death
// effect and save came in the next creature's turn); the turn holds this long after the last one.
inline constexpr uint32_t kSpellLandTicks = 4;
// A cinematic spell (Celestial Host ~40 s in real time) freezes the whole world while it plays: its turn waits for it
// up to this long instead of kSpellHoldCapTicks (the 4-round cap cut Celestial Host at its 13th stage).
inline constexpr uint32_t kCinematicCapTicks = 20 * 75;
inline SpellWait TurnWaitsForSpell(const SpellWaitIn& in) {
  const bool landing = in.cast_this_turn && in.since_flying < kSpellLandTicks;
  const bool any = in.casting || in.flying > 0 || in.lingering > 0 || in.onsets > 0 ||
                   (in.cast_this_turn && in.since_action < kSpellAppearTicks);
  if (!any) return landing ? SpellWait::Landing : SpellWait::None;  // a few ticks, never the cap
  if (in.waited >= (in.cinematic ? kCinematicCapTicks : kSpellHoldCapTicks)) return SpellWait::Capped;
  if (in.casting) return SpellWait::Casting;
  if (in.flying > 0) return SpellWait::Flying;
  if (in.lingering > 0) return SpellWait::Pulsing;
  if (in.onsets > 0 || landing) return SpellWait::Landing;
  return SpellWait::Appearing;
}
inline const char* SpellWaitName(SpellWait w) {
  static const char* kNames[] = {"none", "casting", "appearing", "flying", "pulsing", "landing", "capped"};
  return kNames[static_cast<int>(w)];
}

}  // namespace pstf::tbc
