// SPDX-License-Identifier: GPL-2.0-or-later
// The options tool's ini document: schema from the shipped ini, merge, Baldur.lua lines.
#include <fstream>
#include <sstream>

#include "core/ini_doc.h"
#include "mini_test.h"

using namespace pstf;

namespace {
const char* kTmpl =
    "\xEF\xBB\xBF; header\n"
    "\n"
    "[Voice]\n"
    "; Play examine lines.\n"
    "examine=1\n"
    "[tbc]\n"
    "; Radii in engine units:\n"
    "; start and join.\n"
    "start_radius=450\n"
    "join_radius=650 ; inline\n"
    "end_turn_key=0x23\n"
    "[lighting]\n"
    "; Debug view.\n"
    "debug=0\n"
    "[dev]\n"
    "; Lua file.\n"
    "autorun=\n";
}  // namespace

TEST_CASE("ini doc: keys in order, help from the comment above or the section's last, kinds") {
  auto e = ParseIniEntries(kTmpl);
  CHECK(e.size() == 6);
  CHECK(e[0].Id() == "voice.examine" && e[0].value == "1" && e[0].help == "Play examine lines.");
  CHECK(e[0].kind == IniKind::Bool);
  CHECK(e[1].help == "Radii in engine units: start and join." && e[1].kind == IniKind::Int);
  CHECK(e[2].value == "650" && e[2].help == e[1].help);  // inherits; inline comment cut
  CHECK(e[3].kind == IniKind::Int);                      // 0x23
  CHECK(e[4].Id() == "lighting.debug" && e[4].kind == IniKind::Int);  // 0 but not a switch
  CHECK(e[5].kind == IniKind::Text && e[5].value.empty());
}

TEST_CASE("ini doc: value checks per kind") {
  CHECK(IniValueValid(IniKind::Bool, "0") && IniValueValid(IniKind::Bool, "1"));
  CHECK(!IniValueValid(IniKind::Bool, "2"));
  CHECK(IniValueValid(IniKind::Int, "0x23") && IniValueValid(IniKind::Int, "-5"));
  CHECK(!IniValueValid(IniKind::Int, "12a") && !IniValueValid(IniKind::Int, ""));
  CHECK(IniValueValid(IniKind::Text, "a.lua") && !IniValueValid(IniKind::Text, "a;b"));
}

TEST_CASE("ini doc: merge keeps the template layout, takes values, keeps unknown keys") {
  IniMap user = {{"voice.examine", "0"}, {"tbc.join_radius", "700"}, {"old.thing", "5"}};
  std::string m = MergeIni(kTmpl, user);
  CHECK(m.rfind("\xEF\xBB\xBF; header\n", 0) == 0);
  CHECK(m.find("examine=0\n") != std::string::npos);
  CHECK(m.find("join_radius=700 ; inline\n") != std::string::npos);
  CHECK(m.find("start_radius=450\n") != std::string::npos);  // not in the user's ini: default
  CHECK(m.find("[old]\n; kept from the previous pst_forever.ini (not in this build)\nthing=5\n") !=
        std::string::npos);
  auto back = ParseIni(m);
  CHECK(back["voice.examine"] == "0" && back["tbc.end_turn_key"] == "0x23" && back["old.thing"] == "5");
  // Merging the template with its own values changes nothing.
  CHECK(MergeIni(kTmpl, ParseIni(kTmpl)) == kTmpl);
}

TEST_CASE("ini doc: the shipped ini is a valid schema (every default valid, every key has help)") {
  std::ifstream f(PSTF_SOURCE_DIR "/data/pst_forever.ini", std::ios::binary);
  CHECK(static_cast<bool>(f));
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string text = ss.str();
  auto entries = ParseIniEntries(text);
  CHECK(entries.size() > 100);
  for (const auto& e : entries) {
    if (e.help.empty() || !IniValueValid(e.kind, e.value)) std::printf("  bad entry %s\n", e.Id().c_str());
    CHECK(!e.help.empty());
    CHECK(IniValueValid(e.kind, e.value));
  }
  CHECK(MergeIni(text, ParseIni(text)) == text);
  CHECK(SectionTitle("tbc") == "Turn-based combat" && SectionTitle("newmod") == "Newmod");
}

TEST_CASE("baldur.lua: get the last line's value, set replaces or appends") {
  const std::string lua =
      "SetPrivateProfileString('Window','Maximized','0')\r\n"
      "SetPrivateProfileString('Program Options','Disable Cosmetic Attacks','0')\r\n";
  CHECK(LuaProfileGet(lua, "Program Options", "Disable Cosmetic Attacks", "x") == "0");
  CHECK(LuaProfileGet(lua, "program options", "disable cosmetic attacks", "x") == "0");
  CHECK(LuaProfileGet(lua, "Program Options", "Missing", "x") == "x");
  std::string s = LuaProfileSet(lua, "Program Options", "Disable Cosmetic Attacks", "1");
  CHECK(s ==
        "SetPrivateProfileString('Window','Maximized','0')\r\n"
        "SetPrivateProfileString('Program Options','Disable Cosmetic Attacks','1')\r\n");
  std::string a = LuaProfileSet("SetPrivateProfileString('Window','Maximized','0')", "Program Options",
                                "Disable Cosmetic Attacks", "1");
  CHECK(a ==
        "SetPrivateProfileString('Window','Maximized','0')\n"
        "SetPrivateProfileString('Program Options','Disable Cosmetic Attacks','1')\n");
  CHECK(LuaProfileGet(a, "Program Options", "Disable Cosmetic Attacks", "0") == "1");
}
