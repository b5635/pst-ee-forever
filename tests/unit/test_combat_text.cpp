// SPDX-License-Identifier: GPL-2.0-or-later
// L0 tests for the BG3 combat text (src/modules/tbc/combat_text.h). Times from the reference clip
// (youtube.com/watch?v=mfZz9N57glw at 5 fps): "Miss!" stays put ~2.4 s then fades, an ended status is struck
// through from the left ~0.3 s after it shows; at 25-30 fps: a number glides ~0.3 s out from the body and stops,
// numbers one after another stop apart (Magic Missile's 3 5 3 at 0:41).
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
  const CtLine& st = b.Add(1, 140, 60, CtKind::StatusOn, {"Blessing"}, 1100);  // it walked: same point
  CHECK(st.slot == 1);
  CHECK(st.ax == 100);
  CHECK(st.ay == 50);
  const CtLine& other = b.Add(2, 300, 80, CtKind::Text, {"Run!"}, 1100);
  CHECK(other.slot == 0);
  CHECK(other.ax == 300);
  // "Miss!" gone (t0 1000 + life): the next line takes slot 0 again, under "Blessing"
  const CtLine& late = b.Add(1, 500, 500, CtKind::Crit, {"Critical Hit!"}, 1000 + kCtLifeMs);
  CHECK(late.slot == 0);
  CHECK(late.ax == 100);  // "Blessing" still holds the point
  CHECK(b.Lines().size() == 3);
  // all gone: a new stack starts where the creature is now
  const CtLine& fresh = b.Add(1, 520, 510, CtKind::Text, {"Block!"}, 10000);
  CHECK(fresh.slot == 0);
  CHECK(fresh.ax == 520);
  CHECK(b.Lines().size() == 1);
}

TEST_CASE("numbers glide out from the body's middle, ease out, stop, and stop apart from each other") {
  // the glide (reference clip: ~0.3 s, fast first)
  CHECK(CtDrift(0) == 0.0);
  CHECK(CtDrift(kCtDriftMs / 2) == 0.875);  // ease-out cubic: 7/8 of the way at half time
  CHECK(CtDrift(kCtDriftMs) == 1.0);
  CHECK(CtDrift(2000) == 1.0);
  CombatTextBook b;
  const CtLine& a = b.Add(7, 200, 100, CtKind::Damage, {"3", DamageColor(0x40), 3}, 0);
  CHECK(a.ax == 200);  // starts at the point given: the body's middle
  CHECK(a.ay == 100);
  CHECK(a.ex == kCtFan[0][0]);
  CHECK(a.ey == kCtFan[0][1]);
  int x = 0, y = 0;
  CombatTextBook::Position(a, 0, &x, &y);
  CHECK(x == 200);
  CHECK(y == 100);
  CombatTextBook::Position(a, kCtDriftMs / 2, &x, &y);
  CHECK(y == 100 - 26);  // -30 * 0.875 = -26.25
  CombatTextBook::Position(a, 1500, &x, &y);
  CHECK(y == 100 + kCtFan[0][1]);  // stopped there
  // Magic Missile: three more bolts, 80 ms apart (no merge: past kCtMergeMs) -> three other end spots
  b.Add(7, 200, 100, CtKind::Damage, {"5", DamageColor(0x40), 5}, 120);
  b.Add(7, 200, 100, CtKind::Damage, {"3", DamageColor(0x40), 3}, 240);
  b.Add(7, 200, 100, CtKind::Damage, {"4", DamageColor(0x40), 4}, 360);
  CHECK(b.Lines().size() == 4);
  for (size_t i = 0; i < b.Lines().size(); ++i)
    for (size_t j = i + 1; j < b.Lines().size(); ++j) {
      const CtLine& p = b.Lines()[i];
      const CtLine& q = b.Lines()[j];
      const bool touch = std::abs((p.ay + p.ey) - (q.ay + q.ey)) < kCtNumH && CtOverlapX(p.ax + p.ex, p.w, q.ax + q.ex, q.w);
      CHECK(!touch);
    }
  CHECK(b.Lines()[1].ex == kCtFan[1][0]);  // left, then right, then up-left
  CHECK(b.Lines()[2].ex == kCtFan[2][0]);
  CHECK(b.Lines()[3].ey == kCtFan[3][1]);
  // numbers take no text slot: a "Miss!" over the same creature is still slot 0
  CHECK(b.Add(7, 200, 60, CtKind::Miss, {"Miss!"}, 400).slot == 0);
}

TEST_CASE("heals: white +N, and heals within kCtMergeMs add up, never into damage") {
  CHECK(HealText(5) == "+5");
  CHECK(kCtHeal == kCtNumber);  // white
  CombatTextBook b;
  CHECK(b.Add(7, 0, 0, CtKind::Heal, {"5", kCtHeal, 5}, 1000).segs[0].text == "+5");
  const CtLine& h = b.Add(7, 0, 0, CtKind::Heal, {"2", kCtHeal, 2}, 1050);
  CHECK(b.Merged());
  CHECK(h.segs[0].text == "+7");
  b.Add(7, 0, 0, CtKind::Damage, {"3", kCtNumber, 3}, 1060);  // white weapon damage, same moment: its own number
  CHECK(!b.Merged());
  CHECK(b.Lines().size() == 2);
  CHECK(b.Lines().back().segs[0].text == "3");
  b.Add(7, 0, 0, CtKind::Heal, {"1", kCtHeal, 1}, 1000 + kCtMergeMs);  // past the window: a new heal
  CHECK(!b.Merged());
  CHECK(b.Lines().back().segs[0].text == "+1");
}

TEST_CASE("numbers of one damage type within kCtMergeMs add up: a dagger's 1d6 + 1 shows 7") {
  CombatTextBook b;
  b.Add(7, 0, 0, CtKind::Damage, {"6", kCtNumber, 6}, 1000);
  CHECK(!b.Merged());
  const CtLine& n = b.Add(7, 0, 0, CtKind::Damage, {"1", kCtNumber, 1}, 1000);  // same tick
  CHECK(b.Merged());
  CHECK(n.segs.size() == 1);
  CHECK(n.segs[0].text == "7");
  CHECK(n.segs[0].value == 7);
  CHECK(b.Lines().size() == 1);
  b.Add(7, 0, 0, CtKind::Damage, {"2", kCtNumber, 2}, 1000 + kCtMergeMs - 1);  // still within the window
  CHECK(b.Lines().back().segs[0].text == "9");
  // another type: its own number
  b.Add(7, 0, 0, CtKind::Damage, {"4", DamageColor(0x8), 4}, 1050);
  CHECK(!b.Merged());
  CHECK(b.Lines().size() == 2);
  // the same type but later than the window: its own number
  b.Add(7, 0, 0, CtKind::Damage, {"3", kCtNumber, 3}, 1000 + kCtMergeMs);
  CHECK(!b.Merged());
  CHECK(b.Lines().size() == 3);
  CHECK(b.Lines().back().segs[0].text == "3");
  // another creature's number never adds
  b.Add(8, 0, 0, CtKind::Damage, {"1", kCtNumber, 1}, 1100);
  CHECK(!b.Merged());
  CHECK(b.Lines().size() == 4);
  // text (no value) never adds
  b.Add(9, 0, 0, CtKind::Damage, {"x"}, 2000);
  b.Add(9, 0, 0, CtKind::Damage, {"y"}, 2000);
  CHECK(b.Lines().back().segs[0].text == "y");
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

TEST_CASE("neighbours: only lines that would touch share rows; others stay level over their own heads") {
  CHECK(CtLineWidth(CtKind::Text, {{"Initiative: 4"}}) == 13 * kCtCharPx);
  CHECK(CtLineWidth(CtKind::Damage, {{"12"}, {"7"}}) == 3 * kCtDigitPx + kCtNumGapPx);
  CHECK(CtLineWidth(CtKind::Text, {{"@274"}}) == 12 * kCtCharPx);
  CHECK(CtOverlapX(0, 100, 100, 100));    // 100 apart, 50 + 50 wide halves + air: touching
  CHECK(!CtOverlapX(0, 40, 100, 40));     // short lines 100 apart: apart
  CombatTextBook b;
  // two party members 70 px apart: short lines, side by side, both just above their own heads
  const CtLine& a = b.Add(1, 100, 50, CtKind::Miss, {"Miss!"}, 0);
  const CtLine& c = b.Add(2, 170, 56, CtKind::Miss, {"Miss!"}, 0);
  CHECK(a.slot == 0);
  CHECK(c.slot == 0);
  CHECK(c.ay == 56);  // its own head height
  // a party of four 40 px apart rolls initiative together: long lines that touch share one stack, upward
  CombatTextBook i;
  for (int k = 0; k < 4; ++k) i.Add(10 + k, 100 + 40 * k, 50 + 3 * k, CtKind::Text, {"Initiative: " + std::to_string(k)}, 0);
  for (int k = 0; k < 4; ++k) {
    CHECK(i.Lines()[k].slot == k);
    CHECK(i.Lines()[k].ay == 50);
  }
  // one standing 300 px off keeps its own line at slot 0
  CHECK(i.Add(20, 400, 60, CtKind::Text, {"Initiative: 9"}, 0).slot == 0);
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
