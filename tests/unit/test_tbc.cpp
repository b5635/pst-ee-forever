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
