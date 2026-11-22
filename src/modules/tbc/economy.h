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
//     forever otherwise). Any later non-move action is cancelled ("no action left").
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
      return true;
    default:
      return false;
  }
}
// Script bookkeeping that costs no action (PST:EE ACTION.IDS): NoAction, Help, SetGlobal, Turn,
// Continue, StartTimer, Wait, Face, SmallWait, SetInterrupt, RandomTurn, the DisplayString family,
// WaitRandom.
inline bool IsIdleAction(uint16_t id) {
  switch (id) {
    case 0: case 17: case 30: case 33: case 36: case 61: case 63: case 83: case 84: case 86: case 130:
    case 151: case 262: case 269: case 292: case 311: case 342: case 346: case 368:
      return true;
    default:
      return false;
  }
}

// Ambient idling from a creature's script (GemRB fork CombatDirector::WaitSeconds, same idea): Wait,
// SmallWait, WaitRandom, RandomWalk, RandomTurn. In an AI's turn it never spends time: before the
// creature has done anything it is replaced by an attack (the nudge), after that it ends the turn.
inline bool IsWaitAction(uint16_t id) { return id == 63 || id == 83 || id == 368 || id == 85 || id == 130; }

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
  bool nudged = false;

  Economy() = default;
  Economy(double feet, uint32_t round, bool is_party)
      : max_ft(feet), move_ft(feet), round_ticks(round), party(is_party) {}

  bool MoveLeft() const { return move_ft > 0.05; }

  EconomyStep Feed(uint32_t dt, double moved_ft, bool has_path, uint16_t action) {
    EconomyStep out;
    total += dt;
    if (!party && IsWaitAction(action)) {
      // Waits are ignored: no movement, no time. Not acted yet: fight instead; acted (also when its
      // script cut a running attack short with a wait): the turn is over (GemRB fork ActStep).
      acting = false;
      out.hold_walk = !MoveLeft();
      if (!did_something && !nudged) {
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
      move_ft = move_ft > moved_ft ? move_ft - moved_ft : 0.0;
      did_something = true;
    }
    const bool walk_action = IsMoveAction(action), idle_action = IsIdleAction(action);
    if (has_path && !MoveLeft()) {
      out.stop = Stop::OutOfMove;  // walking (to a point or to a target) with no feet left
      if (acting) acting = false;   // e.g. chasing a target mid-round: the action stays used
    } else if (!idle_action && !walk_action && !has_path) {
      if (!acting) {
        if (action_used) {
          out.stop = Stop::NoAction;
        } else {
          acting = true;
          action_used = true;
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
      } else if (act_ticks >= round_ticks) {
        acting = false;
        out.stop = Stop::RoundDone;  // one attack round, then stop repeating
      }
    }
    last_action = action;
    const bool busy = acting || has_path || (!idle_action && out.stop == Stop::None);
    idle = busy ? 0 : idle + dt;
    out.hold_walk = !MoveLeft();
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
      out.end_turn = (spent && !has_path) || (action_used && !acting && idle >= kIdleTicks) ||
                     (idle >= kIdleTicks && did_something && !acting) || gave_up ||
                     (out.stop == Stop::OutOfMove && action_used) ||
                     total >= 3 * round_ticks + kAiThinkTicks + kNudgeTicks;
    }
    return out;
  }
};

}  // namespace pstf::tbc
