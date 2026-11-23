// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Contributors to the GemRB project <https://gemrb.org> (ScenePolish materials and
// hit particles, gemrb/core/ScenePolish.cpp)
#include "modules/gore/gore_logic.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace pstf::gore {
namespace {
constexpr float kGravity = 520.0f;
constexpr float kPi = 3.14159265f;

std::string Upper(std::string s) {
  for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

const char* MaterialName(Material m) {
  static const char* names[] = {"FLESH", "LEATHER", "ARMORED", "ARMORED_BONE", "HOLLOW", "STONE",
                                "BONE",  "SPIRIT",  "METAL",   "SHADOW",       "FIRE"};
  const size_t i = size_t(m);
  return i < sizeof names / sizeof *names ? names[i] : "?";
}

Material MaterialForArmor(int armor) {
  switch (armor) {
    case 1: return Material::Leather;  // thugs, Dak'kon, githzerai
    case 2: return Material::Armored;  // guards, Mercykillers, Harmonium
    case 3: return Material::Stone;    // golems, statues
    case 4: return Material::Bone;     // Morte, skeletons
    case 6: return Material::Spirit;   // Deionarra, the Transcendent One
    case 7: return Material::Metal;    // modrons, Nordom
    case 9: return Material::Shadow;
    case 10: return Material::Fire;    // Ignus
    default: return Material::Flesh;   // 0 townies, 5 fiends / zombies, 8 the Nameless One
  }
}

bool MaterialFromName(const std::string& name, Material* out) {
  const std::string u = Upper(name);
  for (int i = 0; i <= int(Material::Fire); ++i)
    if (u == MaterialName(Material(i))) {
      *out = Material(i);
      return true;
    }
  return false;
}

Material MaterialTable::Of(int anim_id) const {
  // EE ids go past 255 (RESDATA [558]): the exact id first, then its low byte (classic PST ids)
  for (const int id : {anim_id, anim_id & 0xFF}) {
    auto a = anim_rows.find(id);
    if (a != anim_rows.end()) return a->second;
  }
  auto r = armor_of_anim.find(anim_id);
  if (r == armor_of_anim.end()) r = armor_of_anim.find(anim_id & 0xFF);
  if (r == armor_of_anim.end()) return Material::Flesh;
  auto o = armor_rows.find(r->second);
  return o != armor_rows.end() ? o->second : MaterialForArmor(r->second);
}

bool ParseHitmat(const std::string& text, MaterialTable* out) {
  std::istringstream in(text);
  std::string line;
  if (!std::getline(in, line) || Upper(line).find("2DA") == std::string::npos) return false;
  std::getline(in, line);  // default value
  std::getline(in, line);  // column names
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string row, value;
    if (!(ls >> row >> value)) continue;
    Material m;
    if (!MaterialFromName(value, &m)) continue;
    row = Upper(row);
    if (row.rfind("ANIM", 0) == 0) out->anim_rows[std::atoi(row.c_str() + 4)] = m;
    else if (row.rfind("ARMOR", 0) == 0) out->armor_rows[std::atoi(row.c_str() + 5)] = m;
  }
  return true;
}

void ParseArmorList(const std::string& text, MaterialTable* out) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    int anim = -1, armor = -1;
    if (ls >> anim >> armor && anim >= 0 && armor >= 0) out->armor_of_anim[anim] = armor;
  }
}

int GushSeverity(int t) {
  switch (t) {
    case 3: return 1;  // GUSH_LOW
    case 4: return 2;  // GUSH_MEDIUM
    case 5: return 3;  // GUSH_HIGH
    case 6: return 3;  // PULSATING_ARTERY
    case 7: return 0;  // EXPLODING_DEATH
    default: return -1;
  }
}

float Rng::Next() {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return float(s >> 8) / 16777216.0f;
}

float DirectionAngle(int d) {
  // 0 south (+y), 4 west (-x), 8 north, 12 east: a quarter turn per 4 steps, clockwise from south.
  return kPi * 0.5f + float(d & 15) * (2.0f * kPi / 16.0f);
}

void Particles::Land(Particle& q) {
  if (q.kind == Kind::Blood) {  // splat: a flat stain, a little bigger than the drop, that dries darker
    q.kind = Kind::Stain;
    q.size *= rng.Range(1.3f, 2.4f);
    q.r *= 0.8f;
    q.a = rng.Range(0.6f, 0.85f);
  } else if (!q.landed) {  // chips bounce once
    q.landed = true;
    q.z = 0.0f;
    q.vz = -q.vz * 0.3f;
    q.vx *= 0.45f;
    q.vy *= 0.45f;
    return;
  } else {
    q.kind = Kind::Debris;
  }
  q.z = 0.0f;
  q.vx = q.vy = q.vz = 0.0f;
  q.age = 0.0f;
  q.life = linger * rng.Range(0.85f, 1.15f);
}

void Particles::TrimDecals() {
  size_t decals = 0;
  for (const Particle& q : p) decals += IsDecal(q.kind);
  if (decals <= max_decals) return;
  size_t drop = decals - max_decals;  // oldest first (pushed earlier)
  p.erase(std::remove_if(p.begin(), p.end(),
                         [&drop](const Particle& q) {
                           if (drop && IsDecal(q.kind)) {
                             --drop;
                             return true;
                           }
                           return false;
                         }),
          p.end());
}

void Particles::Simulate(float dt) {
  if (dt <= 0.0f) return;
  for (Particle& q : p) {
    q.age += dt;
    switch (q.kind) {
      case Kind::Spark:
        q.vz -= 420.0f * dt;
        q.vx *= 1.0f - 2.5f * dt;
        q.vy *= 1.0f - 2.5f * dt;
        break;
      case Kind::Blood:
      case Kind::Chip:
        q.vz -= kGravity * dt;
        q.vx *= 1.0f - 0.8f * dt;
        q.vy *= 1.0f - 0.8f * dt;
        break;
      case Kind::Dust:
        q.vx *= 1.0f - 3.0f * dt;
        q.vy *= 1.0f - 3.0f * dt;
        q.vz *= 1.0f - 1.5f * dt;
        q.size += 3.5f * dt;
        break;
      case Kind::Smoke:
        q.vx *= 1.0f - 2.0f * dt;
        q.vy *= 1.0f - 2.0f * dt;
        q.vx += rng.Range(-20.0f, 20.0f) * dt;
        q.size += 4.0f * dt;
        break;
      case Kind::Wisp:
        q.vx *= 1.0f - 1.5f * dt;
        q.vy *= 1.0f - 1.5f * dt;
        q.vx += rng.Range(-30.0f, 30.0f) * dt;
        q.vz += 20.0f * dt;
        break;
      case Kind::Ember:
        q.vz *= 1.0f - 0.6f * dt;
        q.vx += rng.Range(-40.0f, 40.0f) * dt;
        break;
      case Kind::Stain:
      case Kind::Debris:
        continue;  // lying still on the floor
    }
    q.x += q.vx * dt;
    q.y += q.vy * dt;
    q.z += q.vz * dt;
    if (q.kind == Kind::Spark && q.z < 0.0f) {  // bounce once, dimmer
      q.z = 0.0f;
      q.vz = -q.vz * 0.3f;
      q.a *= 0.6f;
    } else if ((q.kind == Kind::Blood || q.kind == Kind::Chip) && q.z <= 0.0f) {
      Land(q);
    }
  }
  TrimDecals();
  p.erase(std::remove_if(p.begin(), p.end(), [](const Particle& q) { return q.age >= q.life; }), p.end());
  for (Flash& f : flashes) f.age += dt;
  flashes.erase(std::remove_if(flashes.begin(), flashes.end(), [](const Flash& f) { return f.age >= f.life; }),
                flashes.end());
}

void Particles::Hit(const HitEvent& e) {
  const Material mat = e.mat;
  const bool gore = e.severity <= 0, crit = e.crit;
  const float sev = (gore ? 4.0f : float(std::min(3, e.severity))) + (crit ? 3.0f : 0.0f);
  const float big = crit ? 0.5f : 0.0f;  // extra particle size
  const float dir = e.dir >= 0 ? e.dir : rng.Range(0.0f, 2.0f * kPi);

  auto spawn = [&](Kind kind, float spread, float speed_lo, float speed_hi, float vz_lo, float vz_hi) {
    Particle q;
    q.kind = kind;
    // crits: a third of everything goes out the other side
    const float ang = (crit && rng.Next() < 0.33f ? dir + 2.6f : dir) + rng.Range(-spread, spread);
    const float speed = rng.Range(speed_lo, speed_hi);
    q.x = e.x + rng.Range(-3.0f, 3.0f);
    q.y = e.y + rng.Range(-2.0f, 2.0f);
    q.z = e.height + rng.Range(-8.0f, 8.0f);
    q.vx = std::cos(ang) * speed;
    q.vy = std::sin(ang) * speed * 0.6f;
    q.vz = rng.Range(vz_lo, vz_hi);
    q.life = 4.0f;  // until it lands
    return q;
  };

  if (Bleeds(mat)) {
    // a directional spray, a few drips straight down, a quick red mist
    const int n = int(6.0f + sev * 7.0f);
    for (int i = 0; i < n && Room(); ++i) {
      const bool flung = rng.Next() < 0.25f;  // most drops land close together, a few fly further
      Particle q = flung ? spawn(Kind::Blood, 0.7f, 50.0f, 90.0f + sev * 15.0f, 20.0f, 110.0f)
                         : spawn(Kind::Blood, 0.9f, 8.0f, 40.0f + sev * 6.0f, -20.0f, 60.0f);
      q.size = (flung ? rng.Range(0.5f, 0.9f) : rng.Range(0.8f, 1.6f) + (gore ? 0.5f : 0.0f)) + big;
      q.r = rng.Range(0.42f, 0.6f);
      q.g = rng.Range(0.0f, 0.04f);
      q.b = rng.Range(0.01f, 0.04f);
      q.a = 0.95f;
      p.push_back(q);
    }
    for (int i = 0; i < int(sev * 2.0f) && Room(); ++i) {
      Particle q = spawn(Kind::Blood, 3.2f, 0.0f, 18.0f, -10.0f, 15.0f);
      q.size = rng.Range(0.8f, 1.5f);
      q.r = rng.Range(0.4f, 0.55f);
      q.g = q.b = 0.02f;
      q.a = 0.95f;
      p.push_back(q);
    }
    for (int i = 0; i < (crit ? 7 : 3) && Room(); ++i) {
      Particle q = spawn(Kind::Dust, 1.2f, 10.0f, 35.0f, -5.0f, 10.0f);
      q.life = rng.Range(0.25f, 0.45f) * (crit ? 1.6f : 1.0f);
      q.size = rng.Range(2.5f, 4.0f) + sev * 0.4f;
      q.r = 0.5f;
      q.g = q.b = 0.03f;
      q.a = 0.4f;
      p.push_back(q);
    }
  }

  if (Sparks(mat)) {
    const int n = int(5.0f + sev * 3.0f);
    for (int i = 0; i < n && Room(); ++i) {
      Particle q = spawn(Kind::Spark, 1.4f, 90.0f, 230.0f, 30.0f, 170.0f);
      q.life = rng.Range(0.15f, 0.4f);
      q.size = rng.Range(0.6f, 1.1f) + big * 0.5f;
      q.r = 1.0f;
      q.g = rng.Range(0.75f, 0.95f);
      q.b = rng.Range(0.35f, 0.6f);
      q.a = rng.Range(0.8f, 1.0f);
      p.push_back(q);
    }
    const float k = crit ? 1.6f : 1.0f;
    flashes.push_back({e.x, e.y, e.height, 55.0f + sev * 10.0f, 0.5f * k, 0.4f * k, 0.22f * k, 0.0f,
                       crit ? 0.22f : 0.12f});
  }

  // dust and chips that fall off and stay on the floor
  auto crumble = [&](float r, float g, float b, int chips, int puffs) {
    for (int i = 0; i < puffs && Room(); ++i) {
      Particle q = spawn(Kind::Dust, 3.2f, 8.0f, 30.0f, 3.0f, 22.0f);
      q.life = rng.Range(0.8f, 1.6f);
      q.size = rng.Range(2.5f, 4.5f);
      q.r = r * 0.9f;
      q.g = g * 0.9f;
      q.b = b * 0.9f;
      q.a = 0.32f;
      p.push_back(q);
    }
    for (int i = 0; i < chips && Room(); ++i) {
      Particle q = spawn(Kind::Chip, 1.6f, 30.0f, 100.0f + big * 80.0f, 30.0f, 120.0f + big * 60.0f);
      q.size = rng.Range(0.5f, 1.2f) + big * 0.6f;
      const float k = rng.Range(0.82f, 1.0f);
      q.r = r * k;
      q.g = g * k;
      q.b = b * k;
      q.a = 0.95f;
      p.push_back(q);
    }
  };
  auto wisps = [&](Kind kind, int n, float r, float g, float b, float a, float s_lo, float s_hi, float l_lo,
                   float l_hi, float sp_hi, float vz_lo, float vz_hi) {
    for (int i = 0; i < n && Room(); ++i) {
      Particle q = spawn(kind, 3.2f, 10.0f, sp_hi, vz_lo, vz_hi);
      q.life = rng.Range(l_lo, l_hi);
      q.size = rng.Range(s_lo, s_hi);
      q.r = r;
      q.g = g;
      q.b = b;
      q.a = a;
      p.push_back(q);
    }
  };
  switch (mat) {
    case Material::Bone:
    case Material::ArmoredBone: crumble(0.9f, 0.86f, 0.74f, int(2.0f + sev * 3.0f), int(2.0f + sev)); break;
    case Material::Stone: crumble(0.55f, 0.53f, 0.5f, int(2.0f + sev * 3.0f), int(2.0f + sev)); break;
    case Material::Metal: crumble(0.36f, 0.38f, 0.42f, int(sev * 2.0f), 0); break;
    case Material::Spirit:
      wisps(Kind::Wisp, int(6.0f + sev * 3.0f), 0.6f, 0.8f, 1.0f, 0.5f, 1.5f, 3.0f, 0.8f, 1.5f, 50.0f, 15.0f, 50.0f);
      break;
    case Material::Shadow:
      wisps(Kind::Smoke, int(5.0f + sev * 2.0f), 0.03f, 0.02f, 0.05f, 0.45f, 3.0f, 6.0f, 0.9f, 1.6f, 40.0f, 10.0f,
            30.0f);
      break;
    case Material::Fire:
      for (int i = 0; i < int(8.0f + sev * 4.0f) && Room(); ++i) {
        Particle q = spawn(Kind::Ember, 3.2f, 20.0f, 90.0f, 30.0f, 90.0f);
        q.life = rng.Range(0.5f, 1.2f);
        q.size = rng.Range(0.7f, 1.4f);
        q.r = 1.0f;
        q.g = rng.Range(0.45f, 0.7f);
        q.b = 0.15f;
        q.a = rng.Range(0.7f, 1.0f);
        p.push_back(q);
      }
      break;
    default: break;
  }
}

Shape ShapeOf(const Particle& q) {
  Shape s{};
  const float t = q.life > 0 ? q.age / q.life : 1.0f;
  s.x = q.x;
  s.y = q.y - q.z;
  s.r = q.r;
  s.g = q.g;
  s.b = q.b;
  s.additive = IsAdditive(q.kind);
  if (q.kind == Kind::Stain) {  // spreads a touch as it soaks in, dries darker
    const float fade = t < 0.75f ? 1.0f : (1.0f - t) / 0.25f;
    const float spread = 1.0f + 0.25f * std::min(1.0f, q.age * 2.0f);
    s.rx = q.size * 1.5f * spread;
    s.ry = s.rx * 0.5f;
    const float dry = 1.0f - 0.35f * std::min(1.0f, q.age / 6.0f);
    s.r *= dry;
    s.g *= dry;
    s.b *= dry;
    s.a = q.a * fade;
    s.disc = true;
    return s;
  }
  if (q.kind == Kind::Debris) {
    s.rx = q.size;
    s.ry = q.size * 0.6f;
    s.a = q.a * (t < 0.75f ? 1.0f : (1.0f - t) / 0.25f);
    s.disc = true;
    return s;
  }
  float fade;
  switch (q.kind) {
    case Kind::Ember: fade = (1.0f - t) * (0.75f + 0.25f * std::sin(q.age * 23.0f + q.x)); break;
    case Kind::Wisp:
    case Kind::Smoke: fade = std::sin(t * kPi); break;
    case Kind::Spark: fade = 1.0f - t * t; break;
    case Kind::Dust: fade = (1.0f - t) * std::min(1.0f, t * 8.0f + 0.3f); break;
    default: fade = 1.0f; break;  // Blood, Chip
  }
  const float size = q.kind == Kind::Spark ? q.size * (1.0f - 0.5f * t) : q.size;
  s.disc = q.kind == Kind::Blood || q.kind == Kind::Chip;
  s.rx = s.ry = s.disc ? size : size * 1.6f;
  s.a = q.a * fade;
  return s;
}

}  // namespace pstf::gore
