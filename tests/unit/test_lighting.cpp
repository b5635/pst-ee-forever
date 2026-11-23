// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Contributors to the GemRB project <https://gemrb.org> (parser cases from
// gemrb/tests/core/Test_AreaSceneData.cpp)
#include <cmath>
#include <cstring>
#include <vector>

#include "engine/symbols.h"
#include "core/patch_math.h"
#include "mini_test.h"
#include "modules/lighting/lighting_logic.h"

using namespace pstf;
using namespace pstf::lighting;

namespace {
bool Near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }

void U32(std::vector<uint8_t>& o, uint32_t v) {
  for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i)));
}
std::vector<uint8_t> MapBytes(const char magic[4], uint32_t w, uint32_t h, uint32_t texel, float unit,
                              const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> o(magic, magic + 4);
  U32(o, 1);
  U32(o, w);
  U32(o, h);
  U32(o, texel);
  uint32_t bits;
  std::memcpy(&bits, &unit, 4);
  U32(o, bits);
  U32(o, 0);
  U32(o, 0);
  o.insert(o.end(), payload.begin(), payload.end());
  return o;
}
SceneMap Uniform(int w, int h, uint16_t v, float unit) {
  std::vector<uint8_t> p(size_t(w) * h * 2);
  for (size_t i = 0; i < p.size(); i += 2) {
    p[i] = uint8_t(v);
    p[i + 1] = uint8_t(v >> 8);
  }
  const auto b = MapBytes("GLH1", w, h, 2, unit, p);
  SceneMap m;
  ReadMap(b.data(), b.size(), "GLH1", 2, m);
  return m;
}
}  // namespace

TEST_CASE("lighting: manifest call sites reach the functions they name") {
  struct {
    uint32_t rva;
    const uint8_t* b;
    uint64_t want;
  } calls[] = {{sym::kRvaFloatMsgCall, sym::kFloatMsgCallBytes, sym::kVaFloatMsgRender},
               {sym::kRvaTransitionsCall, sym::kTransitionsCallBytes, sym::kVaRenderTransitions},
               {sym::kRvaFogCall, sym::kFogCallBytes, sym::kVaRenderFog},
               {sym::kRvaPostRenderCall, sym::kPostRenderCallBytes, sym::kVaPostRender},
               {sym::kRvaBeginScaledFlushCall, sym::kBeginScaledFlushCallBytes, sym::kImageBase + sym::kRvaDrawFlush},
               {sym::kRvaEndScaledFlushCall, sym::kEndScaledFlushCallBytes, sym::kImageBase + sym::kRvaDrawFlush}};
  for (const auto& c : calls) {
    auto t = Rel32Target(sym::kImageBase + c.rva, c.b);
    CHECK(t && *t == c.want);
  }
  // movups [rip+disp], xmm2 at 1404B4394 -> 14601AD88; lea rax, [rip+disp] at 1404B4A89 -> 1437FAE20
  int32_t d = 0;
  std::memcpy(&d, sym::kWorldStStoreBytes + 3, 4);
  CHECK(sym::kImageBase + sym::kRvaWorldStStore + 7 + d == sym::kVaWorldSt);
  std::memcpy(&d, sym::kProgramTableLeaBytes + 3, 4);
  CHECK(sym::kImageBase + sym::kRvaProgramTableLea + 7 + d == sym::kVaProgramTable);
  CHECK(sym::kCellFrameStoreBytes[3] == 0x18 && sym::kCellSeqStoreBytes[3] == 0x1A);  // +0x118 / +0x11A
  CHECK(sym::kAreaTypeReadBytes[4] == sym::kOffAreaType);
}

TEST_CASE("lighting: lights.txt parse (GemRB ParseValid / ParseKeyLine)") {
  const char* text =
      "# GemRB baked scene lights\nversion 1\nsize 100 80\nunknown 1 2 3\n"
      "light 10.0000 20.0000 30.0000 120.0000 1.0000 0.4000 0.1000 0.8000 fire 1\n"
      "light 50.5000 60.2500 12.0000 80.0000 0.2000 0.3000 1.0000 0.4000 pool 0\n";
  AreaScene s;
  CHECK(ParseLights(text, s));
  CHECK(s.lights.size() == 2);
  CHECK(!s.has_key);
  CHECK(s.size_w == 100 && s.size_h == 80);
  CHECK(s.lights[0].x == 10.0f && s.lights[0].y == 20.0f && s.lights[0].z == 30.0f);
  CHECK(s.lights[0].radius == 120.0f && s.lights[0].r == 1.0f && s.lights[0].g == 0.4f && s.lights[0].b == 0.1f);
  CHECK(s.lights[0].strength == 0.8f && s.lights[0].kind == Kind::Fire && s.lights[0].casts_shadow);
  CHECK(s.lights[1].kind == Kind::Pool && !s.lights[1].casts_shadow && s.lights[1].x == 50.5f);

  AreaScene k;
  CHECK(ParseLights("version 1\nkey -0.2500 0.9682 0.5000 1.0000 0.9000 0.8000\n"
                    "light 10 20 30 120 1 0.4 0.1 0.8 fire 1\n",
                    k));
  CHECK(k.has_key && k.key_dir[0] == -0.25f && k.key_dir[1] == 0.9682f && k.key_strength == 0.5f);
  CHECK(k.key_ambient[0] == 1.0f && k.key_ambient[1] == 0.9f && k.key_ambient[2] == 0.8f);
}

TEST_CASE("lighting: lights.txt skips malformed lines, rejects a missing/other version, clamps") {
  AreaScene s;
  CHECK(ParseLights("version 1\nlight 1 2 3\nlight 10 20 30 100 1 0.5 0.2 0.8 fire 1\nlight 4 5 6 70 1 1 1 1 nope 1\n",
                    s));
  CHECK(s.lights.size() == 1 && s.lights[0].kind == Kind::Fire && s.lights[0].x == 10.0f);
  AreaScene none;
  CHECK(!ParseLights("light 10 20 30 100 1 0 0 1 fire 1\n", none));
  CHECK(none.lights.empty());
  CHECK(!ParseLights("version 2\nlight 10 20 30 100 1 0 0 1 fire 1\n", none));
  AreaScene c;
  CHECK(ParseLights("version 1\r\nlight 0 0 0 2000 2 -1 0.5 3 magic 0\r\nlight 0 0 0 1 1 1 1 1 steady 0\r\n", c));
  CHECK(c.lights.size() == 2 && c.lights[0].radius == 600.0f && c.lights[0].r == 1.0f && c.lights[0].g == 0.0f);
  CHECK(c.lights[0].strength == 1.0f && c.lights[1].radius == 10.0f && c.lights[1].kind == Kind::Steady);
}

TEST_CASE("lighting: GLH1/GLN1/GLA1 maps (GemRB ReadMap cases)") {
  std::vector<uint8_t> payload(3 * 2 * 2);
  for (size_t i = 0; i < payload.size(); ++i) payload[i] = uint8_t(i + 1);
  auto good = MapBytes("GLH1", 3, 2, 2, 0.0625f, payload);
  SceneMap m;
  CHECK(ReadMap(good.data(), good.size(), "GLH1", 2, m));
  CHECK(m.w == 3 && m.h == 2 && m.texel == 2 && m.unit == 0.0625f);
  CHECK(m.data.size() == payload.size() && m.data[0] == 1 && m.data[11] == 12);
  auto wrong = MapBytes("GLN1", 3, 2, 2, 0.0625f, payload);
  SceneMap bad;
  CHECK(!ReadMap(wrong.data(), wrong.size(), "GLH1", 2, bad));
  CHECK(bad.data.empty() && bad.w == 0);
  std::vector<uint8_t> trunc(20, 1);
  std::memcpy(trunc.data(), "GLH1", 4);
  CHECK(!ReadMap(trunc.data(), trunc.size(), "GLH1", 2, bad));
  good.pop_back();  // size must be exact
  CHECK(!ReadMap(good.data(), good.size(), "GLH1", 2, bad));

  std::vector<uint8_t> rgb(3 * 2 * 3);
  for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = uint8_t(i * 7);
  auto nb = MapBytes("GLN1", 3, 2, 2, 1.0f, rgb);
  SceneMap n;
  CHECK(ReadMap(nb.data(), nb.size(), "GLN1", 3, n));
  CHECK(n.w == 3 && n.h == 2 && n.data[0] == 0 && n.data[17] == uint8_t(17 * 7));
  CHECK(!ReadMap(nb.data(), nb.size(), "GLA1", 1, bad));
  std::vector<uint8_t> ao(6);
  for (size_t i = 0; i < ao.size(); ++i) ao[i] = uint8_t(255 - i * 20);
  auto ab = MapBytes("GLA1", 3, 2, 2, 1.0f, ao);
  SceneMap a;
  CHECK(ReadMap(ab.data(), ab.size(), "GLA1", 1, a));
  CHECK(a.data[0] == 255 && a.data[5] == uint8_t(255 - 5 * 20));
}

TEST_CASE("lighting: HeightAt nearest + clamped, HeightFloats (GemRB HeightAtNearestClamped)") {
  std::vector<uint8_t> p(4 * 2 * 2);
  for (int i = 0; i < 8; ++i) {
    p[size_t(i) * 2] = uint8_t(i);
    p[size_t(i) * 2 + 1] = 0;
  }
  auto b = MapBytes("GLH1", 4, 2, 2, 0.0625f, p);
  SceneMap m;
  CHECK(ReadMap(b.data(), b.size(), "GLH1", 2, m));
  CHECK(HeightAt(m, 0.0f, 0.0f) == 0.0f);
  CHECK(HeightAt(m, 2.0f, 0.0f) == 1.0f * 0.0625f);
  CHECK(HeightAt(m, 3.9f, 0.1f) == 1.0f * 0.0625f);
  CHECK(HeightAt(m, 6.0f, 2.0f) == 7.0f * 0.0625f);
  CHECK(HeightAt(m, -10.0f, -10.0f) == 0.0f);
  CHECK(HeightAt(m, 1000.0f, 1000.0f) == 7.0f * 0.0625f);
  const auto f = HeightFloats(m);
  CHECK(f.size() == 8 && f[0] == 0.0f && f[5] == 5 * 0.0625f && f[7] == 7 * 0.0625f);
  SceneMap empty;
  CHECK(HeightAt(empty, 1, 1) == 0.0f);
}

TEST_CASE("lighting: falloff (squashed y, height, radius edge)") {
  CHECK(Near(Falloff(0, 0, 0, 0, 0, 0, 100), 1.0));
  CHECK(Near(Falloff(0, 0, 0, 50, 0, 0, 100), 0.5625));  // (1 - 0.25)^2
  CHECK(Near(Falloff(0, 0, 0, 0, 36, 0, 100), 0.5625));  // 36 / 0.72 = 50
  CHECK(Near(Falloff(0, 0, 0, 0, 0, 50, 100), 0.5625));
  CHECK(Falloff(0, 0, 0, 100, 0, 0, 100) == 0.0f);
  CHECK(Falloff(0, 0, 0, 0, 80, 0, 100) == 0.0f);  // 80/0.72 > 100
}

TEST_CASE("lighting: height-map occlusion (flat lit, wall in the band blocks, far wall does not)") {
  // Flat floor: Q is always above the surface it is drawn over -> no hits.
  const SceneMap flat = Uniform(256, 256, 0, 0.0625f);
  CHECK(Occlusion(flat, 100, 300, 0, 100, 100, 50, 16, 2.0f, 48.0f, 0.0f) == 1.0f);
  // Uniform height H: diff = (1 + T)(H - qz). H = 20 px, light 10 px up: diff in [22.9, 45.7] -> blocked.
  const SceneMap wall20 = Uniform(256, 256, 320, 0.0625f);  // 320 / 16 = 20 px
  CHECK(Near(HeightAt(wall20, 10, 10), 20.0));
  CHECK(Occlusion(wall20, 100, 300, 0, 100, 100, 10, 16, 2.0f, 48.0f, 0.0f) == 0.0f);
  // H = 100 px: diff > thickness 48 (the surface is far behind) -> lit.
  const SceneMap wall100 = Uniform(256, 256, 1600, 0.0625f);
  CHECK(Occlusion(wall100, 100, 300, 0, 100, 100, 10, 16, 2.0f, 48.0f, 0.0f) == 1.0f);
  CHECK(Occlusion(wall20, 100, 300, 0, 100, 100, 10, 0, 2.0f, 48.0f, 0.0f) == 1.0f);  // steps 0 = off
}

TEST_CASE("lighting: shader jitter hash and the falloff debug pixel") {
  // fract(52.9829189 * fract(0.5 * 0.06711056 + 0.5 * 0.00583715)) = fract(52.9829189 * 0.036473855)
  CHECK(Near(Jitter(0.5f, 0.5f), 0.932485, 1e-3));
  for (float x = 0.5f; x < 300; x += 17.0f) {
    const float j = Jitter(x, x * 0.7f + 0.5f);
    CHECK(j >= 0.0f && j < 1.0f);
  }
  const float st[4] = {2.0f / 800, -2.0f / 600, -2.0f * 100 / 800 - 1, 2.0f * 50 / 600 + 1};
  const ViewMap m = MakeViewMap(800, 600, st, 900, 1950);
  BakedLight l;
  l.x = 1400;
  l.y = 2300;
  l.z = 0;
  l.radius = 100;
  FalloffPixel f{};
  // light-buffer pixel (199, 149) at to_phys 2: phys (399, 299) -> area (1399, 2301): on the light, flat
  CHECK(DebugFalloffAt(m, 2.0f, 199, 149, l, nullptr, 0, &f));
  CHECK(f.red == 0.0f && Near(f.green, 0.5 * Falloff(1399, 2301, 0, 1400, 2300, 0, 100), 1e-5));
  CHECK(!DebugFalloffAt(m, 2.0f, 10, 10, l, nullptr, 0, &f));  // far outside
  const SceneMap wall = Uniform(2048, 2048, 320, 0.0625f);   // 20 px everywhere: vertical
  CHECK(DebugFalloffAt(m, 2.0f, 199, 140, l, &wall, 16, &f) && f.red == 0.5f);
}

TEST_CASE("lighting: wrapped Lambert and the normal ratio") {
  const float up[3] = {0, 0, 1}, side[3] = {1, 0, 0}, l_up[3] = {0, 0, 1}, l_side[3] = {1, 0, 0};
  CHECK(Near(Lambert(up, l_up), 1.0));
  CHECK(Near(Lambert(up, l_side), 0.4 / 1.4));
  CHECK(Near(NormalRatio(up, l_up), 1.0));
  CHECK(Near(NormalRatio(side, l_side), 2.5));  // 1 / (0.4/1.4) = 3.5, clamped to 2.5
}

TEST_CASE("lighting: ambient by area type, key tint, light scale, flicker") {
  Rgb d = Ambient(0x20, 1.0f, nullptr, true);
  CHECK(Near(d.r, 0.66 * 0.97) && Near(d.g, 0.66 * 0.98) && Near(d.b, 0.66 * 1.05));
  Rgb o = Ambient(0x01, 1.0f, nullptr, true);
  CHECK(Near(o.r, 0.86 * 0.97) && Near(o.b, std::fmin(1.0, 0.86 * 1.05)));
  Rgb c = Ambient(0x08, 1.0f, nullptr, true);  // city: the "other" 0.76
  CHECK(Near(c.g, 0.76 * 0.98));
  Rgb none = Ambient(0x20, 0.0f, nullptr, true);  // strength 0 = no darkening
  CHECK(Near(none.r, 0.97) && Near(none.g, 0.98) && Near(none.b, 1.0));
  AreaScene k;
  k.has_key = true;
  k.key_ambient[0] = 1.2f;
  k.key_ambient[1] = 1.0f;
  k.key_ambient[2] = 0.8f;
  Rgb t = Ambient(0x20, 1.0f, &k, true);
  const double l = Luma(d);
  CHECK(Near(t.r, d.r * 0.5 + 0.5 * l * 1.2) && Near(t.b, d.b * 0.5 + 0.5 * l * 0.8));
  Rgb off = Ambient(0x20, 1.0f, &k, false);
  CHECK(Near(off.r, d.r));
  BakedLight bl;
  bl.strength = 0.8f;
  CHECK(Near(LightScale(bl, 1.0f, 1.0f, 0.5f), 0.48));
  CHECK(Flicker(Kind::Steady, 3, 12345) == 1.0f && Flicker(Kind::Pool, 0, 99) == 1.0f);
  CHECK(Near(Flicker(Kind::Magic, 0, 0), 0.92) && Near(Flicker(Kind::Fire, 0, 0), 0.88));
  for (double tm = 0; tm < 5000; tm += 37) {
    const float f = Flicker(Kind::Fire, 7, tm), g = Flicker(Kind::Magic, 7, tm);
    CHECK(f >= 0.76f && f <= 1.0f && g >= 0.84f && g <= 1.0f);
  }
}

TEST_CASE("lighting: composite (identity at 1, darkening, soft shoulder, highlight keep)") {
  Rgb c{0.4f, 0.3f, 0.2f};
  Rgb a = Composite(c, {1, 1, 1}, 1.25f, 0.6f);
  CHECK(Near(a.r, 0.4) && Near(a.g, 0.3) && Near(a.b, 0.2));
  Rgb h = Composite(c, {0.5f, 0.5f, 0.5f}, 1.25f, 0.6f);
  CHECK(Near(h.r, 0.2) && Near(h.b, 0.1));
  Rgb s = Composite({0.5f, 0.5f, 0.5f}, {2, 2, 2}, 1.25f, 0.6f);  // 1 + 1/(1 + 1/0.25) = 1.2
  CHECK(Near(s.r, 0.6));
  Rgb k = Composite({1, 1, 1}, {0.5f, 0.5f, 0.5f}, 1.25f, 0.6f);  // max 1 -> hi = 0.6: 0.5 + 0.5*0.6
  CHECK(Near(k.r, 0.8));
}

TEST_CASE("lighting: framebuffer <-> area mapping from the engine's uST (FBO branch)") {
  // DrawBeginScaled_GL FBO branch: ortho over rect l=100 t=50 w=800 h=600 (sub_1404B6DA0).
  const float st[4] = {2.0f / 800, -2.0f / 600, -2.0f * 100 / 800 - 1, 2.0f * 50 / 600 + 1};
  // view (1000, 2000): off = view - rect.left = (900, 1950)
  const ViewMap m = MakeViewMap(800, 600, st, 900, 1950);
  float x, y;
  ToArea(m, 0, 600, &x, &y);  // top-left pixel = the view origin
  CHECK(Near(x, 1000, 1e-2) && Near(y, 2000, 1e-2));
  ToArea(m, 0, 0, &x, &y);  // GL y is up: bottom-left
  CHECK(Near(x, 1000, 1e-2) && Near(y, 2600, 1e-2));
  ToArea(m, 800, 0, &x, &y);
  CHECK(Near(x, 1800, 1e-2) && Near(y, 2600, 1e-2));
  float px, py;
  ToPhys(m, 1400, 2300, &px, &py);
  CHECK(Near(px, 400, 1e-2) && Near(py, 300, 1e-2));
  BakedLight l;
  l.x = 1400;
  l.y = 2300;
  l.radius = 100;
  Rect r;
  CHECK(FootprintInBuffer(m, l, 2.0f, 400, 300, &r));
  CHECK(Near(r.x0, 150, 1e-2) && Near(r.x1, 250, 1e-2) && Near(r.y0, 114, 1e-2) && Near(r.y1, 236, 1e-2));
  l.x = 5000;  // off to the right
  CHECK(!FootprintInBuffer(m, l, 2.0f, 400, 300, &r));
  // Back-buffer branch (zoom 1): base slot scaled by dst/src; any uST maps the same way.
  const float st2[4] = {2.0f / 1024, -2.0f / 768, -1.0f, 1.0f};
  const ViewMap m2 = MakeViewMap(1024, 768, st2, 300, 400);
  ToArea(m2, 0, 768, &x, &y);
  CHECK(Near(x, 300, 1e-2) && Near(y, 400, 1e-2));
}
