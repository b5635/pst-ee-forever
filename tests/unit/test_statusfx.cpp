// SPDX-License-Identifier: GPL-2.0-or-later
// statusfx: which state plays which effect, the effects' timing (ported NWN emitter numbers), where they
// draw, and which engine colour glows end.
#include <cmath>
#include <string>
#include <vector>

#include "mini_test.h"
#include "modules/statusfx/statusfx_logic.h"

using namespace pstf::statusfx;

namespace {
bool Near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) <= eps; }
void Run(System& s, float seconds, float dt = 1.0f / 30) {
  for (float t = 0; t < seconds - 1e-4f; t += dt) s.Update(dt);
}
int CountCell(const System& s, int lo, int hi) {
  int n = 0;
  for (const Particle& q : s.p) n += q.cell >= lo && q.cell < hi;
  return n;
}
EffectInfo Eff(uint64_t key, int op, uint32_t type, const char* parent, const char* res = "", bool equipped = false) {
  EffectInfo e;
  e.key = key;
  e.opcode = op;
  e.type = type;
  e.parent = parent;
  e.res = res;
  e.equipped = equipped;
  return e;
}
bool Has(const std::vector<uint64_t>& v, uint64_t k) {
  for (uint64_t x : v)
    if (x == k) return true;
  return false;
}
}  // namespace

TEST_CASE("statusfx: effect names round trip") {
  for (int i = 0; i < static_cast<int>(Fx::Count); ++i) {
    Fx f;
    CHECK(FxFromName(FxName(static_cast<Fx>(i)), &f));
    CHECK(f == static_cast<Fx>(i));
  }
  Fx f;
  CHECK(FxFromName("MANTLE", &f) && f == Fx::Mantle);
  CHECK(!FxFromName("pulse", &f));
  CHECK(IsLoop(Fx::Fear) && IsLoop(Fx::StunLoop) && !IsLoop(Fx::Stun) && !IsLoop(Fx::Mantle));
}

TEST_CASE("statusfx: states start, hold and stop their effects") {
  System s;
  Watcher w;
  w.Step(7, kStatePanic, 0.0, &s);
  CHECK(s.Active(Fx::Fear, 7));
  w.Step(7, kStatePanic, 1.0, &s);
  CHECK(s.inst.size() == 1);  // a held loop is not started twice
  w.Step(7, 0, 2.0, &s);
  CHECK(!s.Active(Fx::Fear, 7));
  w.Step(8, kStateStunned, 0.0, &s);
  CHECK(s.Active(Fx::Stun, 8) && s.Active(Fx::StunLoop, 8));
  w.Step(8, kStateStunned | kStateDead, 0.5, &s);  // a dead creature shows nothing
  CHECK(!s.Active(Fx::StunLoop, 8));
}

TEST_CASE("statusfx: impacts without a loop repeat while the state lasts") {
  System s;
  Watcher w;
  w.repeat = 6;
  w.Step(3, kStateBlind | kStatePoisoned | kStateSilenced, 10.0, &s);
  CHECK(s.inst.size() == 3);
  s.Update(2.0f);  // impacts end by themselves
  CHECK(s.inst.empty());
  w.Step(3, kStateBlind | kStatePoisoned | kStateSilenced, 15.9, &s);
  CHECK(s.inst.empty());
  w.Step(3, kStateBlind | kStatePoisoned | kStateSilenced, 16.0, &s);
  CHECK(s.inst.size() == 3);
  CHECK(s.Active(Fx::Blind, 3) && s.Active(Fx::Acid, 3) && s.Active(Fx::Sonic, 3));
  Watcher once;
  once.repeat = 0;
  System s2;
  once.Step(3, kStateBlind, 0.0, &s2);
  s2.Update(2.0f);
  once.Step(3, kStateBlind, 100.0, &s2);
  CHECK(s2.inst.empty());
}

TEST_CASE("statusfx: ini toggles keep a state's effect off") {
  System s;
  Watcher w;
  w.on.fear = false;
  w.on.poison = false;
  w.Step(1, kStatePanic | kStatePoisoned, 0.0, &s);
  CHECK(s.inst.empty());
}

TEST_CASE("statusfx: stun impact = stars round the head and a lightning burst, gone in 2 s") {
  System s;
  s.Start(Fx::Stun, 5);
  Run(s, 0.5f);
  const int bolts = CountCell(s, kBolt, kBolt + 4), stars = CountCell(s, kGlint, kGlint + 4);
  CHECK(bolts > 20 && bolts < 50);  // 120/s living 0.3 s: ~36 at any moment
  CHECK(stars >= 4);
  std::vector<Quad> q;
  Anchor a;
  a.x = 500, a.y = 400, a.head_px = 70;
  s.Draw(5, a, &q);
  CHECK(!q.empty());
  const float ppm = PxPerMetre(70);
  CHECK(Near(ppm, 70 / (1.9 * kCosE), 1e-3));
  for (const Quad& d : q) {
    if (d.cell >= kBolt) continue;  // stars: on the 0.5 m circle at the head
    const float head = (kHeadM + 0.05f) * kCosE * ppm;
    CHECK(d.y >= a.y - head - 0.5f * kSinE * ppm - 0.5f && d.y <= a.y - head + 0.5f * kSinE * ppm + 0.5f);
    CHECK(std::fabs(d.x - a.x) <= 0.5f * ppm + 0.5f);
  }
  Run(s, 1.6f);
  CHECK(s.inst.empty());
  CHECK(s.p.empty());
}

TEST_CASE("statusfx: acid bursts once at 0.6 s: one green glint and five smoke puffs") {
  System s;
  s.Start(Fx::Acid, 2);
  Run(s, 0.55f);  // 17 frames: 0.567 s
  CHECK(CountCell(s, kBurst, kBurst + 1) == 0);
  Run(s, 0.06f);
  CHECK(CountCell(s, kBurst, kBurst + 1) == 1);
  CHECK(CountCell(s, kSmoke, kSmoke + 8) == 5);
  for (const Particle& q : s.p)
    if (q.cell == kBurst) CHECK(Near(q.r0, 0.05) && Near(q.g0, 1.0) && Near(q.b0, 0.0));
  Run(s, 0.3f);
  CHECK(CountCell(s, kBurst, kBurst + 1) == 0);
  System w;
  w.Start(Fx::Sonic, 2);
  Run(w, 0.65f);
  for (const Particle& q : w.p)
    if (q.cell == kBurst) CHECK(Near(q.r0, 1) && Near(q.g0, 1) && Near(q.b0, 1));
}

TEST_CASE("statusfx: spell mantle = 48 panes round the body, gone by 0.86 s") {
  System s;
  s.Start(Fx::Mantle, 9);
  s.Update(1.0f / 30);
  CHECK(CountCell(s, kShard, kShard + 1) == 48);
  CHECK(CountCell(s, kGlow, kGlow + 1) == 1);
  Run(s, 0.4f);
  std::vector<Quad> q;
  Anchor a;
  a.head_px = 70;
  s.Draw(9, a, &q);
  int panes = 0;
  for (const Quad& d : q) panes += d.cell == kShard;
  CHECK(panes == 48);  // all showing at 0.43 s
  Run(s, 0.45f);
  CHECK(CountCell(s, kShard, kShard + 1) == 0);
}

TEST_CASE("statusfx: fear throws burning skulls while it lasts; skulls draw over their fire") {
  System s;
  s.Start(Fx::Fear, 4);
  Run(s, 1.5f);
  const int skulls = CountCell(s, kSkull, kSkull + 2), fire = CountCell(s, kFire, kFire + 8);
  CHECK(skulls >= 3 && skulls <= 7);  // 4/s after a 0.3 s ramp, living 2 s
  CHECK(fire > skulls);
  std::vector<Quad> q;
  Anchor a;
  s.Draw(4, a, &q);
  bool normal_seen = false, order_ok = true;
  for (const Quad& d : q) {
    if (!d.additive) normal_seen = true;
    else if (normal_seen) order_ok = false;
  }
  CHECK(normal_seen && order_ok);
  s.Stop(Fx::Fear, 4);
  Run(s, 3.5f);
  CHECK(s.p.empty());
  s.Start(Fx::Fear, 4);
  s.Forget(4);
  CHECK(s.inst.empty());
}

TEST_CASE("statusfx: glows of other spells end after 2 s, those of a replaced state at once") {
  GlowCutter c;
  c.cut_after = 2;
  std::vector<EffectInfo> effs = {
      Eff(1, 9, 4096, "SPWI402"),           // confusion's pulse: stays 2 s
      Eff(2, 9, 4096, "FDAG"), Eff(3, 25, 4096, "FDAG"),  // a poison weapon's green pulse: replaced
      Eff(4, 9, 4096, "SWORD", "", true),   // an equipped item's own glow: never
      Eff(5, 9, 6, "SPWI402"),              // still pending: not tracked
      Eff(6, 369, 6, "SPWI115", "S115BLND"), Eff(7, 74, 4096, "SPWI115"),  // blinding overlay: blanked
      Eff(8, 369, 6, "SPPR101", "SP11BLES"),  // Blessing's sparkle: left alone
      Eff(9, 369, 6, "SPWI120", kBlankBam), Eff(10, 74, 4096, "SPWI120")};
  CutPlan p = c.Plan(1, effs, 100.0);
  CHECK(Has(p.expire, 2) && !Has(p.expire, 1) && !Has(p.expire, 4) && !Has(p.expire, 5));
  CHECK(p.blank.size() == 1 && Has(p.blank, 6));
  p = c.Plan(1, effs, 101.9);
  CHECK(!Has(p.expire, 1));
  p = c.Plan(1, effs, 102.0);
  CHECK(Has(p.expire, 1));
  CHECK(c.Tracked() == 2);  // 1 and 2 (running glows only)
  effs.erase(effs.begin());
  c.Plan(1, effs, 103.0);
  CHECK(c.Tracked() == 1);  // gone from the creature: forgotten
  c.Forget(1);
  CHECK(c.Tracked() == 0);
}

TEST_CASE("statusfx: glow_seconds 0 leaves other glows alone; a state toggled off keeps its glow") {
  GlowCutter c;
  c.cut_after = 0;
  c.on.poison = false;
  const std::vector<EffectInfo> effs = {Eff(1, 9, 4096, "SPWI402"), Eff(2, 9, 4096, "FDAG"), Eff(3, 25, 4096, "FDAG")};
  const CutPlan p = c.Plan(1, effs, 0.0);
  CHECK(p.expire.empty());
  const CutPlan q = c.Plan(1, effs, 50.0);
  CHECK(q.expire.empty());
}

TEST_CASE("statusfx: px per metre follows the creature's height, clamped") {
  CHECK(Near(PxPerMetre(0), PxPerMetre(70)));
  CHECK(Near(PxPerMetre(5), 18.0));
  CHECK(Near(PxPerMetre(1000), 80.0));
}
