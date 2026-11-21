// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstring>

#include "core/ini.h"
#include "core/patch_math.h"
#include "engine/symbols.h"
#include "mini_test.h"
#include "modules/voice/voice_logic.h"

using namespace pstf;

TEST_CASE("rel32 target of the examine Fetch call is CTlkTable::Fetch") {
  // win 1402196FC: E8 3F 71 28 00 -> 1404A0840
  auto t = Rel32Target(sym::kImageBase + sym::kRvaExamineFetchCall, sym::kExamineFetchCallBytes);
  CHECK(t && *t == sym::kImageBase + sym::kRvaTlkFetch);
}

TEST_CASE("rel32 encode round-trips and rejects out of range") {
  const uint64_t site = 0x1402196FC;
  auto rel = Rel32For(site, 0x1404A0840);
  CHECK(rel && *rel == 0x0028713F);
  CHECK(!Rel32For(site, site + 0x100000000ull));
  uint8_t insn[5] = {0xE8};
  const int32_t r = *Rel32For(site, 0x13FFF0000ull);
  memcpy(insn + 1, &r, 4);
  CHECK(*Rel32Target(site, insn) == 0x13FFF0000ull);
}

TEST_CASE("absolute jump encoding") {
  uint8_t j[kAbsJmpLen];
  WriteAbsJmp(j, 0x1122334455667788ull);
  const uint8_t want[kAbsJmpLen] = {0xFF, 0x25, 0, 0, 0, 0, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
  CHECK(BytesEqual(j, want, kAbsJmpLen));
}

TEST_CASE("manifest entries have bytes and the entry patch fits") {
  for (const auto& s : sym::kAll) CHECK(s.len > 0 && s.rva > 0x1000);
  CHECK(sizeof sym::kEntryBytes == kAbsJmpLen);
}

TEST_CASE("ini parsing: sections, case, comments, BOM, bools") {
  auto ini = ParseIni("\xEF\xBB\xBF; c\n[Voice]\r\nExamine = 1 ; on\n[x]\nflag=off\nbad\n");
  CHECK(ini.at("voice.examine") == "1");
  CHECK(IniBool(ini, "voice.examine", false));
  CHECK(!IniBool(ini, "x.flag", true));
  CHECK(IniBool(ini, "x.missing", true));
  CHECK(!IniBool(ParseIni(""), "voice.examine", false));  // no ini = vanilla
}

TEST_CASE("replay guard: same line while playing is skipped, others play") {
  voice::ReplayGuard g;
  CHECK(voice::ShouldPlay(g, 828, 1000, 4000));    // first click
  CHECK(!voice::ShouldPlay(g, 828, 3000, 4000));   // double click mid-line
  CHECK(voice::ShouldPlay(g, 856, 3500, 2000));    // a different info point
  CHECK(voice::ShouldPlay(g, 828, 3600, 4000));    // 828 again: last line changed
  CHECK(!voice::ShouldPlay(g, 828, 7599, 4000));
  CHECK(voice::ShouldPlay(g, 828, 7600, 4000));    // finished
}
