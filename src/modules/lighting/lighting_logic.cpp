// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Contributors to the GemRB project <https://gemrb.org>
#include "modules/lighting/lighting_logic.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace pstf::lighting {
namespace {

uint32_t U32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

float F32(const uint8_t* p) {
  const uint32_t bits = U32(p);
  float v = 0;
  std::memcpy(&v, &bits, sizeof v);
  return v;
}

float Clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

bool ParseKind(const std::string& s, Kind* k) {
  if (s == "fire") *k = Kind::Fire;
  else if (s == "magic") *k = Kind::Magic;
  else if (s == "steady") *k = Kind::Steady;
  else if (s == "pool") *k = Kind::Pool;
  else return false;
  return true;
}

constexpr float kT = 1.285714f;  // 1 / sin(elev) with sin(elev) = 0.75 + the GemRB calibration
}  // namespace

bool ParseLights(const std::string& text, AreaScene& out) {
  AreaScene parsed;
  bool saw_version = false;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line[start] == '#') continue;
    std::istringstream in(line.substr(start));
    std::string kw;
    if (!(in >> kw)) continue;
    if (kw == "version") {
      int v = 0;
      if (!(in >> v) || v != 1) return false;
      saw_version = true;
    } else if (kw == "size") {
      in >> parsed.size_w >> parsed.size_h;
    } else if (kw == "key") {
      float dx = 0, dy = 0, s = 0, ar = 1, ag = 1, ab = 1;
      if (!(in >> dx >> dy >> s >> ar >> ag >> ab)) continue;
      parsed.has_key = true;
      parsed.key_dir[0] = dx;
      parsed.key_dir[1] = dy;
      parsed.key_strength = Clamp01(s);
      parsed.key_ambient[0] = std::max(0.0f, ar);  // luma-normalised: a channel can exceed 1
      parsed.key_ambient[1] = std::max(0.0f, ag);
      parsed.key_ambient[2] = std::max(0.0f, ab);
    } else if (kw == "light") {
      BakedLight l;
      float radius = 0;
      std::string kind;
      int casts = 0;
      if (!(in >> l.x >> l.y >> l.z >> radius >> l.r >> l.g >> l.b >> l.strength >> kind >> casts)) continue;
      if (!ParseKind(kind, &l.kind)) continue;
      l.radius = std::max(10.0f, std::min(600.0f, radius));
      l.r = Clamp01(l.r);
      l.g = Clamp01(l.g);
      l.b = Clamp01(l.b);
      l.strength = Clamp01(l.strength);
      l.casts_shadow = casts != 0;
      parsed.lights.push_back(l);
    }
  }
  if (!saw_version) return false;
  out.lights = std::move(parsed.lights);
  out.has_key = parsed.has_key;
  std::copy(parsed.key_dir, parsed.key_dir + 2, out.key_dir);
  out.key_strength = parsed.key_strength;
  std::copy(parsed.key_ambient, parsed.key_ambient + 3, out.key_ambient);
  out.size_w = parsed.size_w;
  out.size_h = parsed.size_h;
  return true;
}

bool ReadMap(const uint8_t* b, size_t n, const char magic[4], int bpt, SceneMap& out) {
  out = SceneMap();
  if (bpt <= 0 || !b || n < 32 || std::memcmp(b, magic, 4) != 0) return false;
  const uint32_t version = U32(b + 4), w = U32(b + 8), h = U32(b + 12), texel = U32(b + 16);
  const float unit = F32(b + 20);
  if (version != 1 || w == 0 || h == 0 || w > 65536 || h > 65536) return false;
  if (uint64_t(n) != 32ull + uint64_t(w) * h * uint64_t(bpt)) return false;
  out.w = int(w);
  out.h = int(h);
  out.texel = int(texel);
  out.unit = unit;
  out.data.assign(b + 32, b + n);
  return true;
}

float HeightAt(const SceneMap& m, float x, float y) {
  if (m.w <= 0 || m.h <= 0 || m.data.size() < size_t(m.w) * m.h * 2) return 0.0f;
  const int texel = m.texel > 0 ? m.texel : 2;
  const int tx = std::max(0, std::min(m.w - 1, int(std::floor(x / float(texel)))));
  const int ty = std::max(0, std::min(m.h - 1, int(std::floor(y / float(texel)))));
  const uint8_t* p = m.data.data() + (size_t(ty) * m.w + tx) * 2;
  return float(uint16_t(p[0] | (p[1] << 8))) * m.unit;
}

std::vector<float> HeightFloats(const SceneMap& m) {
  std::vector<float> f;
  if (m.data.size() < size_t(m.w) * m.h * 2) return f;
  f.resize(size_t(m.w) * m.h);
  for (size_t i = 0; i < f.size(); ++i)
    f[i] = float(uint16_t(m.data[2 * i] | (m.data[2 * i + 1] << 8))) * m.unit;
  return f;
}

float Luma(const Rgb& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

Rgb Ambient(uint16_t type, float strength, const AreaScene* scene, bool use_key) {
  float amb = (type & 0x20) ? 0.66f : (type & 0x01) ? 0.86f : 0.76f;  // dungeon / outdoor / other
  strength = std::max(0.0f, std::min(2.0f, strength));
  amb = 1.0f - (1.0f - amb) * strength;
  Rgb a{amb * 0.97f, amb * 0.98f, std::min(1.0f, amb * 1.05f)};
  if (use_key && scene && scene->has_key) {
    const float l = Luma(a);
    a.r = a.r * 0.5f + 0.5f * l * scene->key_ambient[0];
    a.g = a.g * 0.5f + 0.5f * l * scene->key_ambient[1];
    a.b = a.b * 0.5f + 0.5f * l * scene->key_ambient[2];
  }
  return a;
}

float Flicker(Kind kind, size_t index, double t_ms) {
  const double t = t_ms, i = double(index);
  if (kind == Kind::Fire)
    return float(0.88 + 0.12 * (0.5 * std::sin(t * 0.011 + i) + 0.3 * std::sin(t * 0.023 + i * 2.1) +
                                0.2 * std::sin(t * 0.047 + i * 3.7)));
  if (kind == Kind::Magic) return float(0.92 + 0.08 * std::sin(t * 0.0021 + i));
  return 1.0f;
}

float LightScale(const BakedLight& l, float gain, float flicker, float ambient_luma) {
  return l.strength * gain * flicker * ((1.0f - ambient_luma) + 0.1f);
}

float Falloff(float px, float pg, float ph, float lx, float ly, float lz, float radius) {
  const float dx = lx - px, dy = (ly - pg) / 0.72f, dz = lz - ph;
  const float dist = std::sqrt(dx * dx + dy * dy + dz * dz) / radius;
  if (dist >= 1.0f) return 0.0f;
  const float att = 1.0f - dist * dist;
  return att * att;
}

float Occlusion(const SceneMap& hm, float px, float pg, float ph, float lx, float ly, float lz, int steps,
                float bias, float thickness, float jitter) {
  if (steps <= 0) return 1.0f;
  float hits = 0;
  const float len = std::sqrt((lx - px) * (lx - px) + (ly - pg) * (ly - pg) + (lz - ph) * (lz - ph));
  for (int i = 0; i < steps; ++i) {
    const float s = (float(i) + jitter) / float(steps);
    if (s * len < 4.0f) continue;
    const float qx = px + (lx - px) * s, qy = pg + (ly - pg) * s, qz = ph + (lz - ph) * s;
    const float sx = qx, sy = qy - qz;  // where Q is drawn
    const float hs = HeightAt(hm, sx, sy);
    const float dq = -qy - qz * kT;
    const float ds = -(sy + hs) - hs * kT;
    const float diff = dq - ds;
    if (diff > bias && diff < thickness) hits += 1.0f;
  }
  return 1.0f - std::max(0.0f, std::min(1.0f, hits / 2.0f));
}

float Jitter(float fx, float fy) {
  const float d = fx * 0.06711056f + fy * 0.00583715f;
  const float a = 52.9829189f * (d - std::floor(d));
  return a - std::floor(a);
}

bool DebugFalloffAt(const ViewMap& m, float to_phys, int bx, int by, const BakedLight& l, const SceneMap* hm,
                    int steps, FalloffPixel* out) {
  const float fx = float(bx) + 0.5f, fy = float(by) + 0.5f;
  float mx, my;
  ToArea(m, fx * to_phys, fy * to_phys, &mx, &my);
  const float ground = my + (hm ? HeightAt(*hm, mx, my) : 0.0f);
  const float h = std::max(ground - my, 0.0f);
  const float att = Falloff(mx, ground, h, l.x, l.y, l.z, l.radius);
  const float dx = l.x - mx, dy = (l.y - ground) / 0.72f, dz = l.z - h;
  if (std::sqrt(dx * dx + dy * dy + dz * dz) / l.radius >= 1.0f) return false;
  const float occ = hm ? Occlusion(*hm, mx, ground, h, l.x, l.y, l.z, steps, 2.0f, 48.0f, Jitter(fx, fy)) : 1.0f;
  out->red = h > 2.0f ? 0.5f : 0.0f;
  out->green = att * occ * 0.5f;
  return true;
}

float Lambert(const float n[3], const float l[3]) {
  return Clamp01((n[0] * l[0] + n[1] * l[1] + n[2] * l[2] + 0.4f) / 1.4f);
}

float NormalRatio(const float n[3], const float l[3]) {
  const float up[3] = {0, 0, 1};
  return std::max(0.0f, std::min(2.5f, Lambert(n, l) / std::max(Lambert(up, l), 0.25f)));
}

Rgb Composite(const Rgb& c, const Rgb& l, float max_light, float keep) {
  auto shoulder = [&](float v) {
    const float over = std::max(v - 1.0f, 0.0f);
    return std::min(v, 1.0f) + over / (1.0f + over / std::max(max_light - 1.0f, 0.001f));
  };
  Rgb lit{c.r * shoulder(l.r), c.g * shoulder(l.g), c.b * shoulder(l.b)};
  const float m = std::max(c.r, std::max(c.g, c.b));
  const float t = Clamp01((m - 0.72f) / (1.0f - 0.72f));
  const float hi = t * t * (3.0f - 2.0f * t) * keep;  // smoothstep(0.72, 1, max) * keep
  lit.r = std::min(1.0f, lit.r + (std::max(lit.r, c.r) - lit.r) * hi);
  lit.g = std::min(1.0f, lit.g + (std::max(lit.g, c.g) - lit.g) * hi);
  lit.b = std::min(1.0f, lit.b + (std::max(lit.b, c.b) - lit.b) * hi);
  return lit;
}

ViewMap MakeViewMap(int vp_w, int vp_h, const float st[4], float off_x, float off_y) {
  // phys -> ndc: ndc = phys * 2 / vp - 1; ndc -> draw: (ndc - st.zw) / st.xy; draw -> area: + off.
  ViewMap m;
  m.ax = 2.0f / (float(vp_w) * st[0]);
  m.ay = 2.0f / (float(vp_h) * st[1]);
  m.bx = (-1.0f - st[2]) / st[0] + off_x;
  m.by = (-1.0f - st[3]) / st[1] + off_y;
  return m;
}

void ToArea(const ViewMap& m, float px, float py, float* ax, float* ay) {
  *ax = px * m.ax + m.bx;
  *ay = py * m.ay + m.by;
}

void ToPhys(const ViewMap& m, float ax, float ay, float* px, float* py) {
  *px = (ax - m.bx) / m.ax;
  *py = (ay - m.by) / m.ay;
}

Rect LightFootprint(const BakedLight& l) {
  return {l.x - l.radius, l.y - l.radius * 0.72f - l.radius, l.x + l.radius, l.y + l.radius * 0.72f};
}

bool FootprintInBuffer(const ViewMap& m, const BakedLight& l, float to_phys, int bw, int bh, Rect* out) {
  const Rect a = LightFootprint(l);
  float x0, y0, x1, y1;
  ToPhys(m, a.x0, a.y0, &x0, &y0);
  ToPhys(m, a.x1, a.y1, &x1, &y1);
  Rect r{std::min(x0, x1) / to_phys, std::min(y0, y1) / to_phys, std::max(x0, x1) / to_phys,
         std::max(y0, y1) / to_phys};
  if (r.x1 < 0 || r.y1 < 0 || r.x0 > float(bw) || r.y0 > float(bh)) return false;
  *out = r;
  return true;
}

const char* DebugName(DebugMode m) {
  switch (m) {
    case DebugMode::Lit: return "lit";
    case DebugMode::LightOnly: return "light";
    case DebugMode::RoundTrip: return "roundtrip";
    case DebugMode::Falloff: return "falloff";
    case DebugMode::Off: return "off";
  }
  return "?";
}

}  // namespace pstf::lighting
