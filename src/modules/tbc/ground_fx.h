// SPDX-License-Identifier: GPL-2.0-or-later
// Maneuver ground effects as smoke/dust puffs (pure, unit-tested): each effect is a handful of soft,
// filled, noisy blobs (engine/soft_draw.h DrawSoftPuffs) that drift, grow and fade. Area px, y down;
// the floor is foreshortened (ry = 0.75 rx for a flat puff, rounder while it rises).
//
//   RunBurst   Run starts: tan dust kicked out around the feet (14 puffs, 750 ms).
//   RunTrail   while running: one little dust cloud left behind every ~110 ms (3 puffs, 650 ms).
//   Smoke      Withdraw: a grey smoke burst out from the feet that billows up (12 puffs, 1000 ms).
//   SmokeTrail while withdrawing: faint grey wisps behind the walker (2 puffs, 800 ms).
//   KnockDust  knocked down: dust thrown up when the body lands (after 280 ms; 14 puffs, 850 ms).
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace pstf::tbc {

enum class PuffFx : uint8_t { RunBurst, RunTrail, Smoke, SmokeTrail, KnockDust };

struct Puff {
  float x, y;     // centre, area px
  float rx, ry;   // radii, area px
  uint32_t bgr;   // 0x00BBGGRR
  float alpha;    // 0..1 at the centre
  float seed;     // noise offset (each puff its own shape)
};

// Lifetime in ms after which an effect draws nothing more.
inline double PuffFxLifeMs(PuffFx k) {
  switch (k) {
    case PuffFx::RunBurst: return 750.0;
    case PuffFx::RunTrail: return 650.0;
    case PuffFx::Smoke: return 1000.0;
    case PuffFx::SmokeTrail: return 800.0;
    case PuffFx::KnockDust: return 280.0 + 850.0;
  }
  return 0.0;
}

namespace puff_detail {
// Deterministic 0..1 from the effect's seed and the puff index.
inline double Hash01(uint32_t seed, int i, int salt) {
  uint32_t h = seed * 0x9E3779B1u ^ static_cast<uint32_t>(i * 0x85EBCA77u) ^ static_cast<uint32_t>(salt * 0xC2B2AE3Du);
  h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
  return (h & 0xFFFFFF) / 16777215.0;
}
inline uint32_t Shade(uint32_t bgr, double k) {
  const auto ch = [&](int s) {
    const double v = ((bgr >> s) & 0xFF) * k;
    return static_cast<uint32_t>(v < 0 ? 0 : v > 255 ? 255 : v) << s;
  };
  return ch(0) | ch(8) | ch(16);
}
// Fades in quickly (no pop), out slowly.
inline double Envelope(double u) {
  if (u <= 0 || u >= 1) return 0;
  const double in = u < 0.12 ? u / 0.12 : 1.0;
  const double out = 1.0 - u;
  return in * out * out * (1.6 - 0.6 * out);
}
// One outward burst: n puffs at random angles, ease-out drift to `reach`, rising `rise`, growing r0 -> r1.
inline void Burst(uint32_t seed, double u, int x, int y, int n, double reach, double rise, double r0, double r1,
                  uint32_t bgr, double alpha, std::vector<Puff>* out) {
  if (u <= 0 || u >= 1) return;
  const double ease = 1.0 - (1.0 - u) * (1.0 - u) * (1.0 - u);
  for (int i = 0; i < n; ++i) {
    const double a = 6.283185307 * (i + 0.6 * Hash01(seed, i, 1)) / n;
    const double sp = 0.55 + 0.45 * Hash01(seed, i, 2);
    const double d = reach * sp * ease;
    const double lift = rise * (0.6 + 0.4 * Hash01(seed, i, 3)) * u;
    const double r = r0 + (r1 - r0) * std::sqrt(u) * (0.8 + 0.4 * Hash01(seed, i, 4));
    const double env = Envelope(u + 0.06 * Hash01(seed, i, 5));
    Puff p;
    p.x = static_cast<float>(x + d * std::cos(a));
    p.y = static_cast<float>(y + d * 0.75 * std::sin(a) - lift);
    p.rx = static_cast<float>(r);
    p.ry = static_cast<float>(r * (0.75 + 0.2 * u));  // flat on the floor, rounder as it rises
    p.bgr = Shade(bgr, 0.85 + 0.3 * Hash01(seed, i, 6));
    p.alpha = static_cast<float>(alpha * env);
    p.seed = static_cast<float>(Hash01(seed, i, 7) * 97.0);
    if (p.alpha > 0.003f) out->push_back(p);
  }
}
}  // namespace puff_detail

// The puffs of one effect `ms` after it started, at feet (x, y). Appends to out; nothing outside its life.
inline void PuffsFor(PuffFx k, uint32_t seed, double ms, int x, int y, std::vector<Puff>* out) {
  using namespace puff_detail;
  if (ms < 0 || ms >= PuffFxLifeMs(k)) return;
  switch (k) {
    case PuffFx::RunBurst:
      Burst(seed, ms / 750.0, x, y, 14, 40.0, 12.0, 8.0, 22.0, 0x8AAAC4, 0.70, out);
      break;
    case PuffFx::RunTrail:
      Burst(seed, ms / 650.0, x, y, 3, 8.0, 12.0, 6.0, 15.0, 0x8AAAC4, 0.55, out);
      break;
    case PuffFx::Smoke:
      Burst(seed, ms / 1000.0, x, y, 12, 46.0, 30.0, 12.0, 30.0, 0xC8C8C8, 0.80, out);
      Burst(seed ^ 0x5bd1e995u, ms / 1000.0, x, y - 6, 5, 14.0, 50.0, 10.0, 24.0, 0xDADADA, 0.60, out);  // the column up
      break;
    case PuffFx::SmokeTrail:
      Burst(seed, ms / 800.0, x, y, 2, 7.0, 18.0, 7.0, 18.0, 0xC0C0C0, 0.45, out);
      break;
    case PuffFx::KnockDust:
      Burst(seed, (ms - 280.0) / 850.0, x, y, 14, 38.0, 10.0, 9.0, 24.0, 0x8AAAC4, 0.75, out);
      break;
  }
}

}  // namespace pstf::tbc
