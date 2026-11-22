// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstring>

#include "core/ini.h"
#include "core/patch_math.h"
#include "engine/symbols.h"
#include "mini_test.h"
#include "modules/smooth/smooth_logic.h"
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

TEST_CASE("ini integers: decimal, hex, negative, garbage falls back") {
  auto ini = ParseIni("[tbc]\nend_turn_key=0x23\nframes=60\nneg=-5\nbad=abc\nempty=\n");
  CHECK(IniInt(ini, "tbc.end_turn_key", 0) == 0x23);
  CHECK(IniInt(ini, "tbc.frames", 30) == 60);
  CHECK(IniInt(ini, "tbc.neg", 1) == -5);
  CHECK(IniInt(ini, "tbc.bad", 40) == 40);
  CHECK(IniInt(ini, "tbc.empty", 40) == 40);
  CHECK(IniInt(ini, "tbc.missing", 60) == 60);
}

TEST_CASE("tick clock: alpha is the fraction of the tick, clamped, 0 before the tick") {
  smooth::TickClock c;
  c.enabled = true;
  c.BeginTick(1000, 33);
  CHECK(c.serial == 1);
  CHECK(c.Alpha(1000) == 0.0f);
  CHECK(c.Alpha(999) == 0.0f);
  CHECK(c.Alpha(1016) == 16.0f / 33.0f);
  CHECK(c.Alpha(1100) == 1.0f);
  c.BeginTick(2000, 33);
  CHECK(c.serial == 2);
  CHECK(c.Alpha(2000) == 0.0f);
  CHECK(!c.enabled || c.Alpha(0) == 0.0f);
  smooth::TickClock off;
  CHECK(off.Alpha(12345) == 0.0f);  // disabled, or never captured
}

TEST_CASE("tick clock serial skips 0, which means \"never captured\"") {
  smooth::TickClock c;
  c.serial = 0xFFFFFFFFu;
  c.enabled = true;
  c.BeginTick(5000, 33);
  CHECK(c.serial == 1);
}

TEST_CASE("tick budget: accumulated real time spends one tick per world update") {
  // 12 ms iterations with a 25 ms tick: a tick is taken whenever the accumulated time reaches tick_ms,
  // so the long-run rate is the engine's own even though each short wait overshoots a little.
  smooth::TickBudget c;
  CHECK(!c.Due(0, 25));     // first call: nothing accumulated yet
  CHECK(!c.Due(12, 25));    // 12 ms
  CHECK(!c.Due(24, 25));    // 24 ms
  CHECK(c.Due(36, 25));     // 36 ms -> one tick spent, 11 ms left
  CHECK(!c.Due(48, 25));    // 23 ms
  CHECK(c.Due(60, 25));     // 35 ms -> spent, 10 left
  CHECK(!c.Due(72, 25));    // 22 ms
  CHECK(c.Due(84, 25));     // 34 ms -> spent, 9 left
  CHECK(!c.Due(96, 25));    // 21 ms
  // A long stall is capped: at most four catch-up ticks, never a burst from a loading screen.
  smooth::TickBudget p;
  CHECK(!p.Due(1000, 25));
  CHECK(p.Due(1500, 25));   // 500 ms accumulated, capped to 100 ms
  CHECK(p.Due(1500, 25));
  CHECK(p.Due(1500, 25));
  CHECK(p.Due(1500, 25));   // budget spent down to 0
  CHECK(!p.Due(1500, 25));
}

TEST_CASE("interpolated offset: prev + delta*alpha, relative to the drawn position") {
  smooth::DrawPos p;
  p.Capture(100.0f, 50.0f, 1);
  // Mid-tick: half of a 4 px step is shown, i.e. one tick behind the engine's position.
  CHECK(smooth::InterpOffset(p, 104.0f, 50.0f, 104, 50, 1, 0.5f, 64.0f).dx == -2);
  CHECK(smooth::InterpOffset(p, 104.0f, 50.0f, 104, 50, 1, 1.0f, 64.0f).dx == 0);
  CHECK(smooth::InterpOffset(p, 104.0f, 50.0f, 104, 50, 1, 0.0f, 64.0f).dx == -4);
  // Sub-pixel motion the tick cannot spend: the drawn int is unchanged, the exact position moved.
  CHECK(smooth::InterpOffset(p, 100.0f, 50.0f, 100, 50, 1, 1.0f, 64.0f).dx == 0);
  smooth::DrawPos q;
  q.Capture(100.4f, 50.0f, 1);
  CHECK(smooth::InterpOffset(q, 100.9f, 50.0f, 100, 50, 1, 1.0f, 64.0f).dx == 1);
  CHECK(smooth::InterpOffset(q, 100.4f, 50.0f, 100, 50, 1, 1.0f, 64.0f).dx == 0);
  // Negative coordinates round away from zero.
  smooth::DrawPos n;
  n.Capture(-100.5f, -20.0f, 1);
  CHECK(smooth::InterpOffset(n, -100.5f, -20.0f, -101, -20, 1, 1.0f, 64.0f).dx == 0);
  CHECK(smooth::Round(-0.5f) == -1);
  CHECK(smooth::Round(0.5f) == 1);
}

TEST_CASE("interpolated offset: nothing to do stays at the engine's position") {
  smooth::DrawPos p;
  p.Capture(100.0f, 50.0f, 7);
  CHECK(smooth::InterpOffset(p, 104.0f, 50.0f, 104, 50, 8, 0.5f, 64.0f).dx == 0);  // new tick
  CHECK(smooth::InterpOffset(p, 104.0f, 50.0f, 104, 50, 0, 0.5f, 64.0f).dx == 0);  // serial 0 = never
  smooth::DrawPos fresh;  // captured never
  CHECK(smooth::InterpOffset(fresh, 104.0f, 50.0f, 104, 50, 1, 0.5f, 64.0f).dx == 0);
  // A jump past snap (teleport, area change) draws at its real position; exactly snap still slides.
  CHECK(smooth::InterpOffset(p, 165.0f, 50.0f, 165, 50, 7, 0.5f, 64.0f).dx == 0);
  CHECK(smooth::InterpOffset(p, 165.0f, 50.0f, 165, 50, 7, 0.5f, 64.0f).dy == 0);
  CHECK(smooth::InterpOffset(p, 164.0f, 50.0f, 164, 50, 7, 0.5f, 64.0f).dx == -32);
  CHECK(smooth::InterpOffset(p, 163.9f, 50.0f, 163, 50, 7, 0.5f, 64.0f).dx == -31);
  CHECK(smooth::InterpOffset(p, 100.0f, 314.0f, 100, 314, 7, 0.5f, 64.0f).dy == 0);
  CHECK(smooth::InterpOffset(p, 100.0f, 250.0f, 100, 250, 7, 0.5f, 256.0f).dy == -100);
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
