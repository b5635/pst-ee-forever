// SPDX-License-Identifier: GPL-2.0-or-later
// gore: materials (RESDATA armor + hitmat.2da) and the hit particles (GemRB ScenePolish port).
#include <cmath>
#include <string>

#include "mini_test.h"
#include "modules/gore/gore_logic.h"

using namespace pstf::gore;

namespace {
bool Near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }
const char* kHitmat =
    "2DA V1.0\nFLESH\n        MATERIAL\nARMOR0  FLESH\nARMOR4  BONE\nARMOR10 FIRE\nANIM58  ARMORED_BONE\n"
    "ANIM59  BONE\nANIM71  HOLLOW\nANIM98  HOLLOW\n";
const char* kArmor = "# c\n2\t0\t# Annah\n13\t1\n36\t10\n46\t4\n52\t7\n57\t9\n58\t2\n59\t0\n71\t2\n558\t2\n";
int Count(const Particles& ps, Kind k) {
  int n = 0;
  for (const Particle& q : ps.p) n += q.kind == k;
  return n;
}
}  // namespace

TEST_CASE("gore: materials from RESDATA armor and hitmat.2da rows") {
  MaterialTable t;
  CHECK(ParseHitmat(kHitmat, &t));
  ParseArmorList(kArmor, &t);
  CHECK(t.Of(2) == Material::Flesh);         // Annah bleeds
  CHECK(t.Of(13) == Material::Leather);      // Dak'kon
  CHECK(t.Of(36) == Material::Fire);         // Ignus: embers
  CHECK(t.Of(46) == Material::Bone);         // Morte: dust and chips
  CHECK(t.Of(52) == Material::Metal);        // Nordom: sparks
  CHECK(t.Of(57) == Material::Shadow);       // shadows smoke
  CHECK(t.Of(58) == Material::ArmoredBone);  // ANIM row beats the armor value (2 = armored)
  CHECK(t.Of(59) == Material::Bone);         // skeleton priest: armor 0 but bone
  CHECK(t.Of(71) == Material::Hollow);       // Vhailor only sparks
  CHECK(t.Of(558) == Material::Armored);     // EE id past 255: its own row, not Morte's (558 & 0xFF = 46)
  CHECK(t.Of(0x7F2E) == Material::Bone);     // unknown full id: low byte 0x2E = 46
  CHECK(t.Of(999) == Material::Flesh);
  CHECK(!ParseHitmat("not a table", &t));
  Material m;
  CHECK(MaterialFromName("armored_bone", &m) && m == Material::ArmoredBone && !MaterialFromName("GLASS", &m));
  CHECK(Bleeds(Material::Armored) && Sparks(Material::Armored) && !Bleeds(Material::Hollow) && Sparks(Material::Hollow));
  CHECK(!Bleeds(Material::Bone) && !Sparks(Material::Bone));
}

TEST_CASE("gore: blood types and directions") {
  CHECK(GushSeverity(3) == 1 && GushSeverity(4) == 2 && GushSeverity(5) == 3 && GushSeverity(6) == 3);
  CHECK(GushSeverity(7) == 0 && GushSeverity(9) == -1 && GushSeverity(10) == -1);
  CHECK(IsCriticalAnim(10) && IsCriticalAnim(17) && !IsCriticalAnim(9) && !IsCriticalAnim(18));
  CHECK(Near(std::sin(DirectionAngle(0)), 1.0) && Near(std::cos(DirectionAngle(4)), -1.0));  // south, west
  CHECK(Near(std::sin(DirectionAngle(8)), -1.0) && Near(std::cos(DirectionAngle(12)), 1.0));  // north, east
}

TEST_CASE("gore: hits by material, landing as stains, crits exaggerated") {
  HitEvent e;
  e.x = 100;
  e.y = 200;
  e.mat = Material::Flesh;
  e.severity = 2;
  Particles flesh;
  flesh.Hit(e);
  const int blood = Count(flesh, Kind::Blood);
  CHECK(blood == int(6 + 2 * 7) + 2 * 2);  // spray + drips
  CHECK(Count(flesh, Kind::Spark) == 0 && Count(flesh, Kind::Dust) == 3);
  for (int i = 0; i < 300; ++i) flesh.Simulate(1.0f / 60.0f);  // 5 s: every drop landed
  CHECK(Count(flesh, Kind::Blood) == 0 && Count(flesh, Kind::Stain) == blood);
  for (const Particle& q : flesh.p) CHECK(q.z == 0.0f && q.y > 150 && q.y < 260);  // stains on the floor near the feet

  Particles crit;
  e.crit = true;
  crit.Hit(e);
  CHECK(Count(crit, Kind::Blood) > blood * 2);  // as if severity 5

  Particles bone, hollow, armored, fire;
  e.crit = false;
  e.mat = Material::Bone;
  bone.Hit(e);
  CHECK(Count(bone, Kind::Blood) == 0 && Count(bone, Kind::Chip) == 8 && Count(bone, Kind::Dust) == 4);
  e.mat = Material::Hollow;
  hollow.Hit(e);
  CHECK(Count(hollow, Kind::Blood) == 0 && Count(hollow, Kind::Spark) == 11 && hollow.flashes.size() == 1);
  e.mat = Material::Armored;
  armored.Hit(e);
  CHECK(Count(armored, Kind::Blood) > 0 && Count(armored, Kind::Spark) == 11);
  e.mat = Material::Fire;
  fire.Hit(e);
  CHECK(Count(fire, Kind::Ember) == 16 && Count(fire, Kind::Blood) == 0);
  for (int i = 0; i < 40; ++i) bone.Simulate(1.0f / 20.0f);
  CHECK(Count(bone, Kind::Debris) > 0);  // chips bounce once, then lie on the floor

  Particles cap;  // decals are capped, oldest first
  cap.max_decals = 10;
  e.mat = Material::Flesh;
  for (int i = 0; i < 4; ++i) cap.Hit(e);
  for (int i = 0; i < 300; ++i) cap.Simulate(1.0f / 60.0f);
  CHECK(Count(cap, Kind::Stain) == 10);
}

TEST_CASE("gore: shapes (stains flat and drying, airborne drops lifted)") {
  Particle q;
  q.kind = Kind::Stain;
  q.x = 10;
  q.y = 20;
  q.size = 2;
  q.r = 0.5f;
  q.a = 0.8f;
  q.life = 30;
  q.age = 0;
  Shape s = ShapeOf(q);
  CHECK(s.disc && Near(s.rx, 3.0) && Near(s.ry, 1.5) && Near(s.a, 0.8) && Near(s.r, 0.5));
  q.age = 6;  // spread 1.25, dried 35 %
  s = ShapeOf(q);
  CHECK(Near(s.rx, 3.75) && Near(s.r, 0.325));
  q.age = 29.25f;  // last quarter: fading
  CHECK(Near(ShapeOf(q).a, 0.8 * (1 - 0.975) / 0.25, 1e-3));  // 0.08
  q.kind = Kind::Blood;
  q.z = 30;
  q.age = 0;
  q.life = 4;
  s = ShapeOf(q);
  CHECK(Near(s.y, -10.0) && s.disc && !s.additive);
  q.kind = Kind::Spark;
  CHECK(ShapeOf(q).additive && !ShapeOf(q).disc);
}
