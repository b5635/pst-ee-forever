// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Contributors to the GemRB project <https://gemrb.org>
#include "modules/lighting/lighting_logic.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

Rgb TintRgb(uint32_t c) {
  return {float(c & 0xFF) / 255.0f, float((c >> 8) & 0xFF) / 255.0f, float((c >> 16) & 0xFF) / 255.0f};
}

float Nightness(uint32_t tod, float night_luma) {
  const float l = Luma(TintRgb(tod));
  if (night_luma >= 0.999f) return 0.0f;
  return std::max(0.0f, std::min(1.0f, (1.0f - l) / (1.0f - night_luma)));
}

Rgb TimeOfDayAmbient(const Rgb& day, float n, const DayNight& d) {
  n = std::max(0.0f, std::min(1.0f, n));
  const float dl = Luma(day);
  const Rgb moon{dl * d.night_level * 0.66f, dl * d.night_level * 0.84f, dl * d.night_level * 1.28f};
  Rgb a{day.r + (moon.r - day.r) * n, day.g + (moon.g - day.g) * n, day.b + (moon.b - day.b) * n};
  const float warm = d.dusk_warmth * 4.0f * n * (1.0f - n);  // peaks half way: dusk and dawn
  const float l = Luma(a);
  a.r += (l * 1.25f - a.r) * warm;
  a.g += (l * 0.92f - a.g) * warm;
  a.b += (l * 0.62f - a.b) * warm;
  return a;
}

float NightLightGain(float n, const DayNight& d) {
  return 1.0f + (d.night_light - 1.0f) * std::max(0.0f, std::min(1.0f, n));
}

Rgb UndoTint(const Rgb& c, const Rgb& t) {
  return {std::min(1.0f, c.r / std::max(t.r, 0.2f)), std::min(1.0f, c.g / std::max(t.g, 0.2f)),
          std::min(1.0f, c.b / std::max(t.b, 0.2f))};
}

float EffectCover(const Rgb& fx) {
  const float m = std::max(fx.r, std::max(fx.g, fx.b));
  const float t = std::max(0.0f, std::min(1.0f, (m - 0.01f) / 0.11f));
  return t * t * (3.0f - 2.0f * t);  // smoothstep(0.01, 0.12, m)
}
float UntintedCover(const Rgb& add, float alpha_sq) {
  return std::max(std::sqrt(std::max(0.0f, std::min(1.0f, alpha_sq))), EffectCover(add));
}
Rgb UndoTintFx(const Rgb& c, const Rgb& t, float k) {
  const Rgb u = UndoTint(c, t);
  k = std::max(0.0f, std::min(1.0f, k));
  return {u.r + (c.r - u.r) * k, u.g + (c.g - u.g) * k, u.b + (c.b - u.b) * k};
}

Rgb LightmapPool(const Rgb& lm, float mean_luma, float k) {
  const float over = Luma(lm) - mean_luma;
  if (over <= 0.0f || k <= 0.0f) return {};
  const float s = k * over / std::max(Luma(lm), 1e-4f);  // the pool's own colour, by how far it rises
  return {lm.r * s, lm.g * s, lm.b * s};
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

bool IsGlowBlend(uint32_t flags) { return (flags & 0x208u) != 0 && (flags & 0x20000000u) == 0; }

uint64_t PackResRef(const char* r) {
  uint64_t v = 0;
  for (int i = 0; i < 8 && r[i]; ++i) {
    char c = r[i];
    if (c >= 'a' && c <= 'z') c = char(c - 32);
    v |= uint64_t(uint8_t(c)) << (8 * i);
  }
  return v;
}

bool Emits(const std::vector<uint64_t>& names, const char* resref, uint32_t flags) {
  if (IsGlowBlend(flags)) return true;
  return resref && resref[0] && std::binary_search(names.begin(), names.end(), PackResRef(resref));
}

std::vector<uint64_t> ParseEmitList(const std::string& text) {
  std::vector<uint64_t> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    if (line.empty() || line[0] == '#' || line.size() > 8) continue;
    out.push_back(PackResRef(line.c_str()));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

uint64_t VfxKey(const char* resref, int cycle, int frame) {
  uint64_t name = 0;  // up to 8 upper-case chars packed: unique, no hashing needed
  for (int i = 0; i < 8 && resref[i]; ++i) {
    char c = resref[i];
    if (c >= 'a' && c <= 'z') c = char(c - 32);
    name |= uint64_t(uint8_t(c)) << (8 * i);
  }
  // 8 chars use all 64 bits; fold cycle/frame in with a mix that keeps (name, cycle, frame) distinct
  // in practice (cycle < 256, frame < 4096 in every PST BAM).
  const uint64_t cf = (uint64_t(uint8_t(cycle)) << 12) | uint64_t(uint16_t(frame) & 0xFFF);
  return name * 0x100000001B3ull ^ (cf * 0x9E3779B97F4A7C15ull);
}

const VfxFrame* VfxTable::Find(const char* resref, int cycle, int frame) const {
  const uint64_t k = VfxKey(resref, cycle, frame);
  auto it = std::lower_bound(rows.begin(), rows.end(), k,
                             [](const std::pair<uint64_t, VfxFrame>& r, uint64_t v) { return r.first < v; });
  return (it != rows.end() && it->first == k) ? &it->second : nullptr;
}

bool ParseVfxTable(const std::string& text, VfxTable* out) {
  out->rows.clear();
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    std::string name;
    int cycle = -1, frame = -1;
    VfxFrame f;
    if (!(in >> name >> cycle >> frame >> f.r >> f.g >> f.b >> f.strength >> f.cx >> f.cy >> f.bright)) continue;
    if (name.empty() || name.size() > 8 || cycle < 0 || frame < 0) continue;
    out->rows.emplace_back(VfxKey(name.c_str(), cycle, frame), f);
  }
  std::sort(out->rows.begin(), out->rows.end(),
            [](const std::pair<uint64_t, VfxFrame>& a, const std::pair<uint64_t, VfxFrame>& b) {
              return a.first < b.first;
            });
  return !out->rows.empty();
}

bool FxLight(const GlowDraw& d, float gain, BakedLight* out) {
  if (!d.vfx || d.vfx->strength <= 0.01f || d.w <= 0 || d.h <= 0) return false;
  const float k = std::min(0.9f, std::max(0.12f, d.vfx->strength * gain));
  BakedLight l;
  l.x = d.x + std::max(0.0f, std::min(d.w, d.vfx->cx));
  const float cy = d.y + std::max(0.0f, std::min(d.h, d.vfx->cy));
  l.y = d.y + d.h;                 // ground: the frame bottom
  l.z = std::max(0.0f, l.y - cy);  // drawn on the bright centroid
  l.radius = std::min(380.0f, std::max(60.0f, std::max(d.w, d.h) * 1.3f));
  l.r = d.vfx->r * k;
  l.g = d.vfx->g * k;
  l.b = d.vfx->b * k;
  l.strength = 1.0f;
  l.kind = Kind::Steady;
  *out = l;
  return true;
}

std::vector<BakedLight> MergeFxLights(std::vector<BakedLight> in, float dist, size_t budget) {
  auto power = [](const BakedLight& l) { return (l.r + l.g + l.b) * l.radius; };
  std::stable_sort(in.begin(), in.end(),
                   [&](const BakedLight& a, const BakedLight& b) { return power(a) > power(b); });
  std::vector<BakedLight> out;
  for (const BakedLight& l : in) {
    bool merged = false;
    for (BakedLight& o : out) {
      const float dx = o.x - l.x, dy = (o.y - o.z) - (l.y - l.z);
      if (dx * dx + dy * dy < dist * dist) {
        o.r = std::min(1.5f, o.r + l.r);
        o.g = std::min(1.5f, o.g + l.g);
        o.b = std::min(1.5f, o.b + l.b);
        o.radius = std::max(o.radius, l.radius);
        merged = true;
        break;
      }
    }
    if (!merged && out.size() < budget) out.push_back(l);
  }
  return out;
}

bool MatchPattern(const char* pat, const char* res) {
  int i = 0;
  for (; i < 8 && pat[i]; ++i) {
    char c = res[i];
    if (!c) return false;
    if (c >= 'a' && c <= 'z') c = char(c - 32);
    if (pat[i] != '?' && pat[i] != c) return false;
  }
  return i == 8 || res[i] == 0;
}

std::vector<HandLight> ParseHandLights(const std::string& text) {
  std::vector<HandLight> out;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    std::string pat, kind;
    HandLight h;
    float strength = 1.0f;
    if (!(in >> pat >> h.light.r >> h.light.g >> h.light.b >> h.light.radius >> h.light.z >> kind)) continue;
    in >> strength;
    for (std::string flag; in >> flag;) {
      if (flag == "noshadow") h.noshadow = true;
      if (flag.size() > 5 && flag.size() <= 13 && flag.compare(0, 5, "glow=") == 0)
        for (size_t i = 5; i < flag.size(); ++i) {
          char c = flag[i];
          if (c >= 'a' && c <= 'z') c = char(c - 32);
          h.glow[i - 5] = c;
        }
    }
    if (!ParseKind(kind, &h.light.kind)) continue;
    if (pat.size() > 5 && (pat.compare(0, 5, "anim:") == 0 || pat.compare(0, 5, "ANIM:") == 0)) {
      char* end = nullptr;
      const long id = std::strtol(pat.c_str() + 5, &end, 10);
      if (!end || *end || id < 0 || id > 0xFFFF) continue;
      h.anim = int(id);
    } else if (pat.empty() || pat.size() > 8) {
      continue;
    }
    for (size_t i = 0; h.anim < 0 && i < pat.size(); ++i) {
      char c = pat[i];
      if (c >= 'a' && c <= 'z') c = char(c - 32);
      h.pattern[i] = c;
    }
    h.light.r = Clamp01(h.light.r);
    h.light.g = Clamp01(h.light.g);
    h.light.b = Clamp01(h.light.b);
    h.light.radius = std::max(10.0f, std::min(600.0f, h.light.radius));
    h.light.strength = Clamp01(strength);
    out.push_back(h);
  }
  return out;
}

const HandLight* FindHandLight(const std::vector<HandLight>& t, const char* res) {
  if (!res || !res[0]) return nullptr;
  for (const HandLight& h : t)
    if (h.anim < 0 && MatchPattern(h.pattern, res)) return &h;
  return nullptr;
}

const HandLight* FindCreatureLight(const std::vector<HandLight>& t, int anim) {
  if (anim < 0) return nullptr;
  for (const HandLight& h : t)
    if (h.anim == anim) return &h;
  for (const HandLight& h : t)
    if (h.anim >= 0 && h.anim == (anim & 0xFF)) return &h;
  return nullptr;
}

BakedLight CreatureLightAt(const HandLight& h, float x, float y, double t_ms, float gain) {
  BakedLight l = h.light;
  l.x = x;
  l.y = y;
  l.owned = true;
  l.unblocked = h.noshadow;
  const float k = h.light.strength * gain * Flicker(h.light.kind, size_t(h.anim < 0 ? 0 : h.anim), t_ms);
  l.r *= k;
  l.g *= k;
  l.b *= k;
  return l;
}

BakedLight HandLightAt(const HandLight& h, const GlowDraw& d, size_t index, double t_ms, float gain) {
  BakedLight l = h.light;
  l.x = d.x + d.w * 0.5f;
  l.y = d.y + d.h;
  const float k = h.light.strength * gain * Flicker(h.light.kind, index, t_ms);
  l.r *= k;
  l.g *= k;
  l.b *= k;
  return l;
}

bool ColorEffectLight(int op, uint32_t p1, uint32_t p2, double age_ms, double t_ms, float gain, Rgb* out) {
  Rgb c{float((p1 >> 8) & 0xFF) / 255.0f, float((p1 >> 16) & 0xFF) / 255.0f, float((p1 >> 24) & 0xFF) / 255.0f};
  if (op == 353) c = {float(p1 & 0xFF) / 255.0f, float((p1 >> 8) & 0xFF) / 255.0f, float((p1 >> 16) & 0xFF) / 255.0f};
  float k;
  const int speed = int((p2 >> 16) & 0xFFFF);
  if (op == 8 || op == 52) {
    k = 1.0f;
  } else if (op == 9) {  // pulse: one cycle per 1.5 s (GemRB uses the same visual period)
    k = 0.55f + 0.45f * float(std::sin(t_ms * 2.0 * 3.14159265358979 / 1500.0));
  } else if (op == 61) {  // dissipate: from full colour to nothing; speed = colour steps per tick (15/s)
    const double fade_ms = std::max(300.0, std::min(4000.0, 255.0 / std::max(1, speed) * 66.7));
    k = float(std::max(0.0, 1.0 - age_ms / fade_ms));
  } else if (op == 353) {  // tint screen: rises and fades with the screen tint (about 1.5 s)
    k = age_ms < 1500.0 ? float(std::sin(3.14159265358979 * age_ms / 1500.0)) : 0.0f;
  } else {
    return false;
  }
  if (k <= 0.01f || Luma(c) <= 0.01f) return false;
  k *= gain;
  *out = {c.r * k, c.g * k, c.b * k};
  return true;
}

float SoftKnee(float l, float t, float k) {
  float soft = std::max(0.0f, std::min(2.0f * k, l - t + k));
  soft = soft * soft / (4.0f * k + 0.0001f);
  return std::max(soft, l - t) / std::max(l, 0.0001f);
}

float EmissiveWeight(float r, float g, float b) {
  const float m = std::max(r, std::max(g, b)), n = std::min(r, std::min(g, b));
  const float sat = m > 1e-4f ? (m - n) / m : 0.0f;
  const float t = Clamp01((m - 0.08f) / (0.6f - 0.08f));
  return t * t * (3.0f - 2.0f * t) * (0.55f + 0.45f * sat);
}

float PackSpriteG(float lm, float depth) {
  return std::floor(Clamp01(lm) * 15.0f + 0.5f) * 64.0f + std::max(0.0f, std::min(depth, 63.0f));
}

void UnpackSpriteG(float g, float* lm, float* depth) {
  const float level = std::floor(g / 64.0f);
  *lm = level / 15.0f;
  *depth = g - level * 64.0f;
}

float SpriteLight(float l, float ambient, float lm, float gain, float ao_factor) {
  return ambient * ao_factor + std::max(0.0f, l - ambient) / std::max(lm, 0.3f) * gain;
}

float AoAt(const SceneMap& m, float x, float y) {
  if (m.w <= 0 || m.h <= 0 || m.data.size() < size_t(m.w) * m.h) return 1.0f;
  const int texel = m.texel > 0 ? m.texel : 2;
  const int tx = std::max(0, std::min(m.w - 1, int(std::floor(x / float(texel)))));
  const int ty = std::max(0, std::min(m.h - 1, int(std::floor(y / float(texel)))));
  return float(m.data[size_t(ty) * m.w + tx]) / 255.0f;
}

float SpriteAoFactor(float ao, float strength) { return 1.0f - (1.0f - Clamp01(ao)) * Clamp01(strength); }

bool PaintedEmitter(const BakedLight& l, float core_scale, float* ex, float* ey, float* core) {
  if (l.kind == Kind::Pool) return false;
  *ex = l.x;
  *ey = l.y - l.z;
  *core = std::max(8.0f, std::min(48.0f, l.radius * core_scale));
  return true;
}

float PaintedGlowWeight(float r, float g, float b, float dist, float core) {
  if (core <= 0) return 0.0f;
  auto smooth = [](float e0, float e1, float v) {
    const float t = Clamp01((v - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
  };
  const float v = std::max(r, std::max(g, b));
  return smooth(0.70f, 0.92f, v) * (1.0f - smooth(0.5f * core, core, dist));
}

std::vector<AreaLight> ParseAreaLights(const std::string& text) {
  std::vector<AreaLight> out;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line[start] == '#') continue;
    std::istringstream in(line.substr(start));
    std::string wed, kind;
    AreaLight a;
    BakedLight& l = a.light;
    float radius = 0;
    int casts = 0;
    if (!(in >> wed >> l.x >> l.y >> l.z >> radius >> l.r >> l.g >> l.b >> l.strength >> kind >> casts)) continue;
    if (wed.empty() || wed.size() > 8 || !ParseKind(kind, &l.kind)) continue;
    for (size_t i = 0; i < wed.size(); ++i) a.wed[i] = char(std::toupper(static_cast<unsigned char>(wed[i])));
    l.radius = std::max(10.0f, std::min(600.0f, radius));  // the lights.txt clamps (ParseLights)
    l.r = Clamp01(l.r);
    l.g = Clamp01(l.g);
    l.b = Clamp01(l.b);
    l.strength = Clamp01(l.strength);
    l.casts_shadow = casts != 0;
    out.push_back(a);
  }
  return out;
}

size_t AddAreaLights(const std::vector<AreaLight>& table, const std::string& wed, std::vector<BakedLight>* out) {
  std::string up(wed);
  for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
  size_t n = 0;
  for (const AreaLight& a : table)
    if (up == a.wed) {
      out->push_back(a.light);
      ++n;
    }
  return n;
}

float ContactShadow(float dx, float dy, float rx, float ry, float k) {
  if (rx <= 0 || ry <= 0) return 0.0f;
  const float r = std::sqrt((dx / rx) * (dx / rx) + (dy / ry) * (dy / ry));
  if (r >= 1.0f) return 0.0f;
  float t = (r - 0.3f) / 0.7f;  // GLSL smoothstep(0.3, 1.0, r)
  t = std::max(0.0f, std::min(1.0f, t));
  return k * (1.0f - t * t * (3.0f - 2.0f * t));
}

float KeyShadowK(float strength, float key_strength) {
  return strength * std::max(0.7f, std::min(1.0f, key_strength));
}

void KeyShadowAxes(float dir_x, float dir_y, float k, float* sx, float* sy) {
  *sx = k * dir_x;
  float y = k * dir_y * 0.72f - 0.15f * (1.0f - std::fabs(dir_y));
  if (std::fabs(y) < 0.22f) y = y < 0 ? -0.22f : 0.22f;
  *sy = y;
}

bool KeyShadowSource(float mx, float my, float feet, float sx, float sy, float max_h, float* src_x, float* src_y,
                     float* h) {
  const float hh = (my - feet) / sy;
  if (hh <= 0 || hh > max_h) return false;
  *h = hh;
  *src_x = mx - sx * hh;
  *src_y = feet - hh;
  return true;
}

Rgb Saturate(const Rgb& c, float k) {
  const float l = Luma(c);
  Rgb o{l + (c.r - l) * k, l + (c.g - l) * k, l + (c.b - l) * k};
  o.r = std::max(0.0f, o.r);
  o.g = std::max(0.0f, o.g);
  o.b = std::max(0.0f, o.b);
  return o;
}

std::vector<GlowOverride> ParseGlowOverrides(const std::string& text) {
  std::vector<GlowOverride> out;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    std::string pat;
    int glow = 1, soften = 1;
    if (!(in >> pat >> glow >> soften) || pat.empty() || pat.size() > 8) continue;
    GlowOverride o;
    for (size_t i = 0; i < pat.size(); ++i) {
      char c = pat[i];
      if (c >= 'a' && c <= 'z') c = char(c - 32);
      o.pattern[i] = c;
    }
    o.glow = glow != 0;
    o.soften = soften != 0;
    out.push_back(o);
  }
  return out;
}

const GlowOverride* FindGlowOverride(const std::vector<GlowOverride>& t, const char* res) {
  if (!res || !res[0]) return nullptr;
  for (const GlowOverride& o : t)
    if (MatchPattern(o.pattern, res)) return &o;
  return nullptr;
}

float AtmosphereT(const Atmosphere& a, float h) {
  const float t = Clamp01((h - a.h0) / (a.h1 - a.h0));
  return t * t * (3.0f - 2.0f * t);
}

Rgb ApplyAtmosphere(const Atmosphere& a, const Rgb& c, float h, const Rgb& hz, float strength) {
  const float t = AtmosphereT(a, h) * strength;
  if (t <= 0) return c;
  const float l = Luma(c);
  // Only surfaces get the haze lift: the void (black, stored as a saturated 1024 backdrop) stays black.
  const float s = Clamp01((l - 0.02f) / (0.15f - 0.02f));
  const float surface = s * s * (3.0f - 2.0f * s);
  auto one = [&](float v, float haze) {
    v = v + (l - v) * a.desat * t;                              // toward grey
    v = (v - 0.5f) * (1.0f - a.contrast * t * surface) + 0.5f;  // flatter
    v = v + (haze - v) * a.haze * t * surface;                  // lifted toward the haze colour
    return Clamp01(v);
  };
  return {one(c.r, hz.r), one(c.g, hz.g), one(c.b, hz.b)};
}

Rgb HazeColour(const AreaScene& s, const Rgb& ambient) {
  if (!s.has_key) return ambient;
  const Rgb k{s.key_ambient[0], s.key_ambient[1], s.key_ambient[2]};
  const float kl = std::max(Luma(k), 0.001f), al = Luma(ambient);
  return {std::min(1.0f, k.r / kl * al), std::min(1.0f, k.g / kl * al), std::min(1.0f, k.b / kl * al)};
}

const char* DebugName(DebugMode m) {
  switch (m) {
    case DebugMode::Lit: return "lit";
    case DebugMode::LightOnly: return "light";
    case DebugMode::RoundTrip: return "roundtrip";
    case DebugMode::Falloff: return "falloff";
    case DebugMode::Off: return "off";
    case DebugMode::Emissive: return "emissive";
    case DebugMode::Sprites: return "sprites";
    case DebugMode::EmisRaw: return "emisraw";
    case DebugMode::Untinted: return "untinted";
  }
  return "?";
}

}  // namespace pstf::lighting
