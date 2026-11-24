// SPDX-License-Identifier: GPL-2.0-or-later
// L0 tests for the dialogue ability roll rules (src/modules/dlgroll/roll_logic.h). Conditions are the
// game's own (tools/dlg_checks.py dump DSKELW / DMORTE / DHARLOTD).
#include "mini_test.h"
#include "modules/dlgroll/roll_logic.h"

using namespace pstf::dlgroll;

TEST_CASE("dlgroll DC: (needed - 10) * 2, never under 10 (16 -> 12, 13 -> 10, 8 -> 10, 18 -> 16)") {
  CHECK(Dc(16) == 12);
  CHECK(Dc(13) == 10);
  CHECK(Dc(15) == 10);
  CHECK(Dc(8) == 10);
  CHECK(Dc(18) == 16);
  CHECK(Dc(25) == 30);
}

TEST_CASE("dlgroll modifier: floor((ability - 10) / 2) (12 -> 1, 13 -> 1, 10 -> 0, 9 -> -1, 8 -> -1, 3 -> -4)") {
  CHECK(Modifier(12) == 1);
  CHECK(Modifier(13) == 1);
  CHECK(Modifier(18) == 4);
  CHECK(Modifier(10) == 0);
  CHECK(Modifier(11) == 0);
  CHECK(Modifier(9) == -1);
  CHECK(Modifier(8) == -1);
  CHECK(Modifier(7) == -2);
  CHECK(Modifier(3) == -4);
}

TEST_CASE("dlgroll resolve: 16 STR check with 12 STR needs an 11; 20 always wins, 1 always loses") {
  const int dc = Dc(16), mod = Modifier(12);  // 12, +1
  CHECK(Resolve(11, mod, dc) == Outcome::Success);
  CHECK(Resolve(10, mod, dc) == Outcome::Fail);
  CHECK(Resolve(20, -4, 30) == Outcome::CritSuccess);
  CHECK(Resolve(1, 15, 10) == Outcome::CritFail);
  CHECK(Succeeded(Outcome::CritSuccess));
  CHECK(!Succeeded(Outcome::CritFail));
}

TEST_CASE("dlgroll abilities in a condition: GT and equal checks count, LT and non-abilities do not") {
  // DSKELW state 0 reply 8
  const auto a = CheckedAbilities(
      "Global(\"Skeleton_Examine\",\"GLOBAL\",1)\nNearbyDialog(\"DMorte\")\n!PartyHasItem(\"Prybar\")\nCheckStatGT(Protagonist,12,STR)");
  CHECK(a.size() == 1 && a[0] == kStatStr);
  // DHARLOTD state 10 reply 3: LT on WIS does not ask for more WIS; INT does
  const auto b = CheckedAbilities("CheckStatLT(Protagonist,13,WIS)\nCheckStatGT(Protagonist,12,INT)\nGlobal(\"x\",\"GLOBAL\",0)");
  CHECK(b.size() == 1 && b[0] == kStatInt);
  // PICKPOCKET is not an ability
  CHECK(CheckedAbilities("CheckStatGT(Protagonist,9,PICKPOCKET)\nPartyGoldGT(0)").empty());
  // equality check, odd spacing and case
  const auto c = CheckedAbilities("checkstat( Protagonist , 18 , cHr )");
  CHECK(c.size() == 1 && c[0] == kStatChr);
  // two abilities, each once
  const auto d = CheckedAbilities("CheckStatGT(Protagonist,12,CHR) CheckStatGT(Protagonist,12,INT) CheckStatGT(Protagonist,15,CHR)");
  CHECK(d.size() == 2 && d[0] == kStatChr && d[1] == kStatInt);
}

TEST_CASE("dlgroll needed value: the smallest passing value above the current one") {
  CHECK(NeededValue(9, [](int v) { return v > 12; }) == 13);
  CHECK(NeededValue(13, [](int v) { return v > 12; }) == 14);  // only called for replies that fail now
  CHECK(NeededValue(9, [](int v) { return v == 18; }) == 18);  // CheckStat(…,18,…)
  CHECK(NeededValue(9, [](int) { return false; }) == -1);      // not an ability gate (level, item...)
  CHECK(NeededValue(25, [](int) { return true; }) == -1);
}

TEST_CASE("dlgroll obscure: letters and digits become dots, shape and colour codes stay") {
  CHECK(Obscure("Grab her hand.", ".") == ".... ... .....");
  CHECK(Obscure("\"I'm 12!\"", "*") == "\"*'* **!\"");
  CHECK(Obscure("^0xff212effHi^-", "x") == "^0xff212effxx^-");
  CHECK(Obscure("caf\xC3\xA9", "x") == "xxxx");
  CHECK(Obscure("ab") == "\xC2\xB7\xC2\xB7");
}

TEST_CASE("dlgroll timeline: cues in order, banner after the pop, continue after the banner") {
  CHECK(RollTiming::kLand < RollTiming::kNatural);
  CHECK(RollTiming::kNatural < RollTiming::kBonusLit);
  CHECK(RollTiming::kBonusLit < RollTiming::kBonusFly);
  CHECK(RollTiming::kBonusFly < RollTiming::kTotal);
  CHECK(RollTiming::kTotal < RollTiming::kBonusFade);
  CHECK(RollTiming::Pop(true) == 5250 && RollTiming::Pop(false) == 5100);
  CHECK(RollTiming::Banner(true) == 6250 && RollTiming::Banner(false) == 7000);
  CHECK(RollTiming::Continue(true) == 6650);
}

TEST_CASE("dlgroll die frames: 24 tumble frames, then 50 numbers per scale") {
  CHECK(RestFrame(-9, 0) == 24);
  CHECK(RestFrame(20, 0) == 24 + 29);
  CHECK(RestFrame(40, 2) == 24 + 100 + 49);
  CHECK(RestFrame(99, 5) == RestFrame(40, 2));
  CHECK(RestFrame(-20, -1) == 24);
}

TEST_CASE("dlgroll menu insert: inside the named menu, not into a Lua chunk after it, braces in strings skipped") {
  // UI.MENU has CRLF line ends; WORLD_DIALOG is followed by a Lua chunk with its own braces
  const std::string ui =
      "menu\r\n{\r\n\tname 'WORLD_DIALOG_LEFT'\r\n}\r\nmenu\r\n{\r\n\tname 'WORLD_DIALOG'\r\n"
      "\tonOpen \"t = { 1 }\"\r\n\tlist\r\n\t{\r\n\t}\r\n}\r\n`\r\nfunction f() return {} end\r\n`\r\nmenu\r\n{\r\n}\r\n";
  const std::string out = InsertMenuItem(ui, "WORLD_DIALOG", "ITEM");
  const size_t item = out.find("ITEM");
  CHECK(item != std::string::npos);
  CHECK(item > out.find("name 'WORLD_DIALOG'\r"));
  CHECK(item < out.find('`'));
  CHECK(out.substr(item, 7) == "ITEM}\r\n");
  CHECK(out.find("ITEM") == out.rfind("ITEM"));
  CHECK(InsertMenuItem(ui, "NOPE", "ITEM") == ui);
}
