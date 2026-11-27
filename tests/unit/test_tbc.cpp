// SPDX-License-Identifier: GPL-2.0-or-later
// L0 tests for the turn-based combat core. Exact values come from the 3.2.2 evidence cited in
// src/modules/tbc/tbc_rules.h and docs/TBC_RESEARCH.md.
#include <algorithm>

#include "mini_test.h"
#include "modules/tbc/director.h"
#include "modules/tbc/tbc_rules.h"
#include "modules/tbc/tbc_sim.h"

using namespace pstf::tbc;

namespace {
bool Has(const std::vector<Event>& ev, EvType t, int32_t id = -2) {
  return std::any_of(ev.begin(), ev.end(),
                     [&](const Event& e) { return e.type == t && (id == -2 || e.id == id); });
}
// Dex bonus so large that the d10 cannot change the order: lower initiative acts first.
Recruit P(int32_t id, int dex, int apr2 = 2) { return {id, Side::Party, dex, apr2, 100}; }
Recruit H(int32_t id, int dex, int apr2 = 2) { return {id, Side::Hostile, dex, apr2, 100}; }
}  // namespace

TEST_CASE("tbc time: PST round is 51 ticks, 15 ticks per second, day 108000") {
  CHECK(kRoundTicksPst == 51);
  CHECK(kTicksPerDay == 24u * 300u * kTicksPerSecond);
  // 15 s effect = 225 ticks = 4.41 rounds -> active during 5 rounds.
  CHECK(RoundsForSeconds(15, kRoundTicksPst) == 5);
  CHECK(RoundsForTicks(51, 51) == 1);
  CHECK(RoundsForTicks(52, 51) == 2);
  CHECK(RoundsForTicks(0, 51) == 0);
  CHECK(RoundsForSeconds(6, kRoundTicksOtherEE) == 1);  // BG:EE: 90 ticks < 100
  CHECK(CombatClock(1000, 3, 51) == 1153);
  CHECK(RebaseDelta(2000, 1153) == 847);
}

TEST_CASE("tbc APR: engine short encoding and 3/2 alternation") {
  CHECK(AprHalvesFromStat(0) == 0);
  CHECK(AprHalvesFromStat(1) == 2);
  CHECK(AprHalvesFromStat(5) == 10);
  CHECK(AprHalvesFromStat(6) == 1);   // 0.5
  CHECK(AprHalvesFromStat(7) == 3);   // 1.5
  CHECK(AprHalvesFromStat(10) == 9);  // 4.5
  CHECK(AprHalvesFromStat(11) == 0);
  CHECK(AprHalvesFromStat(-2) == 0);
  CHECK(AttacksThisRound(3, 0) == 2);
  CHECK(AttacksThisRound(3, 1) == 1);
  CHECK(AttacksThisRound(3, 2) == 2);
  CHECK(AttacksThisRound(1, 0) == 1);
  CHECK(AttacksThisRound(1, 1) == 0);
  CHECK(AttacksThisRound(4, 7) == 2);
}

TEST_CASE("tbc initiative: lower first, party wins ties, rng deterministic") {
  CHECK(Initiative(7, 2) == 5);
  CHECK(InitBefore({3, false, 5, 1}, {4, true, 1, 0}));
  CHECK(InitBefore({4, true, 9, 9}, {4, false, 1, 0}));
  CHECK(InitBefore({4, false, 2, 9}, {4, false, 3, 0}));
  CHECK(InitBefore({4, false, 3, 1}, {4, false, 3, 2}));
  Rng a(42), b(42);
  bool in_range = true;
  for (int i = 0; i < 1000; ++i) {
    int d = a.Die(10);
    in_range = in_range && d >= 1 && d <= 10 && d == b.Die(10);
  }
  CHECK(in_range);
}

TEST_CASE("tbc path clipping: prefix within budget, partial last segment") {
  double spent = 0;
  auto p = ClipPath({{0, 0}, {30, 40}, {30, 140}}, 100, &spent);  // 50 + 100
  CHECK(p.size() == 3);
  CHECK(p[1] == (Pt{30, 40}));
  CHECK(p[2] == (Pt{30, 90}));
  CHECK(spent == 100);
  auto all = ClipPath({{0, 0}, {30, 40}}, 500, &spent);
  CHECK(all.size() == 2 && spent == 50);
  auto none = ClipPath({{5, 5}, {50, 5}}, 0, &spent);
  CHECK(none.size() == 1 && spent == 0);
}

TEST_CASE("tbc opportunity: leaving reach triggers, staying or entering does not") {
  CHECK(LeavesReach({0, 0}, {100, 0}, {10, 0}, 30));
  CHECK(!LeavesReach({0, 0}, {20, 0}, {10, 0}, 30));
  CHECK(!LeavesReach({100, 0}, {0, 0}, {10, 0}, 30));
}

TEST_CASE("tbc director: order, budgets, turn ends only on events") {
  Director d(1, kRoundTicksPst);
  d.Start({H(10, -50), P(1, 50), P(2, -20)}, 5000);
  CHECK(d.Running());
  CHECK((d.Order() == std::vector<int32_t>{1, 2, 10}));
  CHECK(d.Current() == 1);
  CHECK(d.Round() == 1);
  auto ev = d.TakeEvents();
  CHECK(Has(ev, EvType::CombatStart) && Has(ev, EvType::RoundStart) && Has(ev, EvType::TurnStart, 1));

  CHECK(!d.BeginMove(1, 101));  // over budget
  CHECK(d.BeginMove(1, 60));
  CHECK(!d.EndTurn(1));         // never end mid-walk
  CHECK(!d.BeginSwing(1));      // one thing at a time
  d.MoveFinished(1, 60);
  CHECK(d.Find(1)->move_left == 40);
  CHECK(d.BeginSwing(1));       // 1 APR: action spent, 0 swings left
  CHECK(d.Find(1)->actions_left == 0);
  CHECK(!d.BeginAction(1));
  d.SwingResolved(1);
  CHECK(d.Current() == 1);      // movement left: turn stays open
  CHECK(d.BeginMove(1, 40));
  d.MoveFinished(1, 40);        // nothing left -> auto end, next in group
  CHECK(d.Current() == 2);
  CHECK(d.EndTurn(2));
  CHECK(d.Current() == 10);
  CHECK(d.EndTurn(10));
  CHECK(d.Round() == 2);
  CHECK(d.Current() == 1);
  CHECK(d.Clock() == 5000 + 51);
}

TEST_CASE("tbc director: a combatant changing sides keeps its turn; an empty side ends the fight") {
  Director d(1, kRoundTicksPst);
  d.Start({H(10, -50), P(1, 50), P(20, 10)}, 5000);  // 20: a neutral that joined on the party's side
  CHECK((d.Order() == std::vector<int32_t>{1, 20, 10}));
  CHECK(d.EndTurn(1));
  CHECK(d.Current() == 20);
  CHECK(!d.SetSide(20, Side::Party));   // same side: nothing
  CHECK(!d.SetSide(99, Side::Hostile)); // not in the fight
  CHECK(d.SetSide(20, Side::Hostile));  // its script turned it hostile on its own turn
  CHECK(d.Running() && d.Current() == 20 && d.Find(20)->r.side == Side::Hostile);
  CHECK(d.EndTurn(20));
  CHECK(d.Current() == 10);
  d.Remove(10);
  CHECK(d.Running());                   // 20 still fights the party
  CHECK(d.SetSide(20, Side::Party));    // ... until it is friendly again: no hostile left
  CHECK(!d.Running());
}

TEST_CASE("tbc director: 3/2 APR gives 2 swings in round 1, 1 in round 2") {
  Director d(7, kRoundTicksPst);
  d.Start({P(1, 50, 3), H(10, -50)}, 0);
  CHECK(d.BeginSwing(1));
  d.SwingResolved(1);
  CHECK(d.Current() == 1);
  CHECK(d.Find(1)->attacks_left == 1);
  CHECK(d.BeginSwing(1));
  d.SwingResolved(1);
  CHECK(d.EndTurn(1));
  CHECK(d.EndTurn(10));
  CHECK(d.Round() == 2);
  CHECK(d.BeginSwing(1));
  CHECK(d.Find(1)->attacks_left == 0);
  d.SwingResolved(1);
  CHECK(!d.BeginSwing(1));
}

TEST_CASE("tbc director: party group switching keeps unspent budgets") {
  Director d(3, kRoundTicksPst);
  d.Start({P(1, 50), P(2, 40), H(10, -50)}, 0);
  CHECK(d.Current() == 1);
  CHECK(d.BeginMove(1, 30));
  CHECK(!d.SwitchTo(2));         // busy
  d.MoveFinished(1, 30);
  CHECK(d.SwitchTo(2));
  CHECK(d.Current() == 2);
  CHECK(!d.SwitchTo(10));        // not party
  CHECK(d.EndTurn(2));
  CHECK(d.Current() == 1);       // back to the member with budget left
  CHECK(d.Find(1)->move_left == 70);
  CHECK(d.EndTurn(1));
  CHECK(d.Current() == 10);
}

TEST_CASE("tbc director: hostile between party members splits the groups") {
  Director d(3, kRoundTicksPst);
  d.Start({P(1, 50), H(10, 0), P(2, -50)}, 0);
  CHECK((d.Order() == std::vector<int32_t>{1, 10, 2}));
  CHECK(!d.SwitchTo(2));
}

TEST_CASE("tbc director: reactions once per own turn, joiners wait if slot passed") {
  Director d(9, kRoundTicksPst);
  d.Start({P(1, 0), H(10, -50)}, 0);
  CHECK(d.Current() == 1);
  CHECK(d.Reaction(10));
  CHECK(!d.Reaction(10));
  CHECK(!d.Reaction(1));         // the mover cannot react on its own turn
  d.Join(H(11, 50));             // initiative far below current: slot passed
  CHECK(d.Find(11)->first_round == 2);
  d.Join(H(12, -60));            // acts later this round
  CHECK(d.Find(12)->first_round == 1);
  CHECK(d.EndTurn(1));
  CHECK(d.Current() == 10);
  CHECK(d.EndTurn(10));
  CHECK(d.Current() == 12);
  CHECK(d.EndTurn(12));
  CHECK(d.Round() == 2);
  CHECK(d.Current() == 11);      // now first in order
  CHECK(d.EndTurn(11));
  CHECK(d.EndTurn(1));
  CHECK(d.Current() == 10);
  CHECK(!d.Find(10)->reaction_used);  // refreshed at its turn start
}

TEST_CASE("tbc director: death ends the fight, removal of current advances") {
  Director d(5, kRoundTicksPst);
  d.Start({P(1, 50), H(10, 0), H(11, -50)}, 0);
  d.Remove(1);                   // party gone -> end, hostile side wins
  auto ev = d.TakeEvents();
  CHECK(!d.Running());
  CHECK(Has(ev, EvType::CombatEnd));
  CHECK(ev.back().value == 1);

  Director e(5, kRoundTicksPst);
  e.Start({P(1, 50), H(10, 0), H(11, -50)}, 0);
  CHECK(e.EndTurn(1));
  CHECK(e.Current() == 10);
  e.Remove(10);                  // fled on its own turn
  CHECK(e.Running());
  CHECK(e.Current() == 11);
  e.Remove(11);
  CHECK(!e.Running());
  CHECK(e.TakeEvents().back().value == 0);  // party wins

  Director f(5, kRoundTicksPst);
  f.Start({P(1, 0)}, 0);         // nobody to fight: ends at once
  CHECK(!f.Running());
}

TEST_CASE("tbc sim: same seed, same fight; party wins with PST round length") {
  auto a = RunSim(1, kRoundTicksPst), b = RunSim(1, kRoundTicksPst);
  CHECK(a.winner == 0);
  CHECK(a.Summary() == b.Summary());
  CHECK(a.clock_ticks == (a.rounds - 1) * kRoundTicksPst);
  CHECK(a.swings >= 6);  // 6 hostile hp, every party swing hits
}

#include "modules/dev/autoload_logic.h"

TEST_CASE("dev autoload: continue after a stable start screen, script after a stable world") {
  using pstf::dev::Act;
  using pstf::dev::Autoload;
  using pstf::dev::Scr;
  Autoload a;
  for (uint32_t i = 0; i < 10; ++i) CHECK(a.Step(Scr::Other) == Act::None);  // movies, splash
  for (uint32_t i = 1; i < Autoload::kStartStable; ++i) CHECK(a.Step(Scr::Start) == Act::None);
  CHECK(a.Step(Scr::Start) == Act::Continue);
  for (int i = 0; i < 20; ++i) CHECK(a.Step(Scr::Load) == Act::None);
  CHECK(a.Step(Scr::World) == Act::None);
  CHECK(a.Step(Scr::Other) == Act::None);  // a flicker resets the count
  for (uint32_t i = 1; i < Autoload::kWorldStable; ++i) CHECK(a.Step(Scr::World) == Act::None);
  CHECK(a.Step(Scr::World) == Act::RunScript);
  CHECK(a.Step(Scr::World) == Act::None);  // once only

  Autoload b;
  Act last = Act::None;
  for (uint32_t i = 0; i <= Autoload::kTimeout; ++i) last = b.Step(Scr::Other);
  CHECK(last == Act::Fail);
  Autoload c;
  for (uint32_t i = 0; i < Autoload::kStartStable; ++i) c.Step(Scr::Start);
  c.ContinueFailed();
  CHECK(c.Step(Scr::World) == Act::None);
}

#include "modules/tbc/census.h"
#include "modules/tbc/turn_clock.h"

TEST_CASE("tbc census: start, join, leave") {
  Radii r;  // 450 / 650 / 1100
  std::vector<Seen> w = {
      {1, true, 2, true, 1000, 1000},     // party (EA PC)
      {2, true, 2, true, 1040, 1000},     // party
      {10, false, 255, true, 1300, 1000}, // hostile 300 px away: inside start radius
      {11, false, 255, true, 1600, 1000}, // hostile 600: joins, does not start
      {12, false, 128, true, 1010, 1000}, // neutral next to us: never joins
      {13, false, 255, false, 1001, 1000},// dead hostile
      {14, false, 255, true, 3000, 1000}, // far hostile
  };
  auto c = Census(w, {}, false, r);
  CHECK(c.start);
  std::vector<int32_t> ids;
  for (auto& s : c.join) ids.push_back(s.id);
  std::sort(ids.begin(), ids.end());
  CHECK((ids == std::vector<int32_t>{1, 2, 10, 11}));

  w[2].x = 1600;  // nearest hostile now 600 away: no fight yet
  CHECK(!Census(w, {}, false, r).start);
  // vertical distance counts 4/3 more (isometric): 340 px up = 453 on the ellipse
  CHECK(!Near(0, 0, 0, 340, 450));
  CHECK(Near(0, 0, 0, 337, 450));

  // running: 10 died, 14 fled out of range was never in; 11 runs off beyond 1100
  w[2].alive = false;
  w[3].x = 2200;
  auto d = Census(w, {1, 2, 10, 11}, true, r);
  std::sort(d.leave.begin(), d.leave.end());
  CHECK((d.leave == std::vector<int32_t>{10, 11}));
  CHECK(d.join.empty());
  // a party member who left the area (not in the list) leaves too
  auto e = Census({w[0]}, {1, 2}, true, r);
  CHECK((e.leave == std::vector<int32_t>{2}));
}

TEST_CASE("tbc census: a neutral fighting a combatant joins on the other side") {
  Radii r;
  auto find = [](const CensusOut& c, int32_t id) -> const Seen* {
    for (const auto& s : c.join)
      if (s.id == id) return &s;
    return nullptr;
  };
  std::vector<Seen> w = {
      {1, true, 2, true, 1000, 1000},
      {10, false, 255, true, 1300, 1000},                 // hostile: starts the fight
      {20, false, 128, true, 1350, 1000, 10, true},        // neutral attacking the hostile
      {21, false, 128, true, 1100, 1000, 10, false},       // neutral whose last target was it, now idle
      {22, false, 128, true, 1400, 1000, 23, true},        // neutral attacking neutral 23 ...
      {23, false, 128, true, 1450, 1000, 10, true},        // ... who attacks the hostile: both join
      {24, false, 128, true, 1500, 1000, 25, true},        // neutral vs neutral, nobody in the fight
      {25, false, 128, true, 1550, 1000, 24, true},
      {26, false, 128, true, 2500, 1000, 10, true},        // fights the hostile but beyond the join radius
  };
  auto c = Census(w, {}, false, r);
  CHECK(c.start);
  CHECK(find(c, 20) && find(c, 20)->ally);
  CHECK(!find(c, 21));
  CHECK(find(c, 23) && find(c, 23)->ally);
  CHECK(find(c, 22) && !find(c, 22)->ally);  // it attacks the party's side
  CHECK(!find(c, 24) && !find(c, 25) && !find(c, 26));
  CHECK(c.join.size() == 5);  // 1, 10, 20, 22, 23

  // running: the hostile (10, hostile side) attacks neutral 21 on its turn: 21 joins on the party side;
  // a neutral attacking the party (1) joins on the hostile side
  w[1].target = 21, w[1].fighting = true;
  w[7] = {27, false, 128, true, 1050, 1000, 1, true};
  auto d = Census(w, {1, 10, 20, 22, 23}, true, r, {10, 22});
  CHECK(find(d, 21) && find(d, 21)->ally);
  CHECK(find(d, 27) && !find(d, 27)->ally);
  CHECK(!find(d, 24));
  CHECK(d.join.size() == 2);
  // nobody fighting: no neutral joins
  for (auto& s : w) s.fighting = false;
  CHECK(Census(w, {1, 10}, true, r, {10}).join.empty());
}

TEST_CASE("tbc turn clock: budget of busy ticks, AI idle end, party waits") {
  TurnClock ai(51, false);
  CHECK(ai.Feed(10, false) == TurnEnd::No);  // thinking
  CHECK(ai.Feed(10, true) == TurnEnd::No);   // attacking
  CHECK(ai.Feed(5, false) == TurnEnd::No);
  CHECK(ai.Feed(3, false) == TurnEnd::Idle); // 8 idle ticks after acting
  TurnClock lazy(51, false);
  CHECK(lazy.Feed(29, false) == TurnEnd::No);
  CHECK(lazy.Feed(1, false) == TurnEnd::Idle);  // nothing chosen in 2 s
  TurnClock busy(51, false);
  CHECK(busy.Feed(50, true) == TurnEnd::No);
  CHECK(busy.Feed(1, true) == TurnEnd::Budget);
  TurnClock pc(51, true);
  CHECK(pc.Feed(10000, false) == TurnEnd::No);  // the player thinks for free
  CHECK(pc.Feed(30, true) == TurnEnd::No);
  CHECK(pc.Feed(500, false) == TurnEnd::No);   // walked, now waits for more orders
  CHECK(pc.Feed(21, true) == TurnEnd::Budget);
  CHECK(pc.busy == 51);
}

// ---- action economy, movement line, HUD text ---------------------------------------------------
#include "modules/tbc/economy.h"
#include "modules/tbc/combat_log.h"
#include "modules/tbc/hover.h"
#include "modules/tbc/round_rules.h"
#include "modules/tbc/hud.h"
#include "modules/tbc/path_line.h"

TEST_CASE("tbc economy: feet on the 16x12 cell metric") {
  CHECK(FeetBetween(0, 0, 16, 0) == 1.0);
  CHECK(FeetBetween(0, 0, 0, 12) == 1.0);
  CHECK(FeetBetween(0, 0, 48, 48) == 5.0);  // 3 ft across, 4 ft down
  CHECK(IsMoveAction(23) && IsMoveAction(22) && !IsMoveAction(3) && !IsMoveAction(31));
  CHECK(IsIdleAction(0) && !IsIdleAction(3));
}

TEST_CASE("tbc economy: walking spends feet and stops when they are gone") {
  Economy e(31, 51, true);
  auto st = e.Feed(1, 10, true, 23);  // MoveToPoint, 10 ft
  CHECK(st.stop == Stop::None && !st.hold_walk && !st.end_turn);
  CHECK(e.move_ft == 21.0);
  e.Feed(1, 15, true, 23);
  st = e.Feed(1, 10, true, 23);  // overshoot: clamp at 0, path still wants more
  CHECK(e.move_ft == 0.0);
  CHECK(st.stop == Stop::OutOfMove && st.hold_walk);
  CHECK(!e.action_used);
  st = e.Feed(1, 0, false, 0);  // cleared; action still free: party turn goes on
  CHECK(!st.end_turn);
  CHECK(e.Feed(1, 40, false, 0).hold_walk);  // a teleport-sized jump is not walking
  CHECK(e.move_ft == 0.0);
}

TEST_CASE("tbc economy: one action, one attack round, then nothing else") {
  Economy e(31, 51, true);
  auto st = e.Feed(1, 3, true, 3);  // Attack: walking up to the target, not the action yet
  CHECK(!e.action_used);
  st = e.Feed(10, 0, false, 3);  // in reach: swinging
  CHECK(e.action_used && e.acting && st.stop == Stop::None);
  st = e.Feed(40, 0, false, 3);  // 50 ticks
  CHECK(e.acting);
  st = e.Feed(1, 0, false, 3);  // 51 = one round
  CHECK(st.stop == Stop::RoundDone && !e.acting);
  st = e.Feed(1, 0, false, 0);
  CHECK(!st.end_turn);  // 28 ft of movement left
  st = e.Feed(1, 0, false, 31);  // Spell: no action left
  CHECK(st.stop == Stop::NoAction);
  e.Feed(1, 15, true, 23);
  st = e.Feed(1, 15, true, 23);  // walk the rest
  CHECK(st.stop == Stop::OutOfMove);
  st = e.Feed(1, 0, false, 0);
  CHECK(st.end_turn);
}

TEST_CASE("tbc economy: a cast ends when the engine finishes it") {
  Economy e(31, 51, true);
  e.Feed(1, 0, false, 31);
  CHECK(e.acting);
  auto st = e.Feed(20, 0, false, 0);  // casting done after 20 ticks
  CHECK(!e.acting && e.action_used && st.stop == Stop::None);
}

TEST_CASE("tbc economy: AI turns end after acting or idling, never hang") {
  Economy a(31, 51, false);
  a.Feed(1, 0, false, 3);
  auto st = a.Feed(51, 0, false, 3);
  CHECK(st.stop == Stop::RoundDone);
  st = a.Feed(8, 0, false, 0);
  CHECK(st.end_turn);
  Economy idle(31, 51, false);
  CHECK(!idle.Feed(14, 0, false, 0).end_turn);
  auto n = idle.Feed(1, 0, false, 0);  // 15 ticks of thinking, nothing chosen: nudge, once
  CHECK(n.nudge && !n.end_turn);
  CHECK(!idle.Feed(19, 0, false, 0).nudge);
  CHECK(idle.Feed(1, 0, false, 0).end_turn);  // nudged, still nothing after 20 ticks
  Economy stuck(31, 51, false);
  bool ended = false;
  for (int i = 0; i < 200 && !ended; ++i) ended = stuck.Feed(1, 0, true, 23).end_turn;  // path never walked
  CHECK(ended);
}

TEST_CASE("tbc economy: a panicked creature's flee walk is its turn (engine-driven AI)") {
  // Horror on a thug: the engine's actions were Panic 24, 184, then a 354 walk; the settle rule held the walk
  Economy held(31, 51, false);
  held.Feed(1, 0, false, 24);
  EconomyStep st;
  for (int i = 0; i < 10; ++i) st = held.Feed(1, 0, false, 0);
  st = held.Feed(1, 0, true, 354);
  CHECK(st.hold_walk || st.end_turn);  // a normal AI: no wandering after its action
  Economy p(31, 51, false);
  p.engine_driven = true;
  p.Feed(1, 0, false, 24);
  bool ended = false;
  double walked = 0;
  for (int i = 0; i < 200 && !ended; ++i) {
    st = p.Feed(1, 0.5, true, 354);
    CHECK(!st.hold_walk || !p.MoveLeft());
    walked = p.walked_ft;
    ended = st.end_turn;
  }
  CHECK(walked > 30.9);  // it fled its whole movement
  st = p.Feed(1, 0, false, 354);
  CHECK(st.end_turn);
  Economy wander(31, 51, false);  // confused, "wander away": its RandomWalk is a walk, not an ignored wait
  wander.engine_driven = true;
  st = wander.Feed(1, 0.5, true, 85);
  CHECK(!st.end_turn && !st.hold_walk && !st.nudge);
  Economy c(31, 51, false);  // confused, standing (RandomTurn): ends on the idle wait, no nudge
  c.engine_driven = true;
  ended = false;
  for (int i = 0; i < 200 && !ended; ++i) ended = c.Feed(1, 0, false, 0).end_turn;
  CHECK(ended);
}

TEST_CASE("tbc economy: script waits are free; walking up to attack is movement, not the round") {
  CHECK(IsIdleAction(368) && IsIdleAction(84) && IsIdleAction(30) && !IsIdleAction(31) && !IsIdleAction(113));
  Economy w(31, 51, false);
  auto st = w.Feed(10, 0, false, 368);  // WaitRandom from its script: not an action
  CHECK(!w.action_used && !w.acting && st.stop == Stop::None);
  // Enemy attacking from 25 ft away: path search pending (no path, not moving), then it walks.
  Economy e(31, 51, false);
  e.Feed(2, 0, false, 3);  // Attack chosen, path not back yet
  CHECK(e.action_used && e.acting);
  double walked = 0;
  for (int i = 0; i < 60; ++i) {  // 60 ticks of walking at 0.4 ft per tick = 24 ft
    st = e.Feed(1, 0.4, true, 3);
    walked += 0.4;
    CHECK(st.stop == Stop::None);
  }
  CHECK(e.acting && e.move_ft < 31 - 23.9);  // the walk was not cut by the 51-tick round
  for (int i = 0; i < 48; ++i) st = e.Feed(1, 0, false, 3);  // swinging: 2 + 49 = 51 standing ticks
  CHECK(st.stop == Stop::None);
  st = e.Feed(1, 0, false, 3);
  CHECK(st.stop == Stop::RoundDone);
}

TEST_CASE("tbc path line: hidden while the creature casts") {
  CHECK(ShowPathLine(true, 0));     // no action: the walk preview
  CHECK(ShowPathLine(true, 23));    // MoveToPoint
  CHECK(!ShowPathLine(true, 31));   // Spell (also its walk into range)
  CHECK(!ShowPathLine(true, 95));   // SpellPoint
  CHECK(!ShowPathLine(true, 191));  // SpellNoDec
  CHECK(!ShowPathLine(false, 0));   // not the party's turn
}

TEST_CASE("tbc path line: cells decode as AIUpdateWalk, dots split at the budget") {
  const Grid g;
  // cell = (GRID_ACTUALY - 1 - y) * GRID_ACTUALX + x
  const Pt p = CellCentre((320 - 1 - 10) * 320 + 5, g);
  CHECK(p.x == 5 * 16 + 8 && p.y == 10 * 12 + 6);
  const Pt from{0, 0};
  const std::vector<Pt> nodes = {{160, 0}, {160, 120}};  // 10 ft across, then 10 ft down
  CHECK(PathFeet(from, nodes) == 20.0);
  const auto dots = PathDots(from, nodes, 12.0, 2.0, 40);
  CHECK(dots.size() == 10);
  CHECK(dots[0].p.x == 32 && dots[0].p.y == 0 && dots[0].reachable);
  CHECK(dots[5].reachable && !dots[6].reachable);  // 12 ft reachable, 14 ft not
  CHECK(dots[9].p.x == 160 && dots[9].p.y == 120);
  CHECK(PathDots(from, nodes, 12.0, 2.0, 3).size() == 3);
}

TEST_CASE("tbc hud: menu text and Lua data") {
  const std::string m = BuildHudMenu();
  CHECK(m.find("name 'PSTF_TBC'") != std::string::npos);
  CHECK(m.find("name \"pstfCardImg12\"") != std::string::npos);
  CHECK(m.find("pstfDot1\"") == std::string::npos);  // the line is drawn by the engine now
  CHECK(m.find("pstfOrigPortraitL") != std::string::npos);
  CHECK(m.find("action \"PSTF_EndTurn()\"") != std::string::npos);
  CHECK(m.find("name 'PSTF_STATUS'") != std::string::npos);
  CHECK(m.find("pstfHudGuard") != std::string::npos);
  CHECK(std::count(m.begin(), m.end(), '`') == 2);  // one inline Lua chunk
  CHECK(std::count(m.begin(), m.end(), '{') == std::count(m.begin(), m.end(), '}'));
  HudView v;
  v.on = true;
  v.round = 2;
  v.party_turn = true;
  v.move_ft = 12.5;
  v.max_ft = 31;
  HudCard c;
  c.id = 7;
  c.bestiary = 43;
  c.name_ref = 274;
  c.name = "a \"b\"";
  c.hp = 5;
  c.max_hp = 10;
  c.active = true;
  v.cards.push_back(c);
  v.dots = {1, 2};
  const std::string lua = BuildHudLua(v);
  CHECK(lua.find("round=2,slot=-1,party=true,move=12.50,maxMove=31.00") != std::string::npos);
  CHECK(lua.find("{id=7,best=43,name=274,hp=5,maxhp=10,foe=false,active=true,done=false,str=\"a b\"}") !=
        std::string::npos);
  CHECK(lua.find("dots={1,2},rdots={}") != std::string::npos);
  CHECK(lua.find(",aoo=false") != std::string::npos);
  v.provokes = true;  // the previewed walk leaves a foe's reach: the "Provokes attack of opportunity" line
  CHECK(BuildHudLua(v).find(",aoo=true") != std::string::npos);
}

#include "modules/tbc/path_preview.h"

TEST_CASE("tbc preview: A* around a wall, smoothed to turning points") {
  // 10x10 open grid with a wall at x=5, y=0..7 (gap at y=8,9).
  auto cost = [](int x, int y) { return (x == 5 && y <= 7) ? kImpassable : 1; };
  const auto p = FindCellPath({1, 1}, {8, 1}, cost, 10000, 10, 10);
  CHECK(!p.empty());
  CHECK(p.front().x == 1 && p.front().y == 1);
  CHECK(p.back().x == 8 && p.back().y == 1);
  for (size_t i = 1; i < p.size(); ++i) CHECK(LineWalkable(p[i - 1], p[i], cost));
  bool below = false;
  for (const Pt& q : p) below = below || q.y >= 8;
  CHECK(below);              // it goes round through the gap
  CHECK(p.size() <= 5);      // smoothed, not one point per cell
  // Straight line when nothing is in the way.
  const auto s = FindCellPath({0, 0}, {9, 0}, [](int, int) { return 1; }, 10000, 10, 10);
  CHECK(s.size() == 2);
  // Unreachable goal and blocked goal give nothing; so does a tiny search budget.
  CHECK(FindCellPath({1, 1}, {5, 3}, cost, 10000, 10, 10).empty());
  auto boxed = [](int x, int y) { return (x >= 6 && y >= 6 && (x == 6 || y == 6)) ? kImpassable : 1; };
  CHECK(FindCellPath({1, 1}, {8, 8}, boxed, 10000, 10, 10).empty());
  CHECK(FindCellPath({1, 1}, {8, 1}, cost, 5, 10, 10).empty());
  // Uniform cost 5 (the engine's open ground): with min_cost the search stays narrow.
  auto five = [](int, int) { return 5; };
  CHECK(FindCellPath({0, 0}, {60, 40}, five, 200, 100, 100, 5).size() == 2);
}

TEST_CASE("tbc hover: hit chance from THAC0 and AC, 5..95 %") {
  CHECK(NeedsToHit(17, 9) == 8);
  CHECK(HitChancePercent(17, 9) == 65);   // needs 8: faces 8..20 = 13 of 20
  CHECK(HitChancePercent(12, 2) == 55);   // the 64-HP undead (THAC0 12) against AC 2: needs 10
  CHECK(HitChancePercent(20, -10) == 5);  // needs 30: only a natural 20
  CHECK(HitChancePercent(5, 10) == 95);   // needs -5: all but a natural 1
}

TEST_CASE("tbc hover: the creature under the cursor, nearest body wins") {
  std::vector<HoverCandidate> c = {{1, 100, 200}, {2, 110, 205}, {3, 400, 400}};
  CHECK(PickHovered(c, 100, 160) == 1);   // over 1's body
  CHECK(PickHovered(c, 112, 170) == 2);   // nearer 2
  CHECK(PickHovered(c, 250, 250) == -1);  // empty ground
  CHECK(PickHovered(c, 400, 410) == 3);   // at the feet
  CHECK(PickHovered(c, 400, 415) == -1);  // 15 px below the feet: no
  CHECK(PickHovered(c, 400, 300) == -1);  // 100 px above: no
}

TEST_CASE("tbc dice: tumble, land on the roll, verdict, fade") {
  CHECK(DiceTiming::Face(700, 17) == 17 && DiceTiming::Landed(700) && !DiceTiming::Verdict(700));
  CHECK(DiceTiming::Face(100, 17) >= 1 && DiceTiming::Face(100, 17) <= 20 && !DiceTiming::Landed(100));
  CHECK(DiceTiming::Verdict(800) && DiceTiming::Alpha(1000) == 1.0);
  CHECK(DiceTiming::Alpha(2800) > 0.49 && DiceTiming::Alpha(2800) < 0.51);
  CHECK(!DiceTiming::Visible(3000) && DiceTiming::Visible(2999));
}

TEST_CASE("tbc portraits: red share of lost HP") {
  CHECK(DamageShare(64, 64) == 0.0 && DamageShare(0, 64) == 1.0 && DamageShare(-3, 64) == 1.0);
  CHECK(DamageShare(48, 64) == 0.25 && DamageShare(5, 0) == 0.0);
}

TEST_CASE("tbc onsets: 5-second steps are damage over time, other short delays resolve now") {
  CHECK(!OnsetResolvesNow(75) && !OnsetResolvesNow(74) && !OnsetResolvesNow(150));  // Adder's Kiss hits
  CHECK(OnsetResolvesNow(100) && OnsetResolvesNow(99));  // Cone of Cold onset
  CHECK(OnsetResolvesNow(20) && OnsetResolvesNow(0));    // impact a moment later
  CHECK(OnsetResolvesNow(3) && OnsetKind(3, 374) == 1);  // Adder's Kiss casting feature, 3 ticks out
  CHECK(!OnsetResolvesNow(113) && !OnsetResolvesNow(500));  // a round and a half or more: clocked
}

TEST_CASE("tbc waits are ignored on AI turns: attack instead, or the turn is over") {
  CHECK(IsWaitAction(368) && IsWaitAction(63) && IsWaitAction(83) && IsWaitAction(85) && !IsWaitAction(3));
  Economy a(31, 51, false);
  auto st = a.Feed(1, 0, false, 368);  // first tick of its turn: WaitRandom from its script
  CHECK(st.nudge && !st.end_turn);     // no 1-second think: the attack order goes out now
  st = a.Feed(1, 0.5, true, 3);        // it walks up and attacks
  CHECK(!st.end_turn && !st.nudge);
  for (int i = 0; i < 51; ++i) st = a.Feed(1, 0, false, 3);
  CHECK(st.stop == Stop::RoundDone);
  st = a.Feed(1, 0, false, 368);       // script waits again: the turn ends at once
  CHECK(st.end_turn);
  Economy b(31, 51, false);
  b.Feed(1, 0, false, 368);            // nudged
  CHECK(b.Feed(1, 0, false, 63).end_turn);  // still only waiting after the order: turn over
  Economy c(31, 51, false);
  c.Feed(1, 0, false, 3);                   // attacking
  c.Feed(5, 0.4, true, 3);                  // walking up to the target
  CHECK(c.Feed(1, 0, false, 368).end_turn); // its script cuts in with WaitRandom: turn over at once
  Economy p(31, 51, true);
  CHECK(!p.Feed(1, 0, false, 368).nudge);   // party turns: the player decides
}

TEST_CASE("tbc waits: a creature that only fled or walked still picks an action") {
  CHECK(IsMoveAction(355) && IsMoveAction(449) && IsWaitAction(410) && !IsWaitAction(355));
  Economy f(31, 51, false);
  f.Feed(1, 0, false, 29);                  // RunAwayFrom (Curst Citizen, user log 14:32:45)
  f.Feed(10, 0.6, true, 355);               // RunAwayFromNoLeaveArea: walking, 6 ft
  auto st = f.Feed(1, 0, false, 368);       // then WaitRandom
  CHECK(st.nudge && !st.end_turn && !f.action_used);
  CHECK(f.Feed(1, 0, false, 368).end_turn); // still waiting after the order: done
}

TEST_CASE("tbc items: the item takes the action, its own follow-up cast is not stopped") {
  Economy d(31, 51, true);
  d.Feed(1, 0, false, 34);                 // UseItem Whispering Flask (SPICE)
  CHECK(d.action_used && d.acting);
  d.Feed(1, 0, false, 0);                  // item used
  auto st = d.Feed(1, 0, false, 113);      // its op146 makes him ForceSpell Aid (SPPR201)
  CHECK(st.stop == Stop::None && d.acting);
  for (int i = 0; i < 20; ++i) d.Feed(1, 0, false, 0);
  for (int i = 0; i < 20; ++i) d.Feed(1, 0, false, 0);
  CHECK(d.Feed(1, 0, false, 113).stop == Stop::NoAction);  // a cast long after: a second action
  Economy e(31, 51, true);
  e.Feed(1, 0, false, 3);
  CHECK(e.Feed(1, 0, false, 31).stop == Stop::NoAction || e.acting);  // Spell is no follow-up
}

TEST_CASE("tbc onsets: presentation plays now, spells resolve, damage over time is clocked") {
  CHECK(OnsetKind(75, 369) == 2);  // Aid's sparkle (SPPR201 op369, 5 s delay) from the Whispering Flask
  CHECK(OnsetKind(225, 174) == 2); // its sound 15 s later
  CHECK(OnsetKind(45, 374) == 1);  // Adder's Kiss spell hit (op374, 3 s delay): the spell resolving
  CHECK(OnsetKind(75, 12) == 0);   // a damage hit one round on: clocked
}

TEST_CASE("tbc lingering spells: one round of pulse ticks per caster round") {
  PulseBudget b;
  int ran = 0;
  for (int i = 0; i < 200; ++i) ran += b.Run(7) ? 1 : 0;  // the cast round, from landing
  CHECK(ran == PulseBudget::kFirst && b.Spent(7));
  b.NewRound(7);  // caster's next turn
  ran = 0;
  for (int i = 0; i < 200; ++i) ran += b.Run(7) ? 1 : 0;
  CHECK(ran == 75 && b.Spent(7) && !b.Spent(8));
  // Swarm Curse pulses every 75 ticks of its counter: one pulse per round after the first.
  b.Clear();
  int counter = 300, pulses = 0;  // ticks of duration left
  auto tick = [&] { if (b.Run(9)) { --counter; if (counter % 75 == 0) ++pulses; } };
  for (int i = 0; i < 500; ++i) tick();
  CHECK(pulses == 1);             // 300 -> 225 in the cast round: the pulse at 225
  b.NewRound(9);
  for (int i = 0; i < 500; ++i) tick();
  CHECK(pulses == 2);             // 225 -> 150: the pulse at 150
}

TEST_CASE("tbc combat log: line format and action names") {
  CHECK(CombatLogLine(12, 5, 3, 7, 808714, 2, 12, "TURN", "Dak'kon (880088) hp 35/42") ==
        "12:05:03.007 w808714 R2 +0012 | TURN   Dak'kon (880088) hp 35/42");
  CHECK(ActionName(368) == "WaitRandom" && ActionName(3) == "Attack" && ActionName(999) == "action 999");
  // 0403MAGE's step: Spell; Wait(1); IncrementGlobal -> the turn ends at the Wait and the step counter is lost
  const uint16_t q[] = {63, 109};
  int lost = -1;
  CHECK(QueueText(q, 2, &lost) == "Wait, IncrementGlobal" && lost == 1);
  const uint16_t q2[] = {3, 63, 368};
  CHECK(QueueText(q2, 3, &lost) == "Attack, Wait, WaitRandom" && lost == 0);
  CHECK(IsStateAction(111) && IsStateAction(10) && !IsStateAction(425));
}

TEST_CASE("tbc script queue: an AI keeps its sequence across turns, a lead Wait is skipped") {
  // 0403MAGE at its turn end: [Wait, IncrementGlobal] -> kept for its next turn (the step counter climbs)
  const uint16_t mage[] = {63, 109};
  CHECK(KeepQueueAtTurnEnd(false, false, mage, 2));
  CHECK(!KeepQueueAtTurnEnd(true, false, mage, 2));   // party: cleared as before
  CHECK(!KeepQueueAtTurnEnd(false, true, mage, 2));   // our nudge order: cleared
  const uint16_t alone[] = {3};
  CHECK(!KeepQueueAtTurnEnd(false, false, alone, 1));  // nothing queued
  // 0601JUMB step 2: Wait(1); Spell -> the Wait is skipped; Wait; WaitRandom alone -> nudged as before
  const uint16_t lead[] = {63, 31};
  CHECK(SkipLeadWait(lead, 2));
  const uint16_t waits[] = {63, 368};
  CHECK(!SkipLeadWait(waits, 2));
  const uint16_t patrol[] = {63, 23, 143, 63, 23};  // Jumble's pre-fight route: not his fight script
  CHECK(!SkipLeadWait(patrol, 5));
  const uint16_t chatter[] = {368, 425, 454, 36};  // Giltspur's WaitRandom; FloatMessage; PlaySequence; Continue
  CHECK(!SkipLeadWait(chatter, 4));
  const uint16_t act[] = {31, 63};
  CHECK(!SkipLeadWait(act, 2));
  // the step counter after its spell is bookkeeping, not a second action (no "No action left" stop)
  Economy e(31.0, 51, false);
  for (int i = 0; i < 10; ++i) e.Feed(1, 0, false, 31);
  e.Feed(1, 0, false, 0);
  CHECK(e.action_used);
  CHECK(e.Feed(1, 0, false, 109).stop == Stop::None);
  CHECK(IsIdleAction(109) && IsIdleAction(10) && !IsIdleAction(31));
  CHECK(KeepCurrentAtTurnEnd(109) && !KeepCurrentAtTurnEnd(63) && !KeepCurrentAtTurnEnd(3));
  // Soego (1500SOEG) on AttackedBy: poof, recolour x8, polymorph are no action; his Attack is
  CHECK(IsIdleAction(418) && IsIdleAction(440) && IsIdleAction(146) && IsIdleAction(229) && !IsIdleAction(3));
}

TEST_CASE("tbc APR: 1.5 attacks = 2 swings, then 1, the attack ends after its swings") {
  CHECK(AprHalvesFromStat(7) == 3);  // TNO in the Hive save: 1 1/2
  CHECK(AttacksThisRound(3, 0) == 2 && AttacksThisRound(3, 1) == 1 && AttacksThisRound(3, 2) == 2);
  Economy e(31, 51, true);
  e.want_swings = 2;
  e.Feed(1, 0, false, 3);  // Attack, in reach
  for (int i = 0; i < 10; ++i) e.Feed(1, 0, false, 3);
  e.Swing();
  auto st = e.Feed(20, 0, false, 3);
  CHECK(st.stop == Stop::None && e.acting && e.SwingsLeft() == 1);  // one more to come
  e.Swing();
  st = e.Feed(1, 0, false, 3);
  CHECK(st.stop == Stop::None && e.SwingsLeft() == 0);  // its animation finishes first
  for (int i = 0; i < 7 && st.stop == Stop::None; ++i) st = e.Feed(1, 0, false, 3);
  CHECK(st.stop == Stop::RoundDone && !e.acting);
  // The engine's round ended with a swing missing: up to a second round for it.
  Economy l(31, 51, true);
  l.want_swings = 2;
  l.Feed(1, 0, false, 3);
  l.Swing();
  st = l.Feed(55, 0, false, 3);
  CHECK(st.stop == Stop::None && l.acting);
  st = l.Feed(50, 0, false, 3);
  CHECK(st.stop == Stop::RoundDone);
  // One swing wanted, got: done after the tail, well before the 51-tick round.
  Economy o(31, 51, false);
  o.want_swings = 1;
  o.Feed(1, 0, false, 3);
  o.Swing();
  st = o.Feed(8, 0, false, 3);
  CHECK(st.stop == Stop::RoundDone);
}

TEST_CASE("tbc AI: after its action the turn is over, no wandering off") {
  Economy a(31, 51, false);
  a.want_swings = 1;
  a.Feed(1, 0, false, 3);
  a.Swing();
  auto st = a.Feed(8, 0, false, 3);
  CHECK(st.stop == Stop::RoundDone && st.hold_walk);
  st = a.Feed(1, 0, true, 355);  // its script runs away right after: held, not walked
  CHECK(st.hold_walk);
  st = a.Feed(3, 0, true, 355);
  CHECK(st.end_turn);
}

#include "modules/tbc/ui_layout.h"

TEST_CASE("tbc BG3 layout: portraits move to their own menu, select buttons go, the rest stays") {
  std::string bar =
      "menu\n{\n\tname \"WORLD_ACTIONBAR\"\n\tonClose\n\t\"\n\t\tInfinity_PopMenu('WORLD_ACTIONBAR_LEFT')\n\t\"\n}\n"
      "menu\n{\n\tname \"WORLD_ACTIONBAR_LEFT\"\n\tonOpen \"horribleTimeFrameOffset = 0\"\n"
      "\tbutton\n\t{\n\t\tarea 84 57 25 24\n\t\tbam \"CIRCBUTT\"\n\t\taction\n\t\t\"\n\t\t\tgame:SelectAll()\n\t\t\"\n\t}\n";
  for (int q = 0; q < 5; ++q)
    bar += "\tbutton\n\t{\n\t\tarea 110 0 45 85\n\t\tbam \"TACTBUTT\"\n\t\tsequence " + std::to_string(q) + "\n\t}\n";
  for (int n = 5; n >= 0; --n) {
    const std::string k = std::to_string(n);
    bar += "\ttext\n\t{\n\t\tarea 763 0 85 85\n\t\tmosaic lua \"getPortraitBg(" + k + ")\"\n\t}\n";
    bar += "\tbutton\n\t{\n\t\tarea 769 4 74 58\n\t\tportrait " + k + "\n\t\taction \"Infinity_OnPortraitLClick(" + k + ")\"\n\t}\n";
    bar += "\tbutton\n\t{\n\t\tarea 767 64 78 16\n\t\thealthbar " + k + "\n\t}\n";
    bar += "\tbutton \n\t{\n\t\tenabled \"Infinity_CanLevelUp(" + k + ")\"\n\t\tbam \"LVLBUTT\"\n\t}\n";
  }
  bar += "}\nmenu\n{\n\tname \"WORLD_ACTIONBAR_LEFT_TABLET\"\n\tbutton\n\t{\n\t\tportrait 0\n\t}\n}\n";
  CHECK(MenuItems(bar, "WORLD_ACTIONBAR_LEFT").size() == 30);
  std::string why;
  const std::string out = Bg3Layout(bar, &why);
  CHECK(why.empty());
  const auto left = MenuItems(out, "WORLD_ACTIONBAR_LEFT");
  CHECK(left.size() == 2);  // select-all and Talk (TACTBUTT 4) stay
  CHECK(out.find("sequence 4") != std::string::npos && out.find("sequence 3") == std::string::npos);
  const auto port = MenuItems(out, "PSTF_PORTRAITS");
  CHECK(port.size() == 25);  // 24 moved + the layout ticker
  CHECK(out.find("name \"pstfPortPic5\"") != std::string::npos && out.find("name \"pstfPortLvl0\"") != std::string::npos);
  CHECK(MenuItems(out, "WORLD_ACTIONBAR_LEFT_TABLET").size() == 1);  // the tablet bar is untouched
  // Not the bar we know: nothing changes.
  const std::string odd = "menu\n{\n\tname \"WORLD_ACTIONBAR_LEFT\"\n\tbutton\n\t{\n\t\tportrait 0\n\t}\n}\n";
  CHECK(Bg3Layout(odd, &why) == odd && !why.empty());
  // the open hook raises our menus over the bar again (no close hook any more: pstfHudGuard)
  CHECK(out.find("pstfHudRaise()") != std::string::npos);
  // 8-member party: slots 6 and 7 are slot 5's group renumbered
  const std::string out8 = Bg3Layout(bar, &why, 8);
  CHECK(why.empty());
  CHECK(MenuItems(out8, "PSTF_PORTRAITS").size() == 33);
  CHECK(out8.find("name \"pstfPortPic7\"") != std::string::npos);
  CHECK(out8.find("\tportrait 7\n") != std::string::npos && out8.find("\thealthbar 6\n") != std::string::npos);
  CHECK(out8.find("getPortraitBg(7)") != std::string::npos && out8.find("Infinity_OnPortraitLClick(6)") != std::string::npos);
  CHECK(out8.find("Infinity_CanLevelUp(7)") != std::string::npos);
  CHECK(out8.find("enabled \"Infinity_GetNumCharacters() > 7\"") != std::string::npos);  // empty slot: no ornament
}

TEST_CASE("tbc hud: hotbar menu, End Turn bam, floats and attacks in the Lua data") {
  const std::string m = BuildHudMenu(true);
  CHECK(m.find("name 'PSTF_BAR'") != std::string::npos);
  CHECK(m.find("bam \"PFENDTRN\"") != std::string::npos);
  CHECK(m.find("pstfBg3 = pstfBg3Ok == true") != std::string::npos);
  CHECK(m.find("'PFTRIP'") != std::string::npos && m.find("'AMGUARD'") == std::string::npos);  // no Block/Guard
  CHECK(m.find("bam \"PFPAGEUP\"") != std::string::npos && m.find("action \"pstfBarNext()\"") != std::string::npos);
  CHECK(m.find("PSTF_BAR_COLS = 10") != std::string::npos && m.find("PSTF_BAR_ROWS = 2") != std::string::npos);
  CHECK(m.find("text color lua \"0xFFFFFF\"") != std::string::npos);  // white "Initiative: N", "23 / 31 ft"
  CHECK(m.find("name \"pstfSt5_8R5\"") != std::string::npos && m.find("name \"pstfSt6_1O\"") == std::string::npos);
  CHECK(BuildHudMenu(true, 8).find("name \"pstfSt7_8G1\"") != std::string::npos);
  CHECK(m.find("name \"pstfStH_8I\"") != std::string::npos);
  CHECK(m.find("name \"pstfCardBtn1\"") != std::string::npos && m.find("name \"pstfFloat16\"") != std::string::npos);
  CHECK(std::count(m.begin(), m.end(), '{') == std::count(m.begin(), m.end(), '}'));
  CHECK(BuildHudMenu(false).find("pstfBg3 = false") != std::string::npos);
  HudView v;
  v.on = true;
  v.attacks_left = 1;
  v.attacks_round = 2;
  v.floats.push_back({100, 50, "Initiative: 7"});
  const std::string lua = BuildHudLua(v);
  CHECK(lua.find(",atk=1,atkRound=2,floats={{x=100,y=50,t=\"Initiative: 7\"},}") != std::string::npos);
}

#include "modules/tbc/status.h"

TEST_CASE("tbc status: one icon per spell/item, debuffs first, rounds left and the drained share") {
  StatusSpans spans;
  std::vector<StatusEffect> fx = {
      {1, "SPWI101", 0, 450, true},     // 4.5 rounds of a buff (two effects of it)
      {1, "SPWI101", 0, 300, true},
      {1, "SPPR101", 0x400, 120, true}, // a hostile spell: debuff, 2 rounds
      {1, "SPFLASH", 0, 20, true},      // a flash, not a status
      {2, "POTN08", 0, 900, true},      // a potion
      {1, "SPEQUIP", 0, 0, false},      // not timed
  };
  auto icons = CollectStatus(7, fx, 1000, 100, &spans, 8);
  CHECK(icons.size() == 3);
  CHECK(icons[0].parent == "SPPR101" && icons[0].debuff && icons[0].rounds == 2);
  CHECK(icons[1].parent == "SPWI101" && !icons[1].debuff && icons[1].rounds == 5);
  CHECK(icons[2].parent == "POTN08" && icons[2].parent_type == 2 && icons[2].rounds == 9);
  CHECK(icons[1].share == 1.0);
  // 150 ticks later: 300 of 450 left
  fx = {{1, "SPWI101", 0, 300, true}};
  icons = CollectStatus(7, fx, 1150, 100, &spans, 8);
  CHECK(icons.size() == 1 && std::abs(icons[0].share - 300.0 / 450.0) < 1e-9);
  // recast: a later end restarts the span
  fx = {{1, "SPWI101", 0, 600, true}};
  icons = CollectStatus(7, fx, 1200, 100, &spans, 8);
  CHECK(icons[0].share == 1.0 && icons[0].rounds == 6);
  CHECK(CollectStatus(7, fx, 1200, 100, nullptr, 0).empty());
  // Globe of Invulnerability: its sphere re-applies 3 s (45 tick) protections: a status (a quarter round is 25)
  fx = {{1, "SPWI602", 0, 45, true}, {1, "SPFLASH", 0, 24, true}};
  icons = CollectStatus(8, fx, 1200, 100, &spans, 8);
  CHECK(icons.size() == 1 && icons[0].parent == "SPWI602" && icons[0].rounds == 1);
}

TEST_CASE("tbc status: Guardian Mantle's attached object gets the spell seen on its way") {
  OrphanMap m;
  // the spell's delayed effects arrive (parent SPWI704), then the attached object (no parent, timed)
  std::vector<StatusEffect> fx = {{1, "SPWI704", 0, 30, true, true, 11}};
  AttributeOrphans(5, &fx, 1000, &m);
  fx = {{0, "", 0, 1200, true, false, 0xABC0}};
  AttributeOrphans(5, &fx, 1040, &m);
  CHECK(fx[0].parent == "SPWI704" && fx[0].parent_type == 1);
  const auto icons = CollectStatus(5, fx, 1040, 100, nullptr, 8);
  CHECK(icons.size() == 1 && icons[0].parent == "SPWI704" && icons[0].rounds == 12);
}

TEST_CASE("tbc status: the drained frame and fitted icons") {
  // full: the whole outline of a 22 px box, 2 px thick, in five pieces from the top centre
  auto f = DrainFrame(0, 0, 22, 2, 1.0);
  CHECK(f.size() == 5);
  CHECK(f[0].x == 1 && f[0].y == 0 && f[0].w == 10 && f[0].h == 2);   // top, centre to the left corner
  CHECK(f[1].x == 0 && f[1].y == 0 && f[1].w == 2 && f[1].h == 22);   // left side
  CHECK(f[2].x == 0 && f[2].y == 20 && f[2].w == 22 && f[2].h == 2);  // bottom
  CHECK(f[3].x == 20 && f[3].y == 0 && f[3].w == 2 && f[3].h == 22);  // right side
  CHECK(f[4].x == 10 && f[4].y == 0 && f[4].w == 12 && f[4].h == 2);  // top, right corner to the centre
  // a quarter: half the top-left leg's way... the top-left leg (10) and 10 of the left side
  f = DrainFrame(0, 0, 22, 2, 0.25);
  CHECK(f.size() == 2 && f[0].w == 10 && f[1].h == 12);
  CHECK(DrainFrame(0, 0, 22, 2, 0.0).empty());
  // item icons keep their aspect: Bandages (30 x 23) in a 38 px square
  const Rect r = FitIcon(30, 23, 38);
  CHECK(r.w == 38 && r.h == 29 && r.x == 0 && r.y == 4);
  const Rect q = FitIcon(32, 32, 38);
  CHECK(q.w == 38 && q.h == 38);
  const StatusIconView v = MakeIconView({"SPWI101", 1, 3, 0.5, false}, "SPWI101C", 274, 32, 32, 22);
  CHECK(v.fit.x == 3 && v.fit.w == 16 && v.frame.size() == 3);
  CHECK(MakeIconView({"X", 1, 1, 1.0, true}, "B", -1, 256, 256, kStatusPortraitPx).fit.w == kStatusPortraitPx - 6);
  HoverView h;
  h.on = true;
  h.name_ref = 5;
  h.icons.push_back(v);
  const std::string lua = BuildStatusLua({{}, {v}}, h);
  CHECK(lua.find("pstfStat = {[0]={},[1]={{b=\"SPWI101C\",n=274,r=3,d=false,fit={3,3,16,16},f={") != std::string::npos);
  CHECK(lua.find("pstfHov = {on=true,fight=false,name=5,") != std::string::npos);
  CHECK(lua.find("aim=false,cx=-1,cy=-1,") != std::string::npos);
  // the hit chance goes next to the cursor (BG3), not on the card
  h.foe = true, h.fight = true, h.chance = 80, h.aim = true, h.cur_x = 400, h.cur_y = 300, h.px_w = 2560;
  CHECK(BuildStatusLua({}, h).find("chance=80,ac=0,needs=0,lv=0,aim=true,cx=400,cy=300,pw=2560,") != std::string::npos);
}

TEST_CASE("tbc preview: a creature is dear ground, the path goes round it and stays round") {
  // open ground 5, a creature footprint (cost 55) in a 3x3 block in the middle of a straight line
  auto cost = [](int x, int y) { return (x >= 4 && x <= 6 && y >= 3 && y <= 5) ? 55 : 5; };
  const auto p = FindCellPath({0, 4}, {10, 4}, cost, 10000, 12, 12, 5);
  CHECK(p.size() >= 3);  // not a straight cut through it
  for (size_t i = 1; i < p.size(); ++i) CHECK(LineWalkable(p[i - 1], p[i], cost, 5));
}

TEST_CASE("tbc status: parentless spell effects take the spell seen on its way (Submerge the Will, Power of One)") {
  using namespace pstf::tbc;
  OrphanMap m;
  // Submerge the Will as the probe saw it: delayed op375 (parent SPWI119) first, then op0/op33..37 with no parent.
  std::vector<StatusEffect> fx{{1, "SPWI119", 0, 25, false, true, 0x10}};
  AttributeOrphans(0x870087, &fx, 1000, &m);
  std::vector<StatusEffect> later{{0, "", 0, 1680, true, false, 0x20}, {0, "", 0, 1680, true, false, 0x28}};
  AttributeOrphans(0x870087, &later, 1030, &m);
  CHECK(later[0].parent == "SPWI119" && later[0].parent_type == 1);
  CHECK(later[1].parent == "SPWI119");
  // They keep it long after the window, and the icon shows: one source, 1600 ticks left = 16 rounds of 100.
  std::vector<StatusEffect> much_later{{0, "", 0, 1600, true, false, 0x20}};
  AttributeOrphans(0x870087, &much_later, 5000, &m);
  CHECK(much_later[0].parent == "SPWI119");
  const auto icons = CollectStatus(0x870087, much_later, 5000, 100, nullptr, 8);
  CHECK(icons.size() == 1 && icons[0].parent == "SPWI119" && icons[0].rounds == 16);
  // A new orphan long after the window is not guessed; another creature's spell is not borrowed.
  std::vector<StatusEffect> stray{{0, "", 0, 900, true, false, 0x30}};
  AttributeOrphans(0x870087, &stray, 5000, &m);
  CHECK(stray[0].parent.empty());
  std::vector<StatusEffect> other{{0, "", 0, 900, true, false, 0x40}};
  AttributeOrphans(0x880088, &other, 1030, &m);
  CHECK(other[0].parent.empty());
  // Effects that went away forget their parent (a reused key gets a fresh look).
  std::vector<StatusEffect> none;
  AttributeOrphans(0x870087, &none, 6000, &m);
  std::vector<StatusEffect> reused{{0, "", 0, 900, true, false, 0x20}};
  AttributeOrphans(0x870087, &reused, 6000, &m);
  CHECK(reused[0].parent.empty());
}

TEST_CASE("tbc round clock: the casting aura clears between a caster's turns, as DoAction clears it after a round") {
  // Values from the all-spells suite (tbc_spells_all.lua -Trace): after a cast the counter sat at 25..33 at the
  // next turn (the caster's AI ran only during its own turn) and SpellPST refused every spell (no cast feedback).
  CHECK(StepCastAura(-1, 75, 51, false) == -1);  // clean stays clean
  CHECK(StepCastAura(31, 75, 51, false) == -1);  // 31 + one round (75) >= ROUND_LENGTH 51: may cast again
  CHECK(StepCastAura(0, 51, 51, false) == -1);   // exactly a round
  CHECK(StepCastAura(0, 50, 51, false) == 50);   // a tick short
  CHECK(StepCastAura(31, 75, 51, true) == 106);  // a cast still running keeps it (DoAction checks +0x46B0)
  CHECK(StepCastAura(0x7FF0, 75, 51, true) == 0x7FFF);  // no wrap
}

TEST_CASE("tbc economy: a long cast is not cut by the attack-round limit") {
  // All-spells suite: Rune of Torment (SPWI914, casting speed 9) was stopped "Attack round done" at 51 ticks with
  // its casting glow on, so it never went off. A cast runs until the engine ends it; an attack still stops.
  Economy e(31, 51, true);
  EconomyStep st;
  for (int i = 0; i < 80; ++i) st = e.Feed(1, 0, false, 191);  // SpellNoDec, still casting after 80 ticks
  CHECK(st.stop == Stop::None && e.acting && e.action_used);
  st = e.Feed(1, 0, false, 0);  // the engine finished the cast
  CHECK(st.stop == Stop::None && !e.acting);
  Economy hung(31, 51, true);
  for (uint32_t i = 0; i < 4 * 51; ++i) st = hung.Feed(1, 0, false, 31);  // Spell: a hung cast, 4 rounds
  CHECK(st.stop == Stop::RoundDone && !hung.acting);
  Economy atk(31, 51, true);
  for (int i = 0; i < 51; ++i) st = atk.Feed(1, 0, false, 3);  // Attack: one round
  CHECK(st.stop == Stop::RoundDone);
  CHECK(IsCastAction(191) && IsCastAction(192) && IsCastAction(95) && !IsCastAction(3) && !IsCastAction(34));
}

TEST_CASE("tbc economy: a cast with no casting time still spends the action") {
  // All-spells suite: Identify (SPWI105, speed 0) went off with the engine's cast feedback while every frame showed
  // no action, so the turn's action stayed free. The cast feedback takes it.
  Economy e(31, 51, true);
  auto st = e.Feed(1, 0, false, 0);
  CHECK(!e.action_used);
  CHECK(e.SawCast());
  CHECK(e.action_used && e.act_action == 191 && !e.acting);
  CHECK(!e.SawCast());  // once
  st = e.Feed(1, 0, false, 3);  // a later attack: no action left
  CHECK(st.stop == Stop::NoAction);
}

TEST_CASE("tbc onsets: a 75-tick impact timed to its spell's visuals resolves now, a damage series stays clocked") {
  // Stygian Ice Storm (SPWI705) as seen one tick after it landed: op12 8d8 at 74, op373 ice drop 64, op269 shake
  // and op174 sound 69, op128 44 (all one source).
  const uint64_t ice = 0x353037495750ull, other = 0x313032495750ull;
  OnsetSeen v[] = {{74, 12, ice, OnsetKind(74, 12)}, {64, 373, ice, OnsetKind(64, 373)}, {69, 269, ice, OnsetKind(69, 269)},
                   {69, 174, ice, OnsetKind(69, 174)}, {44, 128, ice, OnsetKind(44, 128)}};
  CHECK(v[0].kind == 0);  // alone: on a 5-second step
  SyncSpellOnsets(v, 5);
  CHECK(v[0].kind == 1 && v[1].kind == 2 && v[2].kind == 1 && v[3].kind == 2 && v[4].kind == 1);
  // A damage series with a sound at each step: the same opcode at 75 and 150 stays round-clocked.
  OnsetSeen dot[] = {{75, 12, other, 0}, {150, 12, other, 0}, {75, 174, other, 2}, {150, 174, other, 2}};
  SyncSpellOnsets(dot, 4);
  CHECK(dot[0].kind == 0 && dot[1].kind == 0);
  // Visuals of another spell do not pull it in; visuals 20 ticks away do not either.
  OnsetSeen apart[] = {{75, 12, other, 0}, {70, 373, ice, 2}, {150, 12, ice, 0}, {130, 373, ice, 2}};
  SyncSpellOnsets(apart, 4);
  CHECK(apart[0].kind == 0 && apart[2].kind == 0);
}

TEST_CASE("tbc projectiles: theirs move in their caster's turn, one born outside it flies on and lands") {
  ProjectilePace pace;
  PulseBudget pulses;
  const int32_t ignus = 7, tno = 1, fire = 100, missile = 101, cloud = 102;
  // Ignus' fire that started the fight: first seen in the Nameless One's turn, it keeps flying.
  CHECK(pace.Runs(ignus, fire, tno, false, &pulses));
  CHECK(pace.Runs(ignus, fire, tno, false, &pulses));
  // Ignus' own-turn missile: moves in his turn, waits in another's.
  CHECK(pace.Runs(ignus, missile, ignus, false, &pulses));
  CHECK(!pace.Runs(ignus, missile, tno, false, &pulses));
  // A landed cloud born free still keeps to its caster's pulse budget.
  CHECK(!pace.Runs(ignus, cloud, tno, true, &pulses));
  CHECK(pace.Runs(ignus, cloud, ignus, true, &pulses));
  pace.Clear();
  CHECK(pace.free.empty());
}

TEST_CASE("tbc status: a glow that outlasts the buff does not stretch its icon, a glow alone is no status") {
  // Blessing (SPPR101) as seen in game: op130 bless 450 ticks left, op9 glow 480 (tbc_spells_all, 2026-11-25)
  StatusEffect bless, glow;
  bless.parent_type = glow.parent_type = 1;
  bless.parent = glow.parent = "SPPR101";
  bless.timed = glow.timed = true;
  bless.left = 450, bless.opcode = 130, bless.key = 1;
  glow.left = 480, glow.opcode = 9, glow.key = 2;
  auto icons = CollectStatus(1, {bless, glow}, 0, 75, nullptr, 8);
  CHECK(icons.size() == 1 && icons[0].rounds == 6);
  CHECK(CollectStatus(1, {glow}, 0, 75, nullptr, 8).empty());
  // an attached spell object (opcode unknown) still counts
  StatusEffect att = bless;
  att.opcode = -1, att.left = 300;
  icons = CollectStatus(1, {att}, 0, 75, nullptr, 8);
  CHECK(icons.size() == 1 && icons[0].rounds == 4);
}

TEST_CASE("tbc status: Halo's parentless AC takes the spell whose glow arrived with it, not an old one") {
  using namespace pstf::tbc;
  OrphanMap m;
  // first look: an old Blessing (parented, timed) and nothing else: nothing is "arriving"
  std::vector<StatusEffect> fx{{1, "SPPR101", 0, 300, true, false, 0x10}};
  AttributeOrphans(0x180018, &fx, 1000, &m);
  // Halo of Lesser Revelation lands (projectile 259): its op9 glow (parent SPPR105, 45 ticks) and the AC effect
  // the projectile builds with no parent, 20 s per level
  fx = {{1, "SPPR101", 0, 290, true, false, 0x10}, {1, "SPPR105", 0, 45, true, false, 0x50, 9},
        {0, "", 0, 4200, true, false, 0x58}};
  AttributeOrphans(0x180018, &fx, 1010, &m);
  CHECK(fx[2].parent == "SPPR105");
  // a creature seen for the first time with an orphan and a parented effect: no guess
  std::vector<StatusEffect> first{{1, "SPPR101", 0, 300, true, false, 0x70}, {0, "", 0, 900, true, false, 0x78}};
  AttributeOrphans(0x190019, &first, 1010, &m);
  CHECK(first[1].parent.empty());
}

TEST_CASE("tbc round clock: an attached object's pulse fires once per owner round (Conflagration)") {
  // the engine: 51 (ROUND_LENGTH) after a burn, -1 per update; the shift gives the frame's ticks back
  uint16_t c = 51;
  for (int t = 0; t < 300; ++t) {  // 300 world ticks of other creatures' turns: no burn
    c = static_cast<uint16_t>(c - 1);
    c = PulseAfterShift(c, 1);
  }
  CHECK(c == 51);
  c = PulseAfterShift(c, -75);  // the owner's turn start
  CHECK(c == 0);                // its next update burns
  CHECK(PulseAfterShift(c, 1) == 0);  // the same frame's shift keeps it due
  CHECK(PulseAfterShift(1, -75) == 0 && PulseAfterShift(0x7FF0, 100) == 0x7FFF);
}

TEST_CASE("tbc turns: the turn-lost check waits for the engine's state after the step") {
  LoseTurnCheck c;
  CHECK(!c.Due(5));  // not started
  c.Start();
  CHECK(!c.Due(0));  // the turn-start frame itself: STATE still has the expired sleep's bit
  CHECK(!c.Due(1));
  CHECK(c.Due(1));   // 2 ticks in: read now
  CHECK(!c.Due(1));  // once per turn
  c.Start();
  CHECK(c.Due(3));
}

TEST_CASE("tbc turns: confused, panicked and berserk creatures are left to the engine's AI") {
  // Confusion (SPWI402) set 0x80000000 on the thug; the idle nudge then ordered Attack(Player6) every turn
  CHECK(EngineDrivesAi(0x80000000LL));
  CHECK(EngineDrivesAi(0x4));   // panic (Horror, Ignus' Terror)
  CHECK(EngineDrivesAi(0x2));   // berserk
  CHECK(EngineDrivesAi(0x4 | 0x40000));
  CHECK(!EngineDrivesAi(0));
  CHECK(!EngineDrivesAi(0x40000));  // blind: still our nudge
  CHECK(!EngineDrivesAi(-1));
}

TEST_CASE("tbc turns: sleeping, stunned, paralysed, held, frozen or stone creatures lose their turn") {
  // STATE bits as the all-spells suite saw them: Chromatic Orb 0x20 (paralyse), Horror 0x4, Confusion 0x80000000
  CHECK(LosesTurn(0x20));
  CHECK(LosesTurn(0x1));
  CHECK(LosesTurn(0x8));
  CHECK(LosesTurn(0x40) && LosesTurn(0x80));
  CHECK(!LosesTurn(0x4));          // panic: the engine's AI runs away on its turn
  CHECK(!LosesTurn(0x80000000LL)); // confused: acts on its own
  CHECK(!LosesTurn(0x2));          // berserk
  CHECK(!LosesTurn(0x40000));      // blind: acts with penalties
  CHECK(!LosesTurn(0x800 | 0x20)); // dead
  CHECK(!LosesTurn(0) && !LosesTurn(-1));
}

TEST_CASE("tbc status: in a fight the drain frame keeps the length from the cast and never refills between turns") {
  // A 17-round buff (1275 ticks) cast in a fight: the round clock holds effect time while the world clock runs, so
  // `left` stays 1275 through the cast turn and other creatures' turns, then drops 75 at each owner turn start.
  StatusSpans spans;
  std::vector<StatusEffect> fx{{1, "SPWI216", 0, 1275, true, false, 1, 0}};
  auto icons = CollectStatus(9, fx, 5000, 75, &spans, 8);
  CHECK(icons.size() == 1 && icons[0].rounds == 17 && icons[0].share == 1.0);
  for (int64_t now = 5001; now < 5400; now += 7) {  // other turns: world time runs, left does not
    icons = CollectStatus(9, fx, now, 75, &spans, 8);
    CHECK(icons[0].share == 1.0 && icons[0].rounds == 17);
  }
  fx[0].left = 1200;  // the owner's turn start
  icons = CollectStatus(9, fx, 5400, 75, &spans, 8);
  CHECK(icons[0].rounds == 16 && std::abs(icons[0].share - 1200.0 / 1275.0) < 1e-9);
  for (int64_t now = 5401; now < 5800; now += 7) {  // and it stays there, no refill
    icons = CollectStatus(9, fx, now, 75, &spans, 8);
    CHECK(icons[0].rounds == 16 && std::abs(icons[0].share - 1200.0 / 1275.0) < 1e-9);
  }
  // ten rounds on: 7 of 17 left
  fx[0].left = 525;
  icons = CollectStatus(9, fx, 9000, 75, &spans, 8);
  CHECK(icons[0].rounds == 7 && std::abs(icons[0].share - 525.0 / 1275.0) < 1e-9);
  // a recast: the time left goes up, the span starts again from the new length
  fx[0].left = 1275;
  icons = CollectStatus(9, fx, 9100, 75, &spans, 8);
  CHECK(icons[0].rounds == 17 && icons[0].share == 1.0);
}

TEST_CASE("tbc onsets: a classified delayed effect is forgotten when it runs, so reused memory is classified anew") {
  // tbc_decay.lua: Submerge the Will's delayed op375 (29 ticks: resolves now) landed at an address where an earlier
  // round-clocked effect had lived; the stale entry round-clocked the shield to its caster's next turn.
  OnsetBook b;
  const uint64_t addr = 0x7F00AA00, tno = 0x860086, sub = 0x393131495750ull, old = 0x333031525050ull;
  b.Add(addr, 0, tno, 12, old);       // an earlier damage step, round-clocked
  CHECK(b.Known(addr, tno, 12, old) && b.Kind(addr) == 0);
  uint64_t none[1] = {0};
  b.Prune(tno, none, 0);              // it ran: no longer pending on its owner
  CHECK(b.Kind(addr) == -1);
  CHECK(!b.Known(addr, tno, 375, sub));  // the new effect at that address is new
  b.Add(addr, 1, tno, 375, sub);
  CHECK(b.Kind(addr) == 1);
  // a reuse within one pass (no prune between): another opcode or source is not the same effect
  CHECK(!b.Known(addr, tno, 12, old) && b.Kind(addr) == -1);
  // another creature's prune leaves this creature's entries
  b.Add(addr, 1, tno, 375, sub);
  b.Prune(0x180018, none, 0);
  CHECK(b.Kind(addr) == 1);
  uint64_t live[1] = {addr};
  b.Prune(tno, live, 1);
  CHECK(b.Kind(addr) == 1);
}

TEST_CASE("tbc status: an orphan the creature already had does not take a newly arriving spell") {
  // the all-spells suite: Blindness (60 s) showed 50 rounds: a 3750-tick parentless leftover on the foe took it
  OrphanMap m;
  std::vector<StatusEffect> fx{{0, "", 0, 3750, true, false, 0x90}};
  AttributeOrphans(0x180018, &fx, 1000, &m);  // first look: the leftover
  fx = {{0, "", 0, 3750, true, false, 0x90}, {1, "SPWI115", 0x400, 900, true, false, 0x98, 74}};
  AttributeOrphans(0x180018, &fx, 1020, &m);  // Blindness arrives
  CHECK(fx[0].parent.empty());
  const auto icons = CollectStatus(0x180018, fx, 1020, 75, nullptr, 8);
  CHECK(icons.size() == 1 && icons[0].parent == "SPWI115" && icons[0].rounds == 12);
}

TEST_CASE("tbc status: parentless effects with no parented one beside them take the spell being cast") {
  // probe icons.lua: Halo of Lesser Revelation puts op9 + op0 on the foe, both parentless; Black-Barbed Shield an
  // op0 on its caster. The spell being cast (CGameSprite +0x47F8) is their parent.
  OrphanMap m;
  std::vector<StatusEffect> fx;
  AttributeOrphans(0x180018, &fx, 1000, &m);  // a look before
  m.NoteCast("SPPR105", 0x400, 1005);
  fx = {{0, "", 0, 6300, true, false, 0xA0, 9}, {0, "", 0, 6300, true, false, 0xA8, 0}};
  AttributeOrphans(0x180018, &fx, 1020, &m);
  CHECK(fx[0].parent == "SPPR105" && fx[1].parent == "SPPR105" && (fx[1].parent_flags & 0x400));
  auto icons = CollectStatus(0x180018, fx, 1020, 75, nullptr, 8);
  CHECK(icons.size() == 1 && icons[0].debuff && icons[0].rounds == 84);
  // long after the cast a new orphan is not guessed
  std::vector<StatusEffect> late{{0, "", 0, 900, true, false, 0xB0, 0}};
  AttributeOrphans(0x180018, &late, 1500, &m);
  CHECK(late[0].parent.empty());
}

TEST_CASE("tbc: a turn does not end before its spell has finished for this round") {
  using namespace pstf::tbc;
  SpellWaitIn in;
  CHECK(TurnWaitsForSpell(in) == SpellWait::None);  // nothing cast: ends at once
  // still in its casting animation, however long (Rune of Torment, speed 9): no 120-tick cut any more
  in.casting = true, in.cast_this_turn = true, in.since_action = 0, in.waited = 200;
  CHECK(TurnWaitsForSpell(in) == SpellWait::Casting);
  // the cast is over: a second for its projectile to appear
  in.casting = false, in.since_action = 10;
  CHECK(TurnWaitsForSpell(in) == SpellWait::Appearing);
  in.since_action = kSpellAppearTicks;
  CHECK(TurnWaitsForSpell(in) == SpellWait::None);
  // flying, then this round's pulse of a landed cloud, then an onset 100 ticks after landing, past 120 ticks waited
  in.flying = 1;
  CHECK(TurnWaitsForSpell(in) == SpellWait::Flying);
  in.flying = 0, in.lingering = 1, in.waited = 150;
  CHECK(TurnWaitsForSpell(in) == SpellWait::Pulsing);
  in.lingering = 0, in.onsets = 1, in.waited = 230;
  CHECK(TurnWaitsForSpell(in) == SpellWait::Landing);
  // a hung spell: 4 rounds after the end was asked, the turn ends anyway
  in.waited = kSpellHoldCapTicks;
  CHECK(kSpellHoldCapTicks == 300);
  CHECK(TurnWaitsForSpell(in) == SpellWait::Capped);
  // a melee swing has no projectile to wait for
  SpellWaitIn melee;
  melee.since_action = 3;
  CHECK(TurnWaitsForSpell(melee) == SpellWait::None);
}
TEST_CASE("tbc spells: a cinematic spell that freezes the world holds the turn past the 4-round cap") {
  SpellWaitIn in;
  in.flying = 1;
  in.waited = kSpellHoldCapTicks;
  CHECK(TurnWaitsForSpell(in) == SpellWait::Capped);  // an ordinary projectile: cut at 4 rounds
  in.cinematic = true;  // Celestial Host owns the game freeze: stage 13 of ~25 at 300 ticks
  CHECK(TurnWaitsForSpell(in) == SpellWait::Flying);
  in.waited = kCinematicCapTicks;
  CHECK(TurnWaitsForSpell(in) == SpellWait::Capped);  // still a safety cap
  in.flying = 0, in.cinematic = false, in.waited = 951;  // it just ended: the landing hold is not a cap
  in.cast_this_turn = true, in.since_action = 900, in.since_flying = 0;
  CHECK(TurnWaitsForSpell(in) == SpellWait::Landing);
}

TEST_CASE("tbc spells: the turn holds a few ticks after the last projectile is gone (Deathbolt)") {
  SpellWaitIn in;
  in.cast_this_turn = true;
  in.since_action = 200;
  in.since_flying = 0;  // the bolt just went: its death effect is still on its way
  CHECK(TurnWaitsForSpell(in) == SpellWait::Landing);
  in.since_flying = kSpellLandTicks;
  CHECK(TurnWaitsForSpell(in) == SpellWait::None);
  in.cast_this_turn = false;  // a melee swing: nothing to hold
  in.since_flying = 0;
  CHECK(TurnWaitsForSpell(in) == SpellWait::None);
}


TEST_CASE("tbc status: an effect until destroyed (Armor, ~INT_MAX ticks) has an icon with no countdown") {
  using namespace pstf::tbc;
  // spell_follow on SPWI102: op 8 and op 0 with 2147483640 ticks; the icon read r-28633114 (int32 overflow)
  std::vector<StatusEffect> fx{{1, "SPWI102", 0, 2147483640, true, false, 0, 0},
                               {1, "SPPR101", 0, 450, true, false, 0, 130}};
  StatusSpans spans;
  const auto icons = CollectStatus(1, fx, 0, kRoundTicks5s, &spans, 8);
  CHECK(icons.size() == 2);
  CHECK(icons[0].parent == "SPPR101" && icons[0].rounds == 6);  // the one with an end first
  CHECK(icons[1].parent == "SPWI102" && icons[1].rounds == 0 && icons[1].share == 1.0);
}

TEST_CASE("tbc status: an attached visual does not hold the icon past the spell's own effects (Friends)") {
  using namespace pstf::tbc;
  // spell_follow SPWI114 at owner turn start 22: CHR (op 6) gone, its sparkle (an attached object) 105 ticks left
  StatusSpans spans;
  std::vector<StatusEffect> fx{{1, "SPWI114", 0, 30, true, false, 0, 6}, {1, "SPWI114", 0, 105, true, false, 0, -1}};
  auto icons = CollectStatus(1, fx, 0, kRoundTicks5s, &spans, 8);
  CHECK(icons.size() == 1 && icons[0].rounds == 1);  // the CHR's last round, not the sparkle's 2
  // a spell with only an attached object (Submerge the Will's shield) keeps its icon from it
  std::vector<StatusEffect> only{{1, "SPWI119", 0, 1080, true, false, 0, -1}};
  icons = CollectStatus(1, only, 0, kRoundTicks5s, &spans, 8);
  CHECK(icons.size() == 1 && icons[0].rounds == 15);
}

TEST_CASE("tbc status: a parentless effect at a reused address is a new effect (Black-Barbed Shield after Submerge)") {
  using namespace pstf::tbc;
  OrphanMap m;
  // Submerge the Will's AC: parentless, given SPWI119 while it was cast
  std::vector<StatusEffect> fx{{0, "", 0, 3000, true, false, 0xABC, 0}};
  AttributeOrphans(1, &fx, 0, &m);  // first look
  m.NoteCast("SPWI119", 0, 10);
  fx = {{0, "", 0, 3000, true, false, 0xABC, 0}, {0, "", 0, 2990, true, false, 0xDEF, 33}};
  AttributeOrphans(1, &fx, 10, &m);
  CHECK(fx[1].parent == "SPWI119");
  // the suite removes it; Black-Barbed Shield's AC (op 0, 450 ticks) lands at that freed address while it is cast
  m.NoteCast("SPWI203", 0, 500);
  fx = {{0, "", 0, 450, true, false, 0xDEF, 0}};
  AttributeOrphans(1, &fx, 500, &m);
  CHECK(fx[0].parent == "SPWI203");
  // a look later it keeps that parent (same opcode, time falling)
  fx = {{0, "", 0, 440, true, false, 0xDEF, 0}};
  AttributeOrphans(1, &fx, 1000, &m);
  CHECK(fx[0].parent == "SPWI203");
}

TEST_CASE("tbc status: an attached object's re-applied parentless effects do not shorten its icon (Antimagic Shell)") {
  using namespace pstf::tbc;
  // spell_follow SPWI601, end of the cast turn: 13 resistances of 60 ticks (engine-built, given the spell by
  // AttributeOrphans) and the shell object with 4500 ticks: r60, not r1
  StatusSpans spans;
  StatusEffect res{1, "SPWI601", 0, 60, true, false, 1, 0};
  res.orphan = true;
  std::vector<StatusEffect> fx{res, {1, "SPWI601", 0, 4500, true, false, 0, -1}};
  auto icons = CollectStatus(1, fx, 0, kRoundTicks5s, &spans, 8);
  CHECK(icons.size() == 1 && icons[0].rounds == 60);
  // engine-built effects alone (Power of One's strength) keep their own length
  std::vector<StatusEffect> alone{res};
  alone[0].left = 600;
  icons = CollectStatus(1, alone, 0, kRoundTicks5s, &spans, 8);
  CHECK(icons.size() == 1 && icons[0].rounds == 8);
}

TEST_CASE("hover card: level is the highest class level; subtitle is the class or the stance") {
  CHECK(HoverLevel(3, 9, 0) == 9);
  CHECK(HoverLevel(12, 0, 0) == 12);
  CHECK(std::string(HoverSubtitle(7, 2, true)) == "Fighter / Mage");
  CHECK(std::string(HoverSubtitle(3, 2, true)) == "Priest");
  CHECK(std::string(HoverSubtitle(99, 2, true)) == "Companion");
  CHECK(std::string(HoverSubtitle(2, 255, false)) == "Hostile");
  CHECK(std::string(HoverSubtitle(2, 128, false)) == "Neutral");
  CHECK(std::string(HoverSubtitle(2, 4, false)) == "Ally");
}

TEST_CASE("combat text menu: one Lua chunk, a menu per line, balanced braces") {
  const std::string m = BuildCombatTextMenu();
  CHECK(std::count(m.begin(), m.end(), '`') == 2);
  CHECK(std::count(m.begin(), m.end(), '{') == std::count(m.begin(), m.end(), '}'));
  CHECK(m.find("name 'PSTF_CT1'") != std::string::npos);
  CHECK(m.find("name 'PSTF_CT24'") != std::string::npos);
  CHECK(m.find("pstfCtGuard") != std::string::npos);
  const std::string h = BuildHudMenu();
  CHECK(h.find("pstfHovIN1") != std::string::npos);  // status names on the hover card
  CHECK(h.find("pstfHoverLv") != std::string::npos);
}

TEST_CASE("tbc script timers: one round of timer time per round") {
  // a 75-tick round, its own turn ran 45 ticks (3 s counted by CheckTimers): 30 ticks owed = 2 s
  TimerClock c;
  CHECK(c.Owe(75, 45) == 2 && c.debt == 0);
  CHECK(c.Owe(75, 68) == 0 && c.debt == 7);   // 7 ticks carried
  CHECK(c.Owe(75, 67) == 1 && c.debt == 0);   // 7 + 8 = 15 -> 1 s
  CHECK(c.Owe(75, 200) == 0 && c.debt == 0);  // a long turn (its spell landing) owes nothing
}

TEST_CASE("tbc economy: no nudge while a step's variable waits behind its Wait") {
  // S12 apprentice (3012APR2): Spell spent, then [Wait, IncrementGlobal]: the nudge's ClearAI would drop the step
  const uint16_t step[] = {63, 109};
  CHECK(QueueHoldsScriptState(step, 2));
  const uint16_t set[] = {63, 30};      // SetGlobal
  const uint16_t timer[] = {63, 115};   // SetGlobalTimer
  CHECK(QueueHoldsScriptState(set, 2) && QueueHoldsScriptState(timer, 2));
  const uint16_t chatter[] = {63, 369, 229};  // Wait; PlaySequence; FaceObject: nudged as before
  CHECK(!QueueHoldsScriptState(chatter, 3));
  CHECK(!QueueHoldsScriptState(nullptr, 0));
}

TEST_CASE("tbc stalemate: whole quiet rounds end the fight") {
  // S31: three party members and a Hive thug that cannot reach each other, 4 turns a round, HP 82 in all
  Stalemate s;
  bool end = false;
  for (uint32_t r = 1; r <= 3; ++r)
    for (int t = 0; t < 4; ++t) {
      CHECK(!end);
      s.Turn(false, false, false, 82, 4);
      end = s.Round(t == 3 ? r + 1 : r, 3);  // the last turn's end hands over to the next round
    }
  CHECK(end && s.quiet == 3);  // round 4 would start: three whole quiet rounds, the fight ends
  // a swing, a walk, a cast, an HP change or a join breaks the streak
  Stalemate b;
  b.Turn(false, false, false, 82, 4); b.Round(1, 2);
  b.Turn(false, false, false, 82, 4); b.Round(2, 2);
  CHECK(b.quiet == 1);
  b.Turn(false, false, false, 80, 4); b.Round(3, 2);  // a hit in round 2
  CHECK(b.quiet == 0);
  b.Turn(false, false, false, 80, 4); b.Round(4, 2);
  CHECK(b.quiet == 1);
  b.Turn(false, false, false, 80, 5); b.Round(5, 2);  // a join in round 4
  CHECK(b.quiet == 0);
  b.Turn(true, false, false, 80, 5); b.Round(6, 2);
  b.Turn(false, true, false, 80, 5); b.Round(7, 2);
  b.Turn(false, false, true, 80, 5); b.Round(8, 2);
  CHECK(b.quiet == 0);
  CHECK(!Stalemate{}.Round(9, 0));  // limit 0 = off
}

TEST_CASE("tbc stalemate: no new fight until a creature changed") {
  StaleHold h;
  CHECK(!h.Holds({{8, 2252, 2110, 16}}));  // no stalemate yet
  h.at = {{0x80008, 2252, 2110, 16}, {0x950095, 2132, 2150, 16}};
  CHECK(h.Holds({{0x80008, 2252, 2110, 16}, {0x950095, 2132, 2150, 16}}));
  CHECK(h.Holds({{0x80008, 2260, 2120, 16}, {0x950095, 2132, 2150, 16}}));   // a step on the spot
  CHECK(!h.Holds({{0x80008, 2252, 2110, 12}, {0x950095, 2132, 2150, 16}}));  // hurt
  CHECK(!h.Holds({{0x80008, 2300, 2110, 16}, {0x950095, 2132, 2150, 16}}));  // walked 48 px
  CHECK(!h.Holds({{0x80008, 2252, 2110, 16}, {0x40004, 2000, 2000, 30}}));   // someone new
}

TEST_CASE("tbc economy: a spent Spell gives its action back") {
  Economy e(31.0, 51, false);
  e.Feed(1, 0, false, 31);  // Spell starts: the action is taken
  CHECK(e.action_used);
  e.Refund();               // it ended without a cast
  CHECK(!e.action_used && !e.acting);
  const auto st = e.Feed(1, 0, false, 63);  // the Wait behind it is a lead Wait again, not the turn's end
  CHECK(!st.end_turn);
}

TEST_CASE("tbc economy: a flight still searching its path is not nudged at once") {
  Economy e(31.0, 51, false);
  bool nudge = false;
  for (int i = 0; i < 20; ++i) nudge = nudge || e.Feed(1, 0, false, 355).nudge;  // RunAwayFromNoLeaveArea, no step yet
  CHECK(!nudge);
  for (int i = 0; i < 15; ++i) nudge = nudge || e.Feed(1, 0, false, 355).nudge;  // still not moving at 30 ticks
  CHECK(nudge);
}

TEST_CASE("tbc economy: a cosmetic action still playing is not nudged at once") {
  Economy e(31.0, 51, false);
  bool nudge = false;
  for (int i = 0; i < 30; ++i) nudge = nudge || e.Feed(1, 0, false, 454).nudge;  // PlaySequence before its ForceSpell
  CHECK(!nudge);
  for (int i = 0; i < 20; ++i) nudge = nudge || e.Feed(1, 0, false, 454).nudge;
  CHECK(nudge);  // a sequence that never ends still gets the attack
}

