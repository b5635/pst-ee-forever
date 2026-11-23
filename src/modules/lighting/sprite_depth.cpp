// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/lighting/sprite_depth.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/inflate.h"

namespace pstf::lighting {
namespace {

template <class T>
T Rd(const std::vector<uint8_t>& b, size_t o) {
  T v;
  std::memcpy(&v, b.data() + o, sizeof v);
  return v;
}

}  // namespace

bool ParseDep(std::vector<uint8_t> b, DepFile* out) {
  if (b.size() < 32 || std::memcmp(b.data(), "SPD1", 4) != 0 || Rd<uint16_t>(b, 4) != 1) return false;
  DepFile d;
  d.u16 = (Rd<uint16_t>(b, 6) & 1) != 0;
  const size_t nf = Rd<uint16_t>(b, 8), nc = Rd<uint16_t>(b, 10);
  const size_t of = Rd<uint32_t>(b, 12), oc = Rd<uint32_t>(b, 16), ol = Rd<uint32_t>(b, 20);
  if (of + nf * 20 > b.size() || oc + nc * 4 > b.size() || ol > b.size()) return false;
  size_t nlookup = 0;
  for (size_t i = 0; i < nc; ++i) {
    d.cycle_count.push_back(Rd<uint16_t>(b, oc + i * 4));
    d.cycle_first.push_back(Rd<uint16_t>(b, oc + i * 4 + 2));
    nlookup = std::max(nlookup, size_t(d.cycle_first.back()) + d.cycle_count.back());
  }
  if (ol + nlookup * 2 > b.size()) return false;
  for (size_t i = 0; i < nlookup; ++i) d.lookup.push_back(Rd<uint16_t>(b, ol + i * 2));
  for (size_t i = 0; i < nf; ++i) {
    const size_t o = of + i * 20;
    DepFrame f;
    f.w = Rd<uint16_t>(b, o);
    f.h = Rd<uint16_t>(b, o + 2);
    f.cx = Rd<int16_t>(b, o + 4);
    f.cy = Rd<int16_t>(b, o + 6);
    f.scale = Rd<float>(b, o + 8);
    f.offset = Rd<uint32_t>(b, o + 12);
    f.size = Rd<uint32_t>(b, o + 16);
    if (f.size && size_t(f.offset) + f.size > b.size()) return false;
    d.frames.push_back(f);
  }
  d.bytes = std::move(b);
  *out = std::move(d);
  return true;
}

int DepFile::FrameIndex(int cycle, int frame) const {
  if (cycle < 0 || size_t(cycle) >= cycle_count.size() || frame < 0 || frame >= cycle_count[size_t(cycle)])
    return -1;
  const size_t li = size_t(cycle_first[size_t(cycle)]) + size_t(frame);
  if (li >= lookup.size() || lookup[li] >= frames.size()) return -1;
  return lookup[li];
}

bool DepFile::Heights(int index, std::vector<float>* out) const {
  if (index < 0 || size_t(index) >= frames.size()) return false;
  const DepFrame& f = frames[size_t(index)];
  const size_t n = size_t(f.w) * f.h;
  out->assign(n, 0.0f);
  if (!f.size || !n) return true;
  std::vector<uint8_t> raw;
  const size_t bps = u16 ? 2 : 1;
  if (!ZlibInflate(bytes.data() + f.offset, f.size, &raw, n * bps)) return false;
  for (size_t i = 0; i < n; ++i) {
    const float s = u16 ? float(raw[i * 2] | (raw[i * 2 + 1] << 8)) : float(raw[i]);
    (*out)[i] = s * f.scale;
  }
  return true;
}

void SpriteNormal(const std::vector<float>& h, int w, int hgt, int x, int y, bool mirror, float strength,
                  float n[3]) {
  auto at = [&](int px, int py) {
    px = std::max(0, std::min(w - 1, px));
    py = std::max(0, std::min(hgt - 1, py));
    return h[size_t(py) * size_t(w) + size_t(px)];
  };
  float gx = (at(x + 1, y) - at(x - 1, y)) * 0.5f;      // height rising to the right
  const float gu = (at(x, y - 1) - at(x, y + 1)) * 0.5f;  // rising upward (rows run down)
  if (mirror) gx = -gx;
  // Surface toward the camera: p = (x, h, z) (y = toward the camera). Normal = (-dh/dx, 1, -dh/dz).
  float v[3] = {-gx * strength, 1.0f, -gu * strength};
  const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  for (int i = 0; i < 3; ++i) n[i] = v[i] / l;
}

}  // namespace pstf::lighting
