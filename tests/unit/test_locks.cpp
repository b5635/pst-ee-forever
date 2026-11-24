// SPDX-License-Identifier: GPL-2.0-or-later
// L0 tests for the lock rolls (src/modules/locks/lock_rules.h) and the engine's work point / range math
// (src/engine/locks.h, from CGameSprite::PickLock / BashDoor and CGameDoor::GetMoveDest, win 14041EA30 /
// 1403FE480 / 1401B2730). Skill values: the CREs at join (ANNAH lockpicking 30 + DEX 18 SKILLDEX 15 = 45,
// VHAIL STR 18/51, NAMELESS 0 / STR 12); lock difficulties from the shipped areas (AR0901 Container 12 = 60,
// AR0703 Container 3 = 40, AR0602 Chest 1 = 20, AR0202 Container 22 = 5).
#include "engine/locks.h"
#include "mini_test.h"
#include "modules/locks/lock_rules.h"

using namespace pstf::locks;
using pstf::rolls::ChancePercent;

TEST_CASE("locks: difficulty 100 is key only; unlocked locks are not rolled") {
  CHECK(Rollable(true, 0));
  CHECK(Rollable(true, 99));
  CHECK(!Rollable(true, 100));
  CHECK(!Rollable(false, 20));
}

TEST_CASE("locks pick: DC 10 + difficulty / 5, modifier lockpicking / 5 + luck") {
  CHECK(PickDc(0) == 10);
  CHECK(PickDc(5) == 11);
  CHECK(PickDc(20) == 14);
  CHECK(PickDc(40) == 18);
  CHECK(PickDc(60) == 22);
  CHECK(PickDc(99) == 29);
  CHECK(PickModifier(45, 0) == 9);  // Annah
  CHECK(PickModifier(100, 0) == 20);
  CHECK(PickModifier(0, 0) == 0);
  CHECK(PickModifier(-10, 0) == 0);  // DEX 9 or less takes a trained 0 below 0: no penalty past 0
  CHECK(PickModifier(44, 2) == 10);
}

TEST_CASE("locks bash: DC 12 + difficulty / 4, modifier (STR - 10) / 2 + exceptional + luck") {
  CHECK(BashDc(0) == 12);
  CHECK(BashDc(20) == 17);
  CHECK(BashDc(40) == 22);
  CHECK(BashDc(60) == 27);
  CHECK(BashModifier(12, 0, 0) == 1);   // Nameless One
  CHECK(BashModifier(17, 0, 0) == 3);   // Dak'kon
  CHECK(BashModifier(18, 51, 0) == 6);  // Vhailor 18/51
  CHECK(BashModifier(18, 50, 0) == 5);
  CHECK(BashModifier(18, 100, 0) == 7);
  CHECK(BashModifier(19, 0, 0) == 4);   // exceptional strength is 18/xx only
  CHECK(BashModifier(9, 0, -1) == -2);
}

TEST_CASE("locks chances: picking is the easier way in") {
  // Annah vs Vhailor on the same locks
  CHECK(ChancePercent(PickModifier(45, 0), PickDc(20)) == 80);
  CHECK(ChancePercent(BashModifier(18, 51, 0), BashDc(20)) == 50);
  CHECK(ChancePercent(PickModifier(45, 0), PickDc(40)) == 60);
  CHECK(ChancePercent(BashModifier(18, 51, 0), BashDc(40)) == 25);
  CHECK(ChancePercent(PickModifier(45, 0), PickDc(60)) == 40);
  CHECK(ChancePercent(BashModifier(18, 51, 0), BashDc(60)) == 5);  // natural 20 only
  // untrained Nameless One: a 5 lock picks on 11+ (DC 11), bashes on 12+ (DC 13, +1)
  CHECK(ChancePercent(PickModifier(0, 0), PickDc(5)) == 50);
  CHECK(ChancePercent(BashModifier(12, 0, 0), BashDc(5)) == 45);
  // a master thief still fails on a natural 1
  CHECK(ChancePercent(PickModifier(100, 0), PickDc(60)) == 95);
}

TEST_CASE("locks retry: a failure blocks the same lock for retry_hours of game time (4500 ticks an hour)") {
  CHECK(kTicksPerHour == 4500);
  CHECK(RetryBlocked(1000, 1000, 1));
  CHECK(RetryBlocked(1000, 5499, 1));
  CHECK(!RetryBlocked(1000, 5500, 1));
  CHECK(!RetryBlocked(1000, 1000, 0));               // 0 = retry at once
  CHECK(!RetryBlocked(1000, 1000 + 8 * 4500, 2));    // a rest passes 8 hours
  CHECK(!RetryBlocked(90000, 100, 1));               // the clock went back (another save): not blocked
  CHECK(RetryHoursLeft(1000, 1000, 2) == 2);
  CHECK(RetryHoursLeft(1000, 1000 + 4501, 2) == 1);
  CHECK(RetryHoursLeft(1000, 1000 + 9000, 2) == 0);
}

TEST_CASE("locks engine math: the door's nearer stand point and the cell range test") {
  using pstf::engine::CellDist2;
  using pstf::engine::NearerStand;
  using pstf::engine::Pt;
  // y counts 16/9 against x (GetMoveDest): 30 px off in y loses to 39 px off in x
  const Pt a{100, 130}, b{139, 100}, from{100, 100};
  CHECK(NearerStand(from, a, b).x == 139);
  CHECK(NearerStand(from, {100, 120}, b).y == 120);
  // 16 x 12 cells, each coordinate divided on its own: (0,0)-(64,0) is 4 cells: 16 = pick range, beyond bash's 9
  CHECK(CellDist2({0, 0}, {64, 0}) == 16);
  CHECK(CellDist2({0, 0}, {48, 0}) == 9);
  CHECK(CellDist2({15, 11}, {16, 12}) == 2);
  CHECK(CellDist2({0, 0}, {48, 24}) == 13);
}
