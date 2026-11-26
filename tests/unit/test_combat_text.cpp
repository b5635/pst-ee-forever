// SPDX-License-Identifier: GPL-2.0-or-later
// L0 tests for the BG3 combat text (src/modules/tbc/combat_text.h). Times from the reference clip
// (youtube.com/watch?v=mfZz9N57glw at 5 fps): "Miss!" stays put ~2.4 s then fades, an ended status is struck
// through from the left ~0.3 s after it shows, numbers that land together share a row ("13 12").
#include "mini_test.h"
#include "modules/tbc/combat_text.h"

using namespace pstf::tbc;

TEST_CASE("attack text: Miss! or Critical Miss!, never both; Critical Hit!; a plain hit says nothing") {
  CHECK(std::string(AttackText(false, false, 7, 0)) == "Miss!");
  CHECK(std::string(AttackText(false, false, 1, 0)) == "Critical Miss!");
  CHECK(std::string(AttackText(true, false, 20, 0)) == "Critical Hit!");
  CHECK(std::string(AttackText(true, true, 19, 0)) == "Critical Hit!");  // the engine's crit range
  CHECK(std::string(AttackText(true, false, 12, 0)) == "");
  CHECK(std::string(AttackText(false, false, 5, 1)) == "");  // blocked: its own "Blocked!"
  CHECK(std::string(AttackText(false, false, 5, 2)) == "");  // takedown: its own "Trip: ..."
  CHECK(AttackKind("Critical Hit!") == CtKind::Crit);
  CHECK(AttackKind("Critical Miss!") == CtKind::Crit);
  CHECK(AttackKind("Miss!") == CtKind::Miss);
}

TEST_CASE("takedown text: a missed swing, a saved trip, a knock-down") {
  CHECK(TakedownText("Trip", TakedownResult::Missed) == "Trip: Missed");
  CHECK(TakedownText("Trip", TakedownResult::Failed) == "Trip: Failed");
  CHECK(TakedownText("Knockdown", TakedownResult::Success) == "Knockdown: Success");
}

TEST_CASE("engine floaters: Damage: N and Heal: N become numbers, other text stays text") {
  FloaterParse p = ParseFloater("Damage: 7", true);
  CHECK(p.kind == CtKind::Damage);
  CHECK(p.value == 7);
  p = ParseFloater("Heal: 12", false);
  CHECK(p.kind == CtKind::Heal);
  CHECK(p.value == 12);
  p = ParseFloater("Heal: 3", true);  // PST: a negative damage effect heals and says "Heal:"
  CHECK(p.kind == CtKind::Heal);
  CHECK(ParseFloater("Magic Missile", true).kind == CtKind::Text);
  CHECK(ParseFloater("Room 12", false).kind == CtKind::Text);  // a number outside a damage effect
  CHECK(ParseFloater("12", true).kind == CtKind::Text);        // no label: not the engine's damage line
}

TEST_CASE("damage colours: weapon damage white, elements coloured") {
  CHECK(DamageColor(0) == kCtNumber);       // crushing
  CHECK(DamageColor(0x10) == kCtNumber);    // piercing
  CHECK(DamageColor(0x100) == kCtNumber);   // slashing
  CHECK(DamageColor(0x80) == kCtNumber);    // missile
  CHECK(DamageColor(0x8) == Bgr(246, 124, 40));
  CHECK(DamageColor(0x200) == DamageColor(0x8));  // magic fire = fire
  CHECK(DamageColor(0x2) == DamageColor(0x400));  // cold = magic cold
  CHECK(DamageColor(0x4) != kCtNumber);
  CHECK(DamageColor(0x1) != DamageColor(0x20));
  CHECK(DamageColor(0x40) == Bgr(214, 72, 60));
  CHECK(kCtRed == 0x1E22D6);  // UI colours are 0xBBGGRR
}

TEST_CASE("timing: fades in, sits still, fades out, gone; the strike crosses after a beat") {
  CHECK(CtAlpha(0) == 0.0);
  CHECK(CtAlpha(60) == 0.5);
  CHECK(CtAlpha(kCtFadeInMs) == 1.0);
  CHECK(CtAlpha(2299) == 1.0);
  CHECK(CtAlpha(2550) == 0.5);
  CHECK(CtAlpha(kCtLifeMs) == 0.0);
  CHECK(CtStrike(0) == 0.0);
  CHECK(CtStrike(kCtStrikeDelayMs) == 0.0);
  CHECK(CtStrike(kCtStrikeDelayMs + 200) == 0.5);
  CHECK(CtStrike(kCtStrikeDelayMs + kCtStrikeMs) == 1.0);
  CHECK(CtStrike(2000) == 1.0);
}

TEST_CASE("stacking: a creature's lines keep its first point and fill slots from the top down") {
  CombatTextBook b;
  b.Add(1, 100, 50, CtKind::Miss, {"Miss!"}, 1000);
  const CtLine& dmg = b.Add(1, 140, 60, CtKind::Damage, {"12"}, 1100);  // it walked: same point
  CHECK(dmg.slot == 1);
  CHECK(dmg.ax == 100);
  CHECK(dmg.ay == 50);
  const CtLine& other = b.Add(2, 300, 80, CtKind::Text, {"Run!"}, 1100);
  CHECK(other.slot == 0);
  CHECK(other.ax == 300);
  // "Miss!" gone (t0 1000 + life): the next line takes slot 0 again, above the number row
  const CtLine& late = b.Add(1, 500, 500, CtKind::Crit, {"Critical Hit!"}, 1000 + kCtLifeMs);
  CHECK(late.slot == 0);
  CHECK(late.ax == 100);  // the number row still holds the point
  CHECK(b.Lines().size() == 3);
  // all gone: a new stack starts where the creature is now
  const CtLine& fresh = b.Add(1, 520, 510, CtKind::Text, {"Block!"}, 10000);
  CHECK(fresh.slot == 0);
  CHECK(fresh.ax == 520);
  CHECK(b.Lines().size() == 1);
}

TEST_CASE("numbers that land together share a row; later ones start a new row") {
  CombatTextBook b;
  b.Add(7, 0, 0, CtKind::Damage, {"13", kCtNumber}, 0);
  const CtLine& row = b.Add(7, 0, 0, CtKind::Damage, {"12", DamageColor(0x8)}, 150);
  CHECK(row.segs.size() == 2);
  CHECK(row.segs[1].color == DamageColor(0x8));
  CHECK(b.Lines().size() == 1);
  b.Add(7, 0, 0, CtKind::Heal, {"4", kCtHeal}, 200);
  b.Add(7, 0, 0, CtKind::Damage, {"1"}, 300);
  CHECK(b.Lines().back().segs.size() == 4);
  const CtLine& next = b.Add(7, 0, 0, CtKind::Damage, {"2"}, 350);  // the row is full
  CHECK(next.slot == 1);
  const CtLine& later = b.Add(7, 0, 0, CtKind::Damage, {"3"}, 350 + kCtJoinMs);  // too late for row 2
  CHECK(later.slot == 2);
  CHECK(b.Lines().size() == 3);
}

TEST_CASE("a full stack drops its oldest line; the book holds at most kCtLines") {
  CombatTextBook b;
  for (int i = 0; i < kCtSlots; ++i) b.Add(3, 0, 0, CtKind::Text, {"x" + std::to_string(i)}, 10 * i);
  const CtLine& l = b.Add(3, 0, 0, CtKind::Text, {"new"}, 100);
  CHECK(l.slot == 0);  // took the oldest line's slot
  CHECK(static_cast<int>(b.Lines().size()) == kCtSlots);
  for (int id = 10; id < 10 + kCtLines + 5; ++id) b.Add(id, id * 1000, 0, CtKind::Text, {"y"}, 200);  // far apart
  CHECK(static_cast<int>(b.Lines().size()) == kCtLines);
}

TEST_CASE("engine feedback text: trimmed; a cast shows the spell's name") {
  CHECK(CleanFloaterText(" Casts Magic Missile : Morte") == "Magic Missile");
  CHECK(CleanFloaterText(" Casts Blessing") == "Blessing");
  CHECK(CleanFloaterText("Level Up ") == "Level Up");
  CHECK(CleanFloaterText(" has gained 35 experience points.") == "has gained 35 experience points.");
}

TEST_CASE("neighbours share rows: a stack next to another goes under its lines, at its own x") {
  CombatTextBook b;
  b.Add(1, 100, 50, CtKind::Text, {"Blessing"}, 0);
  const CtLine& n1 = b.Add(2, 160, 70, CtKind::Text, {"Magic Missile"}, 10);
  CHECK(n1.ax == 160);
  CHECK(n1.ay == 50);
  CHECK(n1.slot == 1);
  const CtLine& far = b.Add(3, 600, 70, CtKind::Text, {"Miss!"}, 20);  // far away: its own stack
  CHECK(far.ay == 70);
  CHECK(far.slot == 0);
  const CtLine& own = b.Add(1, 100, 50, CtKind::Miss, {"Miss!"}, 30);  // the first creature's next line
  CHECK(own.slot == 2);
}
