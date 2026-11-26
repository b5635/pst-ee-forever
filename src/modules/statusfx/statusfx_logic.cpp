// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/statusfx/statusfx_logic.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>

namespace pstf::statusfx {
namespace {

constexpr float kPi = 3.14159265f;

const char* const kNames[] = {"fear", "stun", "stunloop", "blind", "acid", "sonic", "mantle"};

float Lerp(float a, float b, float t) { return a + (b - a) * t; }
float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Emitter births in [t0, t1) at `rate` per second while the window overlaps [from, to).
int Births(float* acc, float rate, float t0, float t1, float from = 0, float to = 1e9f) {
  const float a = std::max(t0, from), b = std::min(t1, to);
  if (b <= a || rate <= 0) return 0;
  *acc += rate * (b - a);
  const int n = static_cast<int>(*acc);
  *acc -= static_cast<float>(n);
  return n;
}

// A direction inside a cone of half angle `half` around up (z), or anywhere for half >= pi.
void Direction(Rng& r, float half, float* x, float* y, float* z) {
  const float th = r.Range(0, 2 * kPi);
  const float cz = Lerp(1.0f, std::cos(std::min(half, kPi)), r.Next());
  const float s = std::sqrt(std::max(0.0f, 1 - cz * cz));
  *x = s * std::cos(th);
  *y = s * std::sin(th);
  *z = cz;
}

}  // namespace

const char* FxName(Fx f) { return f < Fx::Count ? kNames[static_cast<int>(f)] : "?"; }

bool FxFromName(const std::string& name, Fx* out) {
  std::string n;
  for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (int i = 0; i < static_cast<int>(Fx::Count); ++i)
    if (n == kNames[i]) {
      *out = static_cast<Fx>(i);
      return true;
    }
  return false;
}

bool IsLoop(Fx f) { return f == Fx::Fear || f == Fx::StunLoop; }

float FxLength(Fx f) {
  switch (f) {
    case Fx::Fear: return 1.667f;      // vdr_mindhit04 "duration" cycle
    case Fx::Stun: return 0.967f;      // vim_mentstun emits 0.033..0.967 (1.97 s with its tail)
    case Fx::StunLoop: return 1.0f;    // vdr_mindhit03 "duration"
    case Fx::Blind: return 1.0f;       // vim_blinddeaf emits 0..1.0
    case Fx::Acid:
    case Fx::Sonic: return 0.967f;     // vim_shwng*01 "impact"
    case Fx::Mantle: return 0.833f;    // vim_cntmantle "impact"
    default: return 1.0f;
  }
}

float Rng::Next() {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return static_cast<float>(s >> 8) / 16777216.0f;
}

float PxPerMetre(float head_px) {
  if (head_px <= 0) head_px = 70;
  return std::clamp(head_px / (1.9f * kCosE), 18.0f, 80.0f);
}

Particle& System::Add(int32_t owner) {
  if (p.size() >= max_particles) p.erase(p.begin());  // oldest first
  p.emplace_back();
  p.back().owner = owner;
  return p.back();
}

void System::Start(Fx fx, int32_t owner) {
  if (IsLoop(fx))
    for (Instance& in : inst)
      if (in.fx == fx && in.owner == owner && !in.stopping) return;  // already running
  Instance in;
  in.fx = fx;
  in.owner = owner;
  inst.push_back(in);
}

void System::Stop(Fx fx, int32_t owner) {
  for (Instance& in : inst)
    if (in.fx == fx && in.owner == owner) in.stopping = true;
}

bool System::Active(Fx fx, int32_t owner) const {
  for (const Instance& in : inst)
    if (in.fx == fx && in.owner == owner && !in.stopping) return true;
  return false;
}

int System::Count(int32_t owner) const {
  int n = 0;
  for (const Particle& q : p) n += q.owner == owner;
  return n;
}

void System::Forget(int32_t owner) {
  inst.erase(std::remove_if(inst.begin(), inst.end(), [&](const Instance& i) { return i.owner == owner; }), inst.end());
  p.erase(std::remove_if(p.begin(), p.end(), [&](const Particle& q) { return q.owner == owner; }), p.end());
}

// Each effect, ported from its NWN model: emitter positions, rates, lives, speeds, sizes, colours.
void System::Emit(Instance& in, float t0, float t1) {
  Rng& r = rng;
  const int32_t o = in.owner;
  const float h = kHeadM;
  switch (in.fx) {
    case Fx::Fear: {
      // vdr_mindhit04: a dummy 0.54 m above the head turns 4.71 rad/s (a quarter turn per 0.333 s) and carries
      // two emitters, 2 skulls/s each (ramp 0 -> 2 over the 0.3 s impact). A skull (chunk fx_skull) leaves at
      // 1.5 + 0.5 m/s in any direction, drag 1, and is pulled (p2p gravity 2.3 m/s^2) toward its emitter's
      // reference node: (0.225, -0.309, -0.602) and (-0.225, 0.330, -0.603) from the emitter in the turning
      // frame, i.e. two points circling the head 0.38 m out at head height, on opposite sides. So the skulls
      // burst up, fall back and swirl round the head. Life 2 s, alpha 0.8, turning +-0.2; each trails fire
      // (fxpa_FireBb) at 10/s for its first 1.1 s (life 1 s, growing, rising 0.2 m/s).
      constexpr float kSpin = 4.71f, kUp = 0.54f;
      static const float kRef[2][3] = {{0.225f, -0.309f, -0.602f}, {-0.225f, 0.330f, -0.603f}};
      const float ramp = Clamp01(t1 / 0.3f);
      for (int e = 0; e < 2; ++e) {
        const int n = Births(&in.acc[e], 2.0f * ramp, t0, t1);
        for (int i = 0; i < n; ++i) {
          const float t = Lerp(t0, t1, r.Next());
          Particle& q = Add(o);
          float dx, dy, dz;
          Direction(r, kPi, &dx, &dy, &dz);
          const float v = 1.5f + r.Range(0, 0.5f);
          q.z = h + kUp;
          q.vx = dx * v, q.vy = dy * v, q.vz = dz * v;
          q.drag = 1.0f;
          q.grav = 2.3f;
          q.tx = kRef[e][0], q.ty = kRef[e][1], q.tz = h + kUp + kRef[e][2];
          q.tphase = kSpin * t;
          q.tspin = kSpin;
          q.life = 2.0f;
          q.size0 = q.size1 = 0.22f;
          q.a0 = 0.85f;
          q.a1 = 0.0f;
          q.fade_in = 0.15f;
          q.hold = 0.75f;
          q.cell = kSkull;
          q.frame0 = static_cast<uint8_t>(r.Next() < 0.5f ? 0 : 1);
          q.additive = false;
          q.rot = r.Range(-0.3f, 0.3f);
          q.vrot = e ? -0.2f : 0.2f;
          q.trail = 10.0f;
          q.trail_until = 1.1f;
        }
      }
      break;
    }
    case Fx::Stun: {
      // vim_mentstun: two star emitters circle the head (radius 0.5 m, about one turn a second) from 0.033 to
      // 1.0 s, 5 stars/s each (life 0.5 / 1.0 s, 0.3 -> 0 m, white fading out, 4 frames at 8 fps); a burst of
      // lightning streaks from the head (200/s in NWN, 0.033..0.967 s, life 0.3 s, 3 m/s any direction,
      // 0.5 -> 0 m, alpha 0.2 -> 0.5, light blue).
      for (int e = 0; e < 2; ++e) {
        const int n = Births(&in.acc[e], 5.0f, t0, t1, 0.033f, 1.0f);
        for (int i = 0; i < n; ++i) {
          const float t = Lerp(t0, t1, r.Next());
          const float ang = (e ? kPi : 0.0f) + 6.3f * t;
          Particle& q = Add(o);
          q.x = 0.5f * std::cos(ang);
          q.y = 0.5f * std::sin(ang);
          q.z = h + 0.05f;
          q.life = e ? 1.0f : 0.5f;
          q.size0 = 0.3f;
          q.size1 = 0.0f;
          q.r1 = q.g1 = q.b1 = 0.0f;  // white -> black (additive: fades out)
          q.cell = kGlint;
          q.frames = 4;
          q.fps = 8;
        }
      }
      const int n = Births(&in.acc[2], 120.0f, t0, t1, 0.033f, 0.967f);
      for (int i = 0; i < n; ++i) {
        Particle& q = Add(o);
        float dx, dy, dz;
        Direction(r, kPi, &dx, &dy, &dz);
        q.z = h + 0.13f;
        q.vx = dx * 3;
        q.vy = dy * 3;
        q.vz = dz * 3;
        q.life = 0.3f;
        q.size0 = 0.5f;
        q.size1 = 0.0f;
        q.a0 = 0.5f;
        q.a1 = 0.2f;
        q.r0 = q.r1 = 0.6f;
        q.g0 = q.g1 = 0.85f;
        q.cell = static_cast<uint8_t>(kBolt + static_cast<int>(r.Next() * 4) % 4);
        q.mode = 1;
      }
      break;
    }
    case Fx::StunLoop: {
      // vdr_mindhit03: a dummy turning once a second (clockwise from above) carries two emitters 0.45 m out:
      // 8 blue stars/s (life 0.7 s, 0.3 -> 0 m, (0.44, 0.66, 1) -> (0.61, 0.77, 1), 20 fps, jitter 0.1 m/s)
      // and 5 white stars/s opposite (life 0.5 s, 0.2 -> 0.05 m, 30 fps). The stars turn with the dummy.
      const float spin = -2 * kPi;
      for (int e = 0; e < 2; ++e) {
        const int n = Births(&in.acc[e], e ? 5.0f : 8.0f, t0, t1);
        for (int i = 0; i < n; ++i) {
          const float t = Lerp(t0, t1, r.Next());
          const float ang = (e ? kPi * 0.5f : -kPi * 0.5f) + spin * t;
          Particle& q = Add(o);
          q.x = 0.45f * std::cos(ang);
          q.y = 0.45f * std::sin(ang);
          q.z = h + 0.02f;
          q.spin = spin;
          if (!e) {
            float dx, dy, dz;
            Direction(r, kPi, &dx, &dy, &dz);
            q.vx = dx * 0.1f, q.vy = dy * 0.1f, q.vz = dz * 0.1f;
            q.life = 0.7f;
            q.size0 = 0.3f;
            q.size1 = 0.0f;
            q.r0 = 0.44f, q.g0 = 0.66f, q.r1 = 0.61f, q.g1 = 0.77f;
            q.fps = 20;
          } else {
            q.life = 0.5f;
            q.size0 = 0.2f;
            q.size1 = 0.05f;
            q.fps = 30;
          }
          q.cell = kGlint;
          q.frames = 4;
          q.frame0 = static_cast<uint8_t>(r.Next() * 4) % 4;
        }
      }
      break;
    }
    case Fx::Blind: {
      // vim_blinddeaf: two emitters circle the head 0.43 m out (about 6.5 rad/s) for 1 s, 7 glints/s each:
      // life 1 s, growing 0 -> 1.5 m while they fade (white -> black), turning -2 rad/s, 16 fps.
      for (int e = 0; e < 2; ++e) {
        const int n = Births(&in.acc[e], 7.0f, t0, t1, 0.0f, 1.0f);
        for (int i = 0; i < n; ++i) {
          const float t = Lerp(t0, t1, r.Next());
          const float ang = (e ? kPi : 0.0f) + 6.5f * t;
          Particle& q = Add(o);
          q.x = 0.43f * std::cos(ang);
          q.y = 0.43f * std::sin(ang);
          q.z = h;
          q.life = 1.0f;
          q.size0 = 0.0f;
          q.size1 = 1.1f;
          q.r0 = 0.85f, q.g0 = 0.92f;
          q.r1 = q.g1 = q.b1 = 0.0f;
          q.vrot = -2.0f;
          q.cell = kGlint;
          q.frames = 4;
          q.fps = 16;
        }
      }
      break;
    }
    case Fx::Acid:
    case Fx::Sonic: {
      // vim_shwngacid01 / vim_shwngsonc01: four spark emitters wait at the head (0.2 m below the attach
      // point), then from 0.3 to 0.6 s swing out 0.37 m on the diagonals and in again to 0.5 m above it,
      // 10 sparks/s each all along (life 0.2 s, 0.6 -> 0.1 m, 3 frames at 15 fps; acid: green -> white,
      // one white). At 0.6 s it bursts there: one big glint (2 m in NWN) to 0.93 s and 5 smoke puffs
      // (0.5 + 0.3 m/s in a 120 degree cone up, life 1 s, 0.2 -> 1 m, white -> green, turning 1 rad/s).
      const bool acid = in.fx == Fx::Acid;
      const float gr = acid ? 0.05f : 1.0f, gg = 1.0f, gb = acid ? 0.0f : 1.0f;
      for (int e = 0; e < 4; ++e) {
        const int n = Births(&in.acc[e], 10.0f, t0, t1, 0.0f, 0.933f);
        for (int i = 0; i < n; ++i) {
          const float t = Lerp(t0, t1, r.Next());
          const float s = Clamp01((t - 0.3f) / 0.3f);
          const float ang = kPi * 0.25f + kPi * 0.5f * e;
          const float rad = t > 0.6f ? 0.0f : 0.37f * std::sin(kPi * s);
          Particle& q = Add(o);
          q.x = rad * std::cos(ang);
          q.y = rad * std::sin(ang);
          q.z = h - 0.2f + 0.7f * (1 - std::cos(kPi * s)) * 0.5f;
          q.life = 0.2f;
          q.size0 = 0.6f;
          q.size1 = 0.1f;
          const bool white = !acid || e == 0;
          q.r0 = white ? 1 : gr, q.g0 = white ? 1 : gg, q.b0 = white ? 1 : gb;
          q.cell = kGlint;
          q.frames = 3;
          q.fps = 15;
        }
      }
      if (!in.fired && t1 >= 0.6f) {
        in.fired = true;
        Particle& g = Add(o);
        g.z = h + 0.5f;
        g.life = 0.35f;
        g.size0 = 1.2f;
        g.size1 = 1.3f;
        g.a0 = 0.9f;
        g.a1 = 0.0f;
        g.hold = 0.5f;
        g.r0 = g.r1 = gr, g.g0 = g.g1 = gg, g.b0 = g.b1 = gb;
        g.cell = kBurst;
        g.rot = r.Range(0, 2 * kPi);
        g.vrot = 1.0f;
        for (int i = 0; i < 5; ++i) {
          Particle& q = Add(o);
          float dx, dy, dz;
          Direction(r, kPi / 3, &dx, &dy, &dz);
          const float v = 0.5f + r.Range(0, 0.3f);
          q.z = h + 0.5f;
          q.vx = dx * v, q.vy = dy * v, q.vz = dz * v;
          q.life = 1.0f;
          q.size0 = 0.2f;
          q.size1 = 1.0f;
          q.a0 = 0.8f;
          q.a1 = 0.0f;
          q.r1 = gr, q.g1 = gg, q.b1 = gb;
          q.cell = kSmoke;
          q.frames = 8;
          q.fps = 15;
          q.frame0 = static_cast<uint8_t>(r.Next() * 8) % 8;
          q.rot = r.Range(0, 2 * kPi);
          q.vrot = 1.0f;
        }
      }
      break;
    }
    case Fx::Mantle: {
      // vim_cntmantle: 12 columns x 4 rows of glassy panes on a cylinder round the body (rows 0.41, 1.01,
      // 1.58, 2.18 m; 1.5 m out in NWN), each 0.55 m, blue: they fade in in turn (0.1 .. 0.27 s), close in,
      // hold to 0.5 s and are gone by 0.73 s. A blue glow at the chest under them.
      if (in.fired) break;
      in.fired = true;
      static const float rows[4] = {0.41f, 1.01f, 1.58f, 2.18f};
      for (int c = 0; c < 12; ++c)
        for (int rw = 0; rw < 4; ++rw) {
          Particle& q = Add(o);
          q.theta = (c + 0.5f) * kPi / 6.0f;
          q.rad0 = 0.85f;  // NWN 1.5 m: too wide among PST's crowds
          q.rad1 = 0.62f;
          q.close_t = 0.3f;
          q.z = rows[rw];
          q.delay = r.Range(0.0f, 0.13f);
          q.life = 0.73f;
          q.fade_in = 0.14f;
          q.hold = 0.55f;
          q.a0 = 0.5f;  // 48 additive panes: brighter washes out to white
          q.a1 = 0.0f;
          q.size0 = q.size1 = 0.42f;
          q.r0 = q.r1 = 0.28f, q.g0 = q.g1 = 0.45f;
          q.cell = kShard;
          q.mode = 3;
        }
      Particle& g = Add(o);
      g.z = 1.0f;
      g.life = 0.6f;
      g.size0 = 1.6f;
      g.size1 = 2.2f;
      g.a0 = 0.35f;
      g.a1 = 0.0f;
      g.r0 = g.r1 = 0.25f, g.g0 = g.g1 = 0.4f;
      g.cell = kGlow;
      break;
    }
    default:
      break;
  }
}

void System::Update(float dt) {
  if (dt < 0) dt = 0;
  // emitters
  for (Instance& in : inst) {
    const float t0 = in.t, t1 = in.t + dt;
    if (!in.stopping) {
      if (IsLoop(in.fx)) {
        Emit(in, t0, t1);
      } else if (t0 < FxLength(in.fx)) {
        Emit(in, t0, std::min(t1, FxLength(in.fx)));
      }
    }
    in.t = t1;
    if (!IsLoop(in.fx) && in.t >= FxLength(in.fx)) in.stopping = true;
  }
  // a stopped loop and a finished impact go; the particles they made live on by themselves
  inst.erase(std::remove_if(inst.begin(), inst.end(), [](const Instance& i) { return i.stopping; }), inst.end());
  // particles
  std::vector<Particle> born;
  for (Particle& q : p) {
    if (q.delay > 0) {
      q.delay -= dt;
      continue;
    }
    q.age += dt;
    if (q.drag > 0) {
      const float k = std::max(0.0f, 1 - q.drag * dt);
      q.vx *= k, q.vy *= k, q.vz *= k;
    }
    if (q.grav > 0) {  // p2p gravity toward a turning target
      const float ang = q.tphase + q.tspin * q.age, c = std::cos(ang), s = std::sin(ang);
      const float gx = q.tx * c - q.ty * s - q.x, gy = q.tx * s + q.ty * c - q.y, gz = q.tz - q.z;
      const float len = std::sqrt(gx * gx + gy * gy + gz * gz) + 1e-6f;
      q.vx += q.grav * gx / len * dt, q.vy += q.grav * gy / len * dt, q.vz += q.grav * gz / len * dt;
    }
    q.x += q.vx * dt, q.y += q.vy * dt, q.z += q.vz * dt;
    q.rot += q.vrot * dt;
    if (q.trail > 0 && q.age < q.life && q.age < (q.trail_until > 0 ? q.trail_until : q.life)) {
      q.trail_acc += q.trail * dt;
      while (q.trail_acc >= 1) {
        q.trail_acc -= 1;
        Particle f;
        f.owner = q.owner;
        f.x = q.x + rng.Range(-0.04f, 0.04f);
        f.y = q.y + rng.Range(-0.04f, 0.04f);
        f.z = q.z - 0.02f;
        f.vz = 0.2f + rng.Range(0, 0.1f);
        f.life = 1.0f;
        f.size0 = 0.16f;  // x kSmallBoost: the skull (0.4 m drawn) stays visible inside its flame
        f.size1 = 0.28f;
        const float fade = q.a0 * (1 - Clamp01((q.age / q.life - q.hold) / (1 - q.hold)));
        f.a0 = 0.85f * fade;
        f.a1 = 0.0f;
        f.cell = kFire;
        f.frames = 8;
        f.fps = 15;
        f.frame0 = static_cast<uint8_t>(rng.Next() * 8) % 8;
        born.push_back(f);
      }
    }
  }
  p.erase(std::remove_if(p.begin(), p.end(), [](const Particle& q) { return q.age >= q.life; }), p.end());
  for (const Particle& f : born) {
    if (p.size() >= max_particles) break;
    p.push_back(f);
  }
}

void System::Draw(int32_t owner, const Anchor& a, std::vector<Quad>* out) const {
  const float ppm = PxPerMetre(a.head_px) * scale;
  const size_t first = out->size();
  for (const Particle& q : p) {
    if (q.owner != owner || q.delay > 0) continue;
    const float f = Clamp01(q.age / q.life);
    float alpha = q.a0;
    if (f > q.hold) alpha = Lerp(q.a0, q.a1, Clamp01((f - q.hold) / std::max(1e-3f, 1 - q.hold)));
    if (q.fade_in > 0) alpha *= Clamp01(q.age / q.fade_in);
    float x = q.x, y = q.y;
    const float z = q.z;
    if (q.spin != 0) {
      const float ang = q.spin * q.age, c = std::cos(ang), s = std::sin(ang);
      x = q.x * c - q.y * s;
      y = q.x * s + q.y * c;
    }
    // PST's sprites are small next to NWN's models: small particles (stars, skulls, sparks, flames, smoke)
    // draw kSmallBoost times their NWN size so they read; positions, glows and mantle panes keep theirs.
    const bool big = q.cell == kBurst || q.cell == kGlow || q.cell == kShard;
    float w = Lerp(q.size0, q.size1, f) * ppm * (big ? 1.0f : kSmallBoost), hh = w;
    float rot = q.rot;
    if (q.mode == 3) {  // mantle pane on the cylinder
      const float rad = Lerp(q.rad0, q.rad1, Clamp01(q.age / q.close_t));
      x = rad * std::cos(q.theta);
      y = rad * std::sin(q.theta);
      const float face = std::fabs(std::sin(q.theta));
      w *= 0.25f + 0.75f * face;
      hh *= 0.9f;
      if (y > 0) alpha *= 0.45f;  // behind the creature: dimmer
    } else if (q.mode == 1) {  // streak along its screen velocity
      const float sx = q.vx, sy = -(q.vz * kCosE + q.vy * kSinE);
      rot = std::atan2(sx, -sy);
      hh = w * 1.4f;
      w *= 0.45f;
    } else if (q.mode == 2) {
      hh *= kSinE;
    }
    if (alpha <= 0.004f || w < 0.5f) continue;
    Quad d;
    d.x = a.x + x * ppm;
    d.y = a.y - (z * kCosE + y * kSinE) * ppm;
    d.w = w;
    d.h = hh;
    d.rot = rot;
    uint8_t fr = q.frame0;
    if (q.fps > 0 && q.frames > 1) fr = static_cast<uint8_t>((q.frame0 + static_cast<int>(q.age * q.fps)) % q.frames);
    d.cell = static_cast<uint8_t>(q.cell + fr);
    d.r = Lerp(q.r0, q.r1, f);
    d.g = Lerp(q.g0, q.g1, f);
    d.b = Lerp(q.b0, q.b1, f);
    d.a = Clamp01(alpha);
    d.additive = q.additive;
    out->push_back(d);
  }
  std::stable_partition(out->begin() + static_cast<std::ptrdiff_t>(first), out->end(), [](const Quad& d) { return d.additive; });
}

// --- watcher -----------------------------------------------------------------------------------------
namespace {
struct Rule {
  uint32_t bit;
  Fx impact;  // Count = none
  Fx loop;    // Count = none
};
const Rule kRules[5] = {{kStatePanic, Fx::Count, Fx::Fear},
                        {kStateStunned, Fx::Stun, Fx::StunLoop},
                        {kStateBlind, Fx::Blind, Fx::Count},
                        {kStatePoisoned, Fx::Acid, Fx::Count},
                        {kStateSilenced, Fx::Sonic, Fx::Count}};
bool RuleOn(int i, const Toggles& on) {
  switch (i) {
    case 0: return on.fear;
    case 1: return on.stun;
    case 2: return on.blind;
    case 3: return on.poison;
    default: return on.silence;
  }
}
}  // namespace

uint32_t Watcher::Bits(int32_t id) const {
  const auto it = s_.find(id);
  return it == s_.end() ? 0 : it->second.bits;
}

void Watcher::Step(int32_t id, uint32_t state, double now, System* sys) {
  if (state & kStateDead) state = 0;
  S& s = s_[id];
  for (int i = 0; i < 5; ++i) {
    const Rule& r = kRules[i];
    const bool was = (s.bits & r.bit) != 0, is = (state & r.bit) != 0 && RuleOn(i, on);
    if (is && !was) {
      if (r.impact != Fx::Count) sys->Start(r.impact, id);
      if (r.loop != Fx::Count) sys->Start(r.loop, id);
      s.last[i] = now;
    } else if (is && r.loop == Fx::Count && repeat > 0 && now - s.last[i] >= repeat) {
      sys->Start(r.impact, id);
      s.last[i] = now;
    } else if (!is && was && r.loop != Fx::Count) {
      sys->Stop(r.loop, id);
    }
    if (is) s.bits |= r.bit;
    else s.bits &= ~r.bit;
  }
}

// --- glows -------------------------------------------------------------------------------------------
bool ReplacedStateOpcode(int op, const Toggles& on) {
  switch (op) {
    case 24: return on.fear;     // horror (panic)
    case 45: return on.stun;     // stun
    case 74: return on.blind;    // blindness
    case 25: return on.poison;   // poison
    case 38: return on.silence;  // silence
    default: return false;
  }
}

CutPlan GlowCutter::Plan(int32_t id, const std::vector<EffectInfo>& effs, double now) {
  CutPlan plan;
  std::set<std::string> replaced, blinding;
  for (const EffectInfo& e : effs) {
    if (e.parent.empty()) continue;
    if (ReplacedStateOpcode(e.opcode, on)) replaced.insert(e.parent);
    if (e.opcode == 74 && on.blind) blinding.insert(e.parent);
  }
  std::set<uint64_t> seen;
  for (const EffectInfo& e : effs) {
    if (e.opcode == 369 && (e.type == 6 || e.type == 7 || e.type == 8) && blinding.count(e.parent) &&
        e.res != kBlankBam) {
      plan.blank.push_back(e.key);
      continue;
    }
    if (!IsGlowOpcode(e.opcode) || e.equipped || e.type != 4096) continue;
    seen.insert(e.key);
    const auto k = std::make_pair(id, e.key);
    auto it = first_.find(k);
    if (it == first_.end()) it = first_.emplace(k, now).first;
    if (!e.parent.empty() && replaced.count(e.parent)) plan.expire.push_back(e.key);
    else if (cut_after > 0 && now - it->second >= cut_after) plan.expire.push_back(e.key);
  }
  for (auto it = first_.lower_bound({id, 0}); it != first_.end() && it->first.first == id;) {
    if (!seen.count(it->first.second)) it = first_.erase(it);
    else ++it;
  }
  return plan;
}

void GlowCutter::Forget(int32_t id) {
  for (auto it = first_.lower_bound({id, 0}); it != first_.end() && it->first.first == id;) it = first_.erase(it);
}

}  // namespace pstf::statusfx
