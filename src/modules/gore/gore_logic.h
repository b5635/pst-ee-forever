// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Contributors to the GemRB project <https://gemrb.org> (ScenePolish materials and
// hit particles, gemrb/core/ScenePolish.cpp)
// Material hit effects instead of the engine's blood animations: what a creature is made of (RESDATA.INI
// "armor" = its hit sound set, refined per animation by hitmat.2da) decides blood, sparks, bone dust and
// chips, stone grit, spirit wisps, shadow smoke or embers. Blood and chips land and stay on the floor as
// stains / debris. Presentation only: the engine's damage, sounds and scripts are untouched. Pure logic.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pstf::gore {

enum class Material : uint8_t {
  Flesh,        // cloth, skin: blood
  Leather,      // blood
  Armored,      // chain / plate over a living body: blood and sparks
  ArmoredBone,  // armoured skeleton: sparks, bone dust and chips
  Hollow,       // empty animated armour (Vhailor): sparks only
  Stone,        // grit and stone chips
  Bone,         // bone dust and chips that stay on the floor
  Spirit,       // pale wisps
  Metal,        // constructs: sparks and metal bits
  Shadow,       // dark wisps
  Fire          // embers (Ignus)
};
const char* MaterialName(Material m);
// PST RESDATA.INI "armor" (the HIT_0<type><A+armor> sound set) -> material (GemRB MaterialForArmor).
Material MaterialForArmor(int armor);
bool MaterialFromName(const std::string& name, Material* out);  // FLESH, ARMORED_BONE, ... (any case)
inline bool Bleeds(Material m) { return m == Material::Flesh || m == Material::Leather || m == Material::Armored; }
inline bool Sparks(Material m) {
  return m == Material::Armored || m == Material::ArmoredBone || m == Material::Hollow || m == Material::Metal;
}

// hitmat.2da (ANIM<id> rows: single creatures; ARMOR<n> rows: whole sound sets) + the armor value of each
// animation (tools/hitmat.py: "anim armor" lines from RESDATA.INI). Exact id first, then its low byte.
struct MaterialTable {
  std::map<int, Material> anim_rows, armor_rows;
  std::map<int, int> armor_of_anim;
  Material Of(int anim_id) const;
};
bool ParseHitmat(const std::string& text, MaterialTable* out);      // 2DA V1.0; false if not a 2DA
void ParseArmorList(const std::string& text, MaterialTable* out);   // "anim armor", '#' comments

// CBlood types passed to CGameSprite::AddBlood (win 1403B83F0 / lin 50D8F0, values from StartSpriteEffect's
// switch, win 1403EC260): 3/4/5 gush low/medium/high, 6 pulsating artery, 7 exploding death, 8 whiff,
// 9 glancing sparks, 10..17 the PST critical-hit animations (ch01nuke, critanna, ...).
// Severity of a gush (1..3, 0 = gory death), -1 for anything else.
int GushSeverity(int blood_type);
inline bool IsCriticalAnim(int blood_type) { return blood_type >= 10 && blood_type <= 17; }

struct Rng {
  uint32_t s = 0x9E3779B9u;
  float Next();  // [0, 1)
  float Range(float lo, float hi) { return lo + (hi - lo) * Next(); }
};

enum class Kind : uint8_t { Spark, Ember, Blood, Stain, Dust, Chip, Debris, Smoke, Wisp };
struct Particle {
  float x = 0, y = 0, z = 0;     // ground point (area px) and height above it
  float vx = 0, vy = 0, vz = 0;  // px / s
  float age = 0, life = 0;       // s
  float size = 0;
  float r = 0, g = 0, b = 0, a = 0;
  Kind kind = Kind::Blood;
  bool landed = false;
};
inline bool IsDecal(Kind k) { return k == Kind::Stain || k == Kind::Debris; }
inline bool IsAdditive(Kind k) { return k == Kind::Spark || k == Kind::Ember || k == Kind::Wisp; }

struct Flash {  // spark impact light
  float x, y, z, radius, r, g, b, age, life;
};

struct HitEvent {
  float x = 0, y = 0;     // feet, area px
  float height = 45;      // where it is hit, px above the feet
  Material mat = Material::Flesh;
  int severity = 1;       // 1..3, 0 = gory death
  bool crit = false;      // exaggerated: as if far more damage, a second spray out the other side
  float dir = -1;         // spray direction (radians, screen: 0 = right, pi/2 = down); < 0 = random
};

// Engine direction (0..15, 0 = south, 4 = west, 8 = north, 12 = east) -> screen angle (radians).
float DirectionAngle(int engine_dir);

class Particles {
 public:
  size_t max_particles = 1500, max_decals = 600;
  float linger = 30.0f;  // s a stain / debris stays
  Rng rng;
  std::vector<Particle> p;
  std::vector<Flash> flashes;

  void Hit(const HitEvent& e);
  void Simulate(float dt);
  void Clear() {
    p.clear();
    flashes.clear();
  }

 private:
  bool Room() const { return p.size() < max_particles; }
  void Land(Particle& q);
  void TrimDecals();
};

// Draw parameters of a particle at its age (GemRB DrawParticles / DrawGroundDecals): screen centre offset
// above the ground point, radii (px), colour and alpha; disc = hard edge, else soft blob.
struct Shape {
  float x, y, rx, ry, r, g, b, a;
  bool disc, additive;
};
Shape ShapeOf(const Particle& q);

}  // namespace pstf::gore
