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
#include "modules/tbc/hover.h"
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
