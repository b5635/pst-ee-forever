// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based combat director: the pure, engine-free state machine (BG3-style turn economy on top
// of PST's own rules). The engine adapter feeds it combatants and completion events; it answers
// "who acts now, with what budget" and publishes events for the HUD. It never decides a rule
// outcome (to-hit, damage, saves): the engine does, and reports back.
//
// Turn economy per creature turn: 1 action (attack round, cast, use item, swap, ...), a movement
// budget in pixels, and 1 reaction (opportunity attack) that refreshes at its own turn start.
// An attack spends the action and opens the native attack round: AttacksThisRound swings.
// A turn only ends on an event (EndTurn, or nothing left to spend and nothing in flight),
// never on a timer. Party members in consecutive initiative slots form a group the player can
// switch between (BG3 shared initiative).
#pragma once
#include <cstdint>
#include <vector>

#include "modules/tbc/tbc_rules.h"

namespace pstf::tbc {

enum class Side : uint8_t { Party, Hostile };

struct Recruit {
  int32_t id;
  Side side;
  int dex_reaction_bonus;
  int apr_halves;   // AprHalvesFromStat(engine stat)
  int move_budget;  // pixels per turn
};

struct Combatant {
  Recruit r;
  int roll = 0;
  int initiative = 0;
  uint32_t own_rounds = 0;      // rounds this creature has started (APR alternation)
  uint32_t first_round = 0;     // first round it may act in (late joiners wait)
  int move_left = 0;
  int actions_left = 0;
  int attacks_left = 0;
  bool reaction_used = false;
  bool started = false;         // its turn began this round (budgets granted)
  bool done_this_round = false;
  bool active = true;           // false once dead/fled/handed back
};

enum class EvType : uint8_t {
  CombatStart, RoundStart, TurnStart, TurnEnd, Joined, Left, Swing, Reaction, CombatEnd
};
struct Event {
  EvType type;
  int32_t id;      // creature (or -1)
  int32_t value;   // round number, winner side, swings left...
};

enum class Busy : uint8_t { None, Moving, Attacking, Acting };

class Director {
 public:
  Director(uint32_t seed, uint32_t round_ticks) : rng_(seed), round_ticks_(round_ticks) {}

  // Fight lifecycle.
  void Start(const std::vector<Recruit>& who, uint32_t world_tick);
  void Join(const Recruit& r);
  void Remove(int32_t id);  // died, fled, or handed back (dialogue/cutscene)
  bool Running() const { return running_; }

  // Queries.
  int32_t Current() const { return current_; }
  uint32_t Round() const { return round_; }
  Busy BusyState() const { return busy_; }
  const Combatant* Find(int32_t id) const;
  std::vector<int32_t> Order() const;  // initiative order of active combatants
  uint32_t Clock() const { return CombatClock(start_tick_, rounds_done_, round_ticks_); }
  std::vector<Event> TakeEvents();

  // Commands for the current creature (player input or AI). Return false if not allowed.
  bool SwitchTo(int32_t id);                     // party group only
  bool BeginMove(int32_t id, int pixels);        // reserve movement for a clipped path
  void MoveFinished(int32_t id, int pixels_spent);
  bool BeginSwing(int32_t id);                   // first swing spends the action
  void SwingResolved(int32_t id);
  bool BeginAction(int32_t id);                  // cast / use item / swap / quick slot
  void ActionResolved(int32_t id);
  bool EndTurn(int32_t id);
  bool Reaction(int32_t id);                     // opportunity attack (off-turn, once per own turn)

 private:
  Combatant* Get(int32_t id);
  void Advance();               // pick next slot or start a new round
  void BeginTurn(Combatant& c);
  void Resume(Combatant& c);    // group switch: start or continue its turn
  void FinishTurn(Combatant& c);
  void MaybeAutoEnd(Combatant& c);
  void StartRound();
  bool CheckEnd();
  bool InCurrentGroup(const Combatant& c) const;
  std::vector<Combatant*> Sorted();

  Rng rng_;
  uint32_t round_ticks_;
  uint32_t start_tick_ = 0;
  uint32_t round_ = 0;
  uint32_t rounds_done_ = 0;
  bool running_ = false;
  int32_t current_ = -1;
  Busy busy_ = Busy::None;
  std::vector<Combatant> all_;
  std::vector<Event> events_;
};

}  // namespace pstf::tbc
