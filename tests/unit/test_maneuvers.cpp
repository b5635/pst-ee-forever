// SPDX-License-Identifier: GPL-2.0-or-later
// L0 tests for the turn-based maneuvers (src/modules/tbc/maneuvers.h): GemRB fork rules, the engine's
// melee reach (CGameSprite::Attack lin 4ED490 L304-353) and the directed AI's choices.
#include "mini_test.h"
#include "modules/tbc/maneuvers.h"

using namespace pstf::tbc;

TEST_CASE("tbc run: action spent, remaining movement plus max (31/31 -> 62, 0/31 -> 31)") {
  Economy e(31.0, 51, true);
  Stance s;
  CHECK(ApplyRun(&e, &s));
  CHECK(e.move_ft == 62.0);
  CHECK(e.max_ft == 31.0);
  CHECK(e.action_used);
  CHECK(s.ran);
  CHECK(!ApplyRun(&e, &s));  // one action
  Economy z(31.0, 51, true);
  z.move_ft = 0.0;
  Stance s2;
  CHECK(ApplyRun(&z, &s2));
  CHECK(z.move_ft == 31.0);
  Economy busy(31.0, 51, true);
  busy.acting = true;
  CHECK(!CanRun(busy));
}

TEST_CASE("tbc withdraw: halves movement, spends the action, refused with no movement") {
  Economy e(31.0, 51, true);
  e.move_ft = 20.0;
  Stance s;
  CHECK(ApplyWithdraw(&e, &s));
  CHECK(e.move_ft == 10.0);
  CHECK(e.action_used);
  CHECK(s.withdrawn);
  Economy none(31.0, 51, true);
  none.move_ft = 0.0;
  CHECK(!CanWithdraw(none));
  Economy used(31.0, 51, true);
  used.action_used = true;
  CHECK(!CanWithdraw(used));
}

TEST_CASE("tbc block: fighters but the Nameless One and Morte; attacks become stacks") {
  CHECK(MayBlock(2, 3));    // Vhailor, fighter
  CHECK(MayBlock(7, 7));    // Dak'kon, fighter/mage
  CHECK(!MayBlock(2, 9));   // Morte
  CHECK(!MayBlock(2, 2));   // the Nameless One
  CHECK(!MayBlock(4, 8));   // Annah, thief
  CHECK(MayTrip(2, 9));     // Morte trips
  CHECK(!MayTrip(2, 2));
  CHECK(!MayTrip(1, 5));    // Ignus, mage
  Economy e(31.0, 51, true);
  Stance s;
  s.takedown = Takedown::Trip;
  CHECK(!ApplyBlock(&e, &s, true, 0));
  CHECK(ApplyBlock(&e, &s, true, 2));
  CHECK(s.block_stacks == 2);
  CHECK(s.takedown == Takedown::None);
  CHECK(e.action_used);
  CHECK(!ApplyBlock(&e, &s, true, 2));
  Economy f(31.0, 51, true);
  Stance p;
  p.prone = true;
  CHECK(!CanBlock(f, p, true, 1));
}

TEST_CASE("tbc block roll: d20 + AC 4 beats THAC0, 1 fails, 20 succeeds, at least the attack's d20") {
  CHECK(!BlockRollHits(1, 0));
  CHECK(BlockRollHits(20, 40));
  CHECK(BlockRollHits(13, 16));   // 17 > 16
  CHECK(!BlockRollHits(12, 16));  // 16 is not > 16
  CHECK(BlockStops(15, 16, 15));
  CHECK(!BlockStops(15, 16, 16));  // the attack rolled higher
  CHECK(!BlockStops(12, 16, 3));   // the block itself missed
  CHECK(BlockStops(20, 30, 20));
}

TEST_CASE("tbc trip: armed and disarmed, -2 to hit as THAC0 + 2; knockdown for THAC0 15- AI") {
  Economy e(31.0, 51, true);
  Stance s;
  CHECK(ToggleTrip(e, &s, true));
  CHECK(s.takedown == Takedown::Trip);
  CHECK(TakedownThac0(s.takedown) == 2);
  CHECK(ToggleTrip(e, &s, true));
  CHECK(s.takedown == Takedown::None);
  CHECK(!ToggleTrip(e, &s, false));
  e.action_used = true;
  CHECK(!ToggleTrip(e, &s, true));  // attacked already
  CHECK(MayKnockdown(false, 15));
  CHECK(!MayKnockdown(false, 16));
  CHECK(!MayKnockdown(true, 5));
  CHECK(TakedownThac0(Takedown::Knockdown) == 0);
  Economy k(31.0, 51, false);
  Stance ks;
  CHECK(ArmKnockdown(k, &ks, true));
  CHECK(ks.takedown == Takedown::Knockdown);
  CHECK(!ArmKnockdown(k, &ks, true));
}

TEST_CASE("tbc stance: a new turn clears budgets, keeps prone (it stands up then)") {
  Stance s;
  s.ran = s.withdrawn = s.reaction_used = s.prone = true;
  s.block_stacks = 3;
  s.takedown = Takedown::Trip;
  NewTurn(&s);
  CHECK(!s.ran);
  CHECK(!s.withdrawn);
  CHECK(!s.reaction_used);
  CHECK(s.block_stacks == 0);
  CHECK(s.takedown == Takedown::None);
  CHECK(s.prone);
}

TEST_CASE("tbc melee reach: engine Attack range in cells, personal spaces") {
  // melee range field 1 -> 2, both personal space 3: 2 + 1 + 1 - 1 = 3 cells
  CHECK(ReachCells(kMeleeRangeField, 3, 3) == 3);
  CHECK(ReachCells(kMeleeRangeField, 1, 1) == 1);
  CHECK(ReachCells(kMeleeRangeField, 5, 3) == 4);
  // cell deltas: x/16, y/12 -> (100,100) is cell (6,8); (148,100) cell (9,8): 3 cells
  CHECK(CellDist2(100, 100, 148, 100) == 9);
  CHECK(InReach(100, 100, 148, 100, 3));
  CHECK(!InReach(100, 100, 164, 100, 3));  // cell 10: 4 cells
  // CAIUtil::CountSquares: the larger cell delta (Chebyshev), not the Euclidean distance.
  CHECK(CellDist2(100, 100, 148, 136) == 9);
  CHECK(InReach(100, 100, 148, 136, 3));   // 3 across, 3 down: 3 squares
  CHECK(!InReach(100, 100, 164, 136, 3));  // 4 across
  // Morte (personal space 3) swung at a Hive thug 4 cells away, 1 down (tbc_moves2 trace): reach 3 + RANGE_FUDGE.
  CHECK(!InReach(2232, 1806, 2168, 1794, ReachCells(kMeleeRangeField, 3, 3)));
  CHECK(InReach(2232, 1806, 2168, 1794, ReachCells(kMeleeRangeField, 3, 3) + kRangeFudge));
}

TEST_CASE("tbc attack of opportunity: leaving reach, not withdrawn, once, not prone") {
  Stance r;
  CHECK(ProvokesNow(true, false, false, r));
  CHECK(!ProvokesNow(true, true, false, r));    // still in reach
  CHECK(!ProvokesNow(false, false, false, r));  // never was
  CHECK(!ProvokesNow(true, false, true, r));    // withdrew
  r.reaction_used = true;
  CHECK(!ProvokesNow(true, false, false, r));
  Stance p;
  p.prone = true;
  CHECK(!ProvokesNow(true, false, false, p));
  CHECK(CanReact(0));
  CHECK(CanReact(0x10));    // invisible still swings
  CHECK(!CanReact(0x20));   // helpless (op 39 prone)
  CHECK(!CanReact(0x4));    // panic
  CHECK(!CanReact(-1));     // gone
  Reaction a;
  a.reactor = 5;
  CHECK(!a.Feed(1, false));
  CHECK(!a.Feed(1, true));
  CHECK(!a.Feed(9, false));
  CHECK(a.Feed(1, false));  // 10 ticks after the swing
  Reaction b;
  b.reactor = 6;
  CHECK(!b.Feed(89, false));
  CHECK(b.Feed(1, false));  // never swung: 90 ticks cap (a whole attack round at kReactionPace)
}

TEST_CASE("tbc directed AI: run to close, withdraw to flee, block when hurt and surrounded, takedowns") {
  AiView v;
  v.melee_intent = true;
  v.gap_cells = 40, v.reach_cells = 3, v.move_ft = 31.0;
  CHECK(ChooseAiMove(v) == AiMove::Run);  // 37 cells > 31
  v.gap_cells = 34;
  CHECK(ChooseAiMove(v) == AiMove::None);  // 31 cells: a walk gets there
  v.gap_cells = 3;
  CHECK(ChooseAiMove(v) == AiMove::None);  // roll 100: no takedown
  v.roll100 = 20, v.may_trip = true;
  CHECK(ChooseAiMove(v) == AiMove::Trip);
  v.may_knockdown = true;
  CHECK(ChooseAiMove(v) == AiMove::Knockdown);
  v.roll100 = 21;
  CHECK(ChooseAiMove(v) == AiMove::None);
  v.may_block = true, v.adjacent_foes = 2, v.hp = 10, v.max_hp = 30;
  CHECK(ChooseAiMove(v) == AiMove::Block);  // 33 % < 35 %
  v.hp = 11;
  CHECK(ChooseAiMove(v) == AiMove::None);  // 36 %
  AiView f;
  f.flee_intent = true, f.adjacent_foes = 1;
  CHECK(ChooseAiMove(f) == AiMove::None);  // enemies never withdraw
  f.may_withdraw = true;                    // an ally (party side) does
  CHECK(ChooseAiMove(f) == AiMove::Withdraw);
  f.adjacent_foes = 0;
  CHECK(ChooseAiMove(f) == AiMove::None);
  f.adjacent_foes = 1, f.action_free = false;
  CHECK(ChooseAiMove(f) == AiMove::None);
  CHECK(IsFleeAction(29) && IsFleeAction(184) && IsFleeAction(355) && !IsFleeAction(23));
  CHECK(IsAttackAction(3) && IsAttackAction(400) && !IsAttackAction(31));
}

TEST_CASE("tbc aoo lookahead: a running step past the reach edge still provokes, staying in reach does not") {
  // Reactor at (160, 120); reach 3 cells (two personal-space-3 creatures). The mover stands 2 cells right.
  const int rx = 160, ry = 120, reach = ReachCells(kMeleeRangeField, 3, 3);
  CHECK(reach == 3);
  // Its path: one 16 px cell further (still in reach: dx 3), then two more (dx 4, 5: out).
  const std::vector<std::pair<int, int>> run{{216, 126}, {232, 126}, {248, 126}};
  CHECK(StepLeavesReach(rx, ry, 200, 126, run, reach, kAooLookAheadPx));
  // Looking at the next cell alone (the old check) misses it: that cell is still in reach.
  CHECK(InReach(rx, ry, 216, 126, reach));
  // A path that circles inside the reach does not provoke; one already out of reach does not either.
  const std::vector<std::pair<int, int>> stay{{184, 126}, {168, 138}, {152, 138}};
  CHECK(!StepLeavesReach(rx, ry, 200, 126, stay, reach, kAooLookAheadPx));
  CHECK(!StepLeavesReach(rx, ry, 300, 126, run, reach, kAooLookAheadPx));
  // A waypoint far away (the engine keeps one point per straight stretch): the walk there leaves the reach.
  const std::vector<std::pair<int, int>> far{{488, 126}};
  CHECK(StepLeavesReach(rx, ry, 200, 126, far, reach, kAooLookAheadPx));
  // Far ahead (beyond 48 px of walking) does not count yet.
  const std::vector<std::pair<int, int>> later{{184, 126}, {168, 126}, {184, 126}, {200, 126}, {280, 126}};
  CHECK(!StepLeavesReach(rx, ry, 200, 126, later, reach, kAooLookAheadPx));
}

TEST_CASE("tbc trip pace: half-speed wind-up, 1.5x follow-through, ends with the attack sequence") {
  TripPace p;
  CHECK(p.Updates(true) == 1);  // inactive: one update a tick
  p.Start(0x180018);
  int sum = 0;
  for (int i = 0; i < 10; ++i) sum += p.Updates(true);
  CHECK(sum == 5);  // half speed
  p.Resolved();
  sum = 0;
  for (int i = 0; i < 10; ++i) sum += p.Updates(true);
  CHECK(sum == 15);  // 1.5x
  CHECK(p.Active());
  CHECK(p.Updates(false) == 1);  // the attack sequence ended: normal pace
  CHECK(!p.Active());
  p.Start(1);
  for (uint32_t i = 0; i <= TripPace::kCapTicks; ++i) p.Updates(true);
  CHECK(!p.Active());  // capped
}

TEST_CASE("tbc prone pose: die cycle forwards, held on the last frame, backwards to stand up") {
  PronePose p;
  CHECK(p.Back() == -1);
  p.Fall();
  p.len = 4;
  CHECK(p.Back() == 3);  // first frame of the fall
  p.Tick(4);
  CHECK(p.Back() == 2);
  p.Tick(4);
  p.Tick(4);
  CHECK(p.Back() == 0);
  p.Tick(4);
  CHECK(p.phase == PronePose::Phase::Down);
  CHECK(p.Back() == 0);
  p.Tick(4);
  CHECK(p.Back() == 0);  // held
  p.Rise();
  CHECK(p.Back() == 0);  // the last frame first: the fall played backwards
  p.Tick(4);
  CHECK(p.Back() == 1);
  p.Tick(4);
  p.Tick(4);
  CHECK(p.Back() == 3);  // first die frame = standing
  p.Tick(4);
  CHECK(p.Back() == -1);  // done: back to its ready stance
  CHECK(kProneAcPenalty == 4);
}

TEST_CASE("tbc AI stops short: runs instead while the action is free") {
  Economy e(31.0, 51, false);
  e.move_ft = 0.0;
  CHECK(AiRunsWhenShort(e, true, true));
  CHECK(!AiRunsWhenShort(e, false, true));  // not walking to attack
  CHECK(!AiRunsWhenShort(e, true, false));
  Economy party(31.0, 51, true);
  party.move_ft = 0.0;
  CHECK(!AiRunsWhenShort(party, true, true));  // the player decides
  e.action_used = true;
  CHECK(!AiRunsWhenShort(e, true, true));  // nothing to spend
  Economy left(31.0, 51, false);
  CHECK(!AiRunsWhenShort(left, true, true));  // still has movement
  Stance s;
  Economy r(31.0, 51, false);
  r.move_ft = 0.0;
  CHECK(ApplyRun(&r, &s));
  CHECK(r.move_ft == 31.0);
}

TEST_CASE("tbc aoo late catch: a floating mover one cell past the reach still provokes") {
  CHECK(LeftReachLate(true, false, true));
  CHECK(!LeftReachLate(false, false, true));  // never was in reach
  CHECK(!LeftReachLate(true, true, true));    // still in reach
  CHECK(!LeftReachLate(true, false, false));  // jumped further than the fudge cell: too far to swing
  CHECK(kRangeFudge == 1);
}
