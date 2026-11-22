// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstring>

#include "core/ini.h"
#include "core/patch_math.h"
#include "engine/symbols.h"
#include "mini_test.h"
#include "modules/smooth/smooth_logic.h"
#include "modules/tbc/camera_logic.h"
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

TEST_CASE("tick pacer: accumulated real time spends one tick per world update") {
  // 16.67 ms iterations (60 Hz) with a 33.33 ms tick (fps 30): a tick is taken whenever the accumulated
  // time reaches tick_ms, so the long-run rate is the engine's own whatever the draw rate is.
  smooth::TickPacer c;
  const double tick = 1000.0 / 30.0, frame = 1000.0 / 60.0;
  CHECK(!c.Due(0.0, tick));  // first call: nothing accumulated yet
  int ticks = 0;
  for (int i = 1; i <= 601; ++i) ticks += c.Due(i * frame, tick) ? 1 : 0;
  CHECK(ticks == 300);  // 10 s (+ half a tick) of 60 Hz frames = 300 ticks at fps 30
  // Alpha is the leftover as a fraction of a tick.
  smooth::TickPacer a;
  CHECK(!a.Due(0.0, 25.0));
  CHECK(!a.Due(10.0, 25.0));
  CHECK(a.Alpha(25.0) == 0.4f);
  CHECK(a.Due(30.0, 25.0));  // 30 ms: one tick spent, 5 ms left
  CHECK(a.Alpha(25.0) == 0.2f);
  // A long stall is capped: at most four catch-up ticks, never a burst from a loading screen.
  smooth::TickPacer p;
  CHECK(!p.Due(1000.0, 25.0));
  CHECK(p.Due(1500.0, 25.0));  // 500 ms accumulated, capped to 100 ms
  CHECK(p.Due(1500.0, 25.0));
  CHECK(p.Due(1500.0, 25.0));
  CHECK(p.Due(1500.0, 25.0));  // budget spent down to 0
  CHECK(!p.Due(1500.0, 25.0));
  CHECK(p.Alpha(25.0) == 0.0f);
}

TEST_CASE("track: a walker stepping every other tick slides evenly over both ticks") {
  // The walk from the smooth_lock.lua trace (fps 30): x 2079.25 -> 2083.00 on tick 2, nothing on tick
  // 3, -> 2086.75 on tick 4. Shown over the 2-tick cadence, not over one tick and then a hold.
  smooth::Track t;
  t.Observe(7, 2075.5f, 100.0f, 1, 64.0f);  // first sight: shown where it is
  float x = 0, y = 0;
  t.At(1, 0.5f, &x, &y);
  CHECK(x == 2075.5f && y == 100.0f);
  t.Observe(7, 2079.25f, 100.0f, 2, 64.0f);  // a step after a 1-tick gap: span 1
  CHECK(t.span == 1);
  t.Observe(7, 2079.25f, 100.0f, 3, 64.0f);  // no step
  t.Observe(7, 2083.0f, 100.0f, 4, 64.0f);   // a step 2 ticks later: span 2
  CHECK(t.span == 2);
  CHECK(t.ax == 2079.25f && t.bx == 2083.0f);
  t.At(4, 0.0f, &x, &y);
  CHECK(x == 2079.25f);
  t.At(4, 0.5f, &x, &y);
  CHECK(x == 2080.1875f);  // a quarter of the way: 0.5 of 2 ticks
  t.Observe(7, 2083.0f, 100.0f, 5, 64.0f);  // the in-between tick: the slide goes on
  t.At(5, 0.0f, &x, &y);
  CHECK(x == 2081.125f);  // halfway, no hold
  t.At(5, 0.5f, &x, &y);
  CHECK(x == 2082.0625f);
  t.Observe(7, 2086.75f, 100.0f, 6, 64.0f);  // next step arrives exactly when the slide ends
  t.At(6, 0.0f, &x, &y);
  CHECK(x == 2083.0f);
  CHECK(t.ax == 2083.0f);
  // At rest it is shown exactly where the engine has it (the engine's int is the floor).
  t.At(9, 0.0f, &x, &y);
  CHECK(x == 2086.75f);
  CHECK(smooth::Pixel(x) == 2086);
  CHECK(smooth::Pixel(-0.25f) == -1);
}

TEST_CASE("track: early steps never jump, starts keep the cadence, jumps and gaps reset") {
  smooth::Track t;
  t.Observe(7, 0.0f, 0.0f, 1, 64.0f);
  t.Observe(7, 0.0f, 0.0f, 2, 64.0f);
  t.Observe(7, 4.0f, 0.0f, 3, 64.0f);  // span 2 (2 ticks since tb = 1)
  CHECK(t.span == 2);
  t.Observe(7, 8.0f, 2.0f, 4, 64.0f);  // a step one tick later: the slide restarts where it was shown
  CHECK(t.ax == 2.0f && t.ay == 0.0f);
  CHECK(t.span == 1);
  // Standing still for longer than kMaxSpan, then a step: the previous cadence is kept.
  for (uint32_t s = 5; s <= 12; ++s) t.Observe(7, 8.0f, 2.0f, s, 64.0f);
  t.Observe(7, 12.0f, 2.0f, 13, 64.0f);
  CHECK(t.span == 1);
  CHECK(t.ax == 8.0f);
  // A jump past snap (teleport, area change) shows at its real position.
  t.Observe(7, 100.0f, 2.0f, 14, 64.0f);
  float x = 0, y = 0;
  t.At(14, 0.0f, &x, &y);
  CHECK(x == 100.0f);
  // A missed tick (the object was not there) or another object in the same slot resets.
  t.Observe(7, 104.0f, 2.0f, 16, 64.0f);
  CHECK(t.ax == 104.0f && t.bx == 104.0f);
  CHECK(t.Current(7, 16) && !t.Current(7, 17) && !t.Current(8, 16));
  t.Observe(8, 50.0f, 2.0f, 17, 64.0f);
  CHECK(t.id == 8 && t.ax == 50.0f);
  smooth::Track fresh;
  CHECK(!fresh.Current(-1, 0));
}

TEST_CASE("camera: eases in float by real time, then locks on the shown position") {
  tbc::CameraEase c;
  c.Reset(0, 0);
  c.Step(100, 0, 0.0, 60.0, 6.0);  // no real time passed: the camera stays where it is
  CHECK(c.X() == 0 && c.Y() == 0);
  // One time constant (60 ms) closes 63 % of the gap; two 30 ms steps land on the same point, so the
  // camera looks the same at any frame rate.
  tbc::CameraEase a, b;
  a.Reset(0, 0);
  b.Reset(0, 0);
  a.Step(100, 50, 30.0, 60.0, 6.0);
  a.Step(100, 50, 30.0, 60.0, 6.0);
  b.Step(100, 50, 60.0, 60.0, 6.0);
  CHECK(a.X() == 63 && a.Y() == 32);
  CHECK(std::fabs(a.x - b.x) < 1e-9 && std::fabs(a.y - b.y) < 1e-9);
  // Short steps are not lost to truncation: 1.3 px * 0.2 still moves the camera.
  tbc::CameraEase s;
  s.Reset(0, 0);
  s.x = 98.7;
  s.Step(100, 0, 13.4, 60.0, 0.5);
  CHECK(s.x > 98.7 && !s.locked);
  // Within lock_px it locks, and from then on it IS the target, frame for frame.
  tbc::CameraEase l;
  l.Reset(95, 0);
  l.Step(100, 0, 16.0, 60.0, 6.0);
  CHECK(l.locked && l.X() == 100);
  l.Step(102, 1, 16.0, 60.0, 6.0);
  CHECK(l.X() == 102 && l.Y() == 1);
  // Half an area away is shown at once.
  CHECK(l.Distant(703, 1, 600));
  CHECK(!l.Distant(702, 1, 600));
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
