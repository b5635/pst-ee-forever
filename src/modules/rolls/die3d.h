// SPDX-License-Identifier: GPL-2.0-or-later
// The roll panel's real-time d20: geometry, face numbers and its motion over the panel's timeline, pure
// (no GL, no engine). The renderer (die3d_gl.cpp) only projects what this gives it.
//
// Conventions follow tools/make_dice_bams.py (the BAM fallback looks the same): an icosahedron of unit
// circumradius, faces wound outward, opposite faces sum to 21. A pose R is a 3x3 rotation whose rows are
// screen x, y, z in model space: view = R * v, screen y up = +y, the viewer at +z (orthographic).
//
// Motion (ms after the Roll click, d20.h RollTiming): two spins about random axes slow down to rest at
// kLand on the natural face (no jump), the die rocks once on landing, turns about its vertical axis at
// kTotal while its front face changes to the total, and pops at the result. Before the click it sways.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "modules/rolls/d20.h"

namespace pstf::rolls::die3d {

struct V3 {
  double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline double Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double Len(V3 a) { return std::sqrt(Dot(a, a)); }
inline V3 Norm(V3 a) { return a * (1.0 / Len(a)); }

struct M3 {
  V3 r[3];  // rows
};
inline V3 Mul(const M3& m, V3 v) { return {Dot(m.r[0], v), Dot(m.r[1], v), Dot(m.r[2], v)}; }
inline M3 Mul(const M3& a, const M3& b) {
  M3 o;
  const V3 c0{b.r[0].x, b.r[1].x, b.r[2].x}, c1{b.r[0].y, b.r[1].y, b.r[2].y}, c2{b.r[0].z, b.r[1].z, b.r[2].z};
  for (int i = 0; i < 3; ++i) o.r[i] = {Dot(a.r[i], c0), Dot(a.r[i], c1), Dot(a.r[i], c2)};
  return o;
}
inline M3 Identity() { return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; }
// Rodrigues: rotation by `ang` about unit axis `a`.
inline M3 AxisAngle(V3 a, double ang) {
  const double c = std::cos(ang), s = std::sin(ang), t = 1 - c;
  return {{{t * a.x * a.x + c, t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y},
           {t * a.x * a.y + s * a.z, t * a.y * a.y + c, t * a.y * a.z - s * a.x},
           {t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c}}};
}
// make_dice_bams.py tilt(ax, ay) = Ry(ay) * Rx(ax).
inline M3 Tilt(double ax, double ay) { return Mul(AxisAngle({0, 1, 0}, ay), AxisAngle({1, 0, 0}, ax)); }
// Angle between two poses (radians): how far apart they are.
inline double PoseDistance(const M3& a, const M3& b) {
  double tr = 0;  // trace(a * b^T)
  for (int i = 0; i < 3; ++i) tr += Dot(a.r[i], b.r[i]);
  double c = (tr - 1) / 2;
  c = c > 1 ? 1 : (c < -1 ? -1 : c);
  return std::acos(c);
}

struct Mesh {
  std::array<V3, 12> v;
  std::array<std::array<int, 3>, 20> f;  // wound outward
  std::array<int, 20> num;               // printed number of each face
};

inline const Mesh& D20() {
  static const Mesh m = [] {
    Mesh m;
    const double p = (1 + std::sqrt(5.0)) / 2;
    const double raw[12][3] = {{0, 1, p},  {0, -1, p},  {0, 1, -p}, {0, -1, -p}, {1, p, 0},  {-1, p, 0},
                               {1, -p, 0}, {-1, -p, 0}, {p, 0, 1},  {-p, 0, 1},  {p, 0, -1}, {-p, 0, -1}};
    const double n0 = std::sqrt(1 + p * p);
    for (int i = 0; i < 12; ++i) m.v[i] = {raw[i][0] / n0, raw[i][1] / n0, raw[i][2] / n0};
    int nf = 0;
    auto edge = [&](int a, int b) { return std::fabs(Len(m.v[a] - m.v[b]) - 1.0515) < 0.01; };
    for (int i = 0; i < 12; ++i)
      for (int j = i + 1; j < 12; ++j)
        for (int k = j + 1; k < 12; ++k) {
          if (!edge(i, j) || !edge(j, k) || !edge(i, k)) continue;
          const V3 c = (m.v[i] + m.v[j] + m.v[k]) * (1.0 / 3);
          const V3 n = Cross(m.v[j] - m.v[i], m.v[k] - m.v[i]);
          m.f[nf++] = Dot(n, c) > 0 ? std::array<int, 3>{i, j, k} : std::array<int, 3>{i, k, j};
        }
    // numbers as make_dice_bams.py _numbers(): faces by (-z, y, x) of their centre take 1..10, each
    // one's opposite face 21 - n
    std::array<V3, 20> cent;
    for (int i = 0; i < 20; ++i) cent[i] = (m.v[m.f[i][0]] + m.v[m.f[i][1]] + m.v[m.f[i][2]]) * (1.0 / 3);
    std::array<int, 20> order;
    for (int i = 0; i < 20; ++i) order[i] = i;
    for (int a = 1; a < 20; ++a)  // insertion sort: stable like Python's sorted
      for (int b = a; b > 0; --b) {
        const V3 &x = cent[order[b - 1]], &y = cent[order[b]];
        const bool less = (-y.z < -x.z) || (-y.z == -x.z && (y.y < x.y || (y.y == x.y && y.x < x.x)));
        if (!less) break;
        std::swap(order[b - 1], order[b]);
      }
    m.num.fill(0);
    int nxt = 1;
    for (int i : order) {
      if (m.num[i]) continue;
      int opp = 0;
      double best = 1e9;
      for (int j = 0; j < 20; ++j) {
        const double d = Len(cent[j] + cent[i]);
        if (d < best) best = d, opp = j;
      }
      m.num[i] = nxt;
      m.num[opp] = 21 - nxt;
      if (++nxt > 10) break;
    }
    return m;
  }();
  return m;
}

inline V3 FaceCentre(int f) {
  const Mesh& m = D20();
  return (m.v[m.f[f][0]] + m.v[m.f[f][1]] + m.v[m.f[f][2]]) * (1.0 / 3);
}
inline int FaceOf(int number) {
  const Mesh& m = D20();
  for (int i = 0; i < 20; ++i)
    if (m.num[i] == number) return i;
  return -1;
}

// The pose that turns face f to the viewer with its first vertex straight up (make_dice_bams rot_to_front).
inline M3 RotToFront(int f) {
  const Mesh& m = D20();
  const V3 c = FaceCentre(f);
  const V3 z = Norm(c);
  V3 top = m.v[m.f[f][0]] - c;
  top = top - z * Dot(top, z);
  const V3 y = Norm(top);
  const V3 x = Cross(y, z);
  return {{x, y, z}};
}
// The resting pose showing `number` (1..20; anything else rests on the 20 face, as the BAM does).
inline M3 RestPose(int number) {
  const int f = (number >= 1 && number <= 20) ? FaceOf(number) : FaceOf(20);
  return Mul(Tilt(0.12, -0.08), RotToFront(f));
}
// Face nearest the viewer in pose R.
inline int FrontFace(const M3& R) {
  int best = 0;
  double bz = -2;
  for (int i = 0; i < 20; ++i) {
    const double z = Mul(R, FaceCentre(i)).z;
    if (z > bz) bz = z, best = i;
  }
  return best;
}

// One throw's random choices (from a seed, so a test can replay it).
struct Throw {
  V3 axis1, axis2;
  double spin1, spin2;  // radians still to turn at the click
};
inline Throw MakeThrow(uint32_t seed) {
  auto next = [&seed]() {  // xorshift32 -> [0, 1)
    seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
    return double(seed) / 4294967296.0;
  };
  if (!seed) seed = 0x9E3779B9u;
  auto axis = [&]() {
    const double z = next() * 2 - 1, a = next() * 6.283185307179586, r = std::sqrt(1 - z * z);
    return V3{r * std::cos(a), r * std::sin(a), z};
  };
  Throw t;
  t.axis1 = axis();
  t.axis2 = axis();
  t.spin1 = 6.283185307179586 * (2.2 + next() * 0.9);
  t.spin2 = 6.283185307179586 * (0.8 + next() * 0.6);
  return t;
}

inline double Smooth(double u) { return u <= 0 ? 0 : (u >= 1 ? 1 : u * u * (3 - 2 * u)); }
inline double Bump(double ms, double at, double len) {  // 0 -> 1 -> 0 over [at, at + len)
  if (ms < at || ms >= at + len) return 0;
  return std::sin((ms - at) / len * 3.141592653589793);
}

constexpr double kRockMs = 420, kTurnMs = 320;

struct Frame {
  M3 pose;
  double scale = 1;     // 1 = the BAM's 150 px die (radius 52)
  int front_face = 0;   // face whose number is replaced by `front_text`
  int front_text = 20;  // number printed there (natural, then the total)
};

// The die at `ms` after the click (ms < 0: not thrown; `now_ms` drives the idle sway). nat 1..20.
inline Frame At(double ms, double now_ms, int nat, int total, bool ok, const Throw& th) {
  Frame fr;
  if (ms < 0) {
    const M3 rest = RestPose(20);
    const M3 sway = Mul(AxisAngle({0, 1, 0}, 0.10 * std::sin(now_ms * 0.0016)),
                        AxisAngle({1, 0, 0}, 0.07 * std::sin(now_ms * 0.0011 + 1.3)));
    fr.pose = Mul(sway, rest);
    fr.front_face = FaceOf(20);
    fr.front_text = 20;
    return fr;
  }
  const M3 rest = RestPose(nat);
  fr.front_face = FaceOf(nat);
  fr.front_text = nat;
  M3 pose = rest;
  const double land = RollTiming::kLand;
  if (ms < land) {
    // the turn left falls off as (1 - u)^2.4: speed decays smoothly to zero at the landing
    const double left = std::pow(1 - ms / land, 2.4);
    pose = Mul(rest, Mul(AxisAngle(th.axis1, th.spin1 * left), AxisAngle(th.axis2, th.spin2 * left)));
  } else if (ms < land + kRockMs) {
    // one rock onto the face after the hit (screen x axis, damped)
    const double u = (ms - land) / kRockMs;
    const double a = 0.22 * std::sin(u * 6.283185307179586) * (1 - u) * (1 - u);
    pose = Mul(AxisAngle({1, 0, 0}, a), rest);
  }
  if (total != nat && ms >= RollTiming::kTotal - kTurnMs / 2) {
    // a full turn about the vertical axis; the front face shows the total from the half turn on
    const double u = Smooth((ms - (RollTiming::kTotal - kTurnMs / 2)) / kTurnMs);
    pose = Mul(AxisAngle({0, 1, 0}, u * 6.283185307179586), pose);
    if (u >= 0.5) fr.front_text = total;
  }
  fr.pose = pose;
  // scale pulses as the BAM's (1.12 at the landing and the total, 1.25 then 1.12 at the pop)
  const double pop = RollTiming::Pop(ok);
  fr.scale = 1 + 0.12 * Bump(ms, land - 40, 220) + 0.12 * Bump(ms, RollTiming::kTotal, 260) +
             0.25 * Bump(ms, pop, RollTiming::kPop * 0.8);
  return fr;
}

// Number atlas (tools/make_dice_bams.py d20_numbers.bin): cells kCell px in kCols columns; big (front
// face) text n = -9..40 at cell n + 9, the small face numbers 1..20 at cell 49 + n. In a cell the face
// triangle is apex (T/2, 0), base right (T, 0.866 T), base left (0, 0.866 T), y down.
constexpr int kCell = 128, kCols = 10, kRows = 7;
inline int AtlasCell(int n, bool big) {
  if (big) return (n < -9 ? -9 : (n > 40 ? 40 : n)) + 9;
  return 49 + (n < 1 ? 1 : (n > 20 ? 20 : n));
}

}  // namespace pstf::rolls::die3d
