// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Contributors to the GemRB project <https://gemrb.org> (parser cases from
// gemrb/tests/core/Test_AreaSceneData.cpp)
#include <algorithm>
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
  // lea rax, dword_14601AEC0 (scratch surfaces) at 1404B6BDB; mov dword_14601ADE0, ecx (draw colour) at 1404B4726
  std::memcpy(&d, sym::kScratchLeaBytes + 3, 4);
  CHECK(sym::kImageBase + sym::kRvaScratchLea + 7 + d == sym::kVaScratch);
  std::memcpy(&d, sym::kDrawColorStoreBytes + 2, 4);
  CHECK(sym::kImageBase + sym::kRvaDrawColorStore + 6 + d == sym::kVaDrawColor);
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

TEST_CASE("lighting fx: glow blend flags (RenderTexture 1404AEAE0)") {
  CHECK(!IsGlowBlend(0));            // SRC_ALPHA, 1-SRC_ALPHA
  CHECK(IsGlowBlend(0x8));           // 1-DST_COLOR, ONE
  CHECK(IsGlowBlend(0x200));         // DST_COLOR, ONE
  CHECK(IsGlowBlend(0x208));         // SRC_COLOR, ONE
  CHECK(IsGlowBlend(0x208 | 0x100)); // other bits do not matter
  CHECK(!IsGlowBlend(0x208 | 0x20000000));  // bit 29: multiply
  CHECK(!IsGlowBlend(0x40000000));   // the BAM v2 multiply flag is not a glow
  CHECK(sym::kRenderTextureBlendBytes[1] == 0x08 && sym::kRenderTextureBlendBytes[2] == 0x02);
  CHECK(sym::kRenderTextureCopy == 5 && sym::kRenderTextureBytes[0] == 0x4C);  // mov [rsp+20h], r9: 5 bytes
  CHECK(sym::kRender3dCopy == 6 && sym::kFxRender3dCopy == 6);  // 40 5x, 5x, 5x, 41 5x
}

TEST_CASE("lighting fx: vfx table parse and lookup") {
  VfxTable t;
  CHECK(ParseVfxTable("# header\nSPFIRE\t0\t0\t1.000\t0.500\t0.100\t0.2500\t10.0\t20.0\t42\n"
                      "SPFIRE\t0\t1\t1\t0.6\t0.2\t0.3\t11\t21\t40\n"
                      "spark\t2\t7\t0.2\t0.4\t1\t0.1\t3\t4\t5\nTOOLONGNAME 0 0 1 1 1 1 1 1 1\nbad line\n",
                      &t));
  CHECK(t.rows.size() == 3);
  const VfxFrame* f = t.Find("SPFIRE", 0, 1);
  CHECK(f && Near(f->g, 0.6) && Near(f->strength, 0.3) && f->cx == 11.0f && f->bright == 40);
  CHECK(t.Find("spfire", 0, 0) && Near(t.Find("spfire", 0, 0)->b, 0.1));  // case-insensitive
  CHECK(t.Find("SPARK", 2, 7) && t.Find("SPARK", 2, 7)->bright == 5);
  CHECK(!t.Find("SPFIRE", 1, 0) && !t.Find("SPFIRE", 0, 2) && !t.Find("SPFIR", 0, 0));
  CHECK(VfxKey("AB", 0, 1) != VfxKey("AB", 1, 0) && VfxKey("AB", 0, 0) != VfxKey("BA", 0, 0));
  VfxTable e;
  CHECK(!ParseVfxTable("# nothing\n", &e));
}

TEST_CASE("lighting fx: effect light from a glow draw (GemRB AddGlowLight numbers)") {
  VfxFrame v;
  v.r = 1.0f;
  v.g = 0.5f;
  v.b = 0.25f;
  v.strength = 0.2f;
  v.cx = 30;
  v.cy = 20;
  GlowDraw d;
  d.x = 100;
  d.y = 200;
  d.w = 60;
  d.h = 80;
  d.vfx = &v;
  BakedLight l;
  CHECK(FxLight(d, 2.6f, &l));
  CHECK(Near(l.x, 130) && Near(l.y, 280) && Near(l.z, 60));  // ground = frame bottom, drawn on (130, 220)
  CHECK(Near(l.radius, 104));                                 // 80 * 1.3
  CHECK(Near(l.r, 0.52) && Near(l.g, 0.26) && Near(l.b, 0.13));  // k = 0.2 * 2.6
  v.strength = 0.01f;
  CHECK(!FxLight(d, 2.6f, &l));  // too faint
  v.strength = 0.02f;
  CHECK(FxLight(d, 2.6f, &l) && Near(l.r, 0.12));  // k floor 0.12
  v.strength = 1.0f;
  CHECK(FxLight(d, 2.6f, &l) && Near(l.r, 0.9));   // k cap 0.9
  d.w = d.h = 10;
  CHECK(FxLight(d, 2.6f, &l) && l.radius == 60.0f);  // radius floor
  d.w = d.h = 1000;
  CHECK(FxLight(d, 2.6f, &l) && l.radius == 380.0f);  // radius cap
  d.vfx = nullptr;
  CHECK(!FxLight(d, 2.6f, &l));  // not in the table: no light
}

TEST_CASE("lighting fx: merge close effect lights, keep the strongest within budget") {
  auto mk = [](float x, float y, float r, float rad) {
    BakedLight l;
    l.x = x;
    l.y = y;
    l.z = 0;
    l.r = l.g = l.b = r;
    l.radius = rad;
    return l;
  };
  std::vector<BakedLight> in = {mk(0, 0, 0.2f, 100), mk(10, 10, 0.5f, 60), mk(500, 0, 0.1f, 60), mk(900, 0, 0.9f, 200)};
  auto out = MergeFxLights(in, 48.0f, 8);
  CHECK(out.size() == 3);
  CHECK(Near(out[0].r, 0.9) && out[0].x == 900.0f);  // strongest first
  CHECK(Near(out[1].r, 0.7) && out[1].x == 10.0f && out[1].radius == 100.0f);  // 0.5 + 0.2 merged
  auto two = MergeFxLights(in, 48.0f, 2);
  CHECK(two.size() == 2 && two[1].x == 10.0f);
  auto cap = MergeFxLights({mk(0, 0, 1.0f, 60), mk(1, 1, 1.0f, 60)}, 48.0f, 4);
  CHECK(cap.size() == 1 && cap[0].r == 1.5f);
}

TEST_CASE("lighting bloom: soft knee (GemRB PostFS, knee 0.18)") {
  CHECK(SoftKnee(0.1f, 0.45f, 0.18f) == 0.0f);                  // far below: nothing
  CHECK(Near(SoftKnee(1.0f, 0.45f, 0.18f), 0.55));              // above the knee: (l - t) / l
  CHECK(Near(SoftKnee(0.45f, 0.45f, 0.18f), 0.1, 1e-3));  // at t: k^2/(4k) / l = 0.045 / 0.45
  CHECK(SoftKnee(0.5f, 0.45f, 0.18f) > SoftKnee(0.4f, 0.45f, 0.18f));
}

TEST_CASE("lighting atmosphere v1: ramp by height, haze colour from the key light") {
  Atmosphere a;
  CHECK(AtmosphereT(a, 0) == 0.0f && AtmosphereT(a, 200) == 0.0f && AtmosphereT(a, 1024) == 1.0f);
  CHECK(Near(AtmosphereT(a, 612), 0.5));
  const Rgb c{0.8f, 0.2f, 0.4f}, hz{0.5f, 0.5f, 0.6f};
  const Rgb flat = ApplyAtmosphere(a, c, 100, hz, 1.0f);
  CHECK(flat.r == c.r && flat.g == c.g && flat.b == c.b);  // floor level: untouched
  const Rgb hi = ApplyAtmosphere(a, c, 1024, hz, 1.0f);
  // t = 1: r = 0.8 -> desat (l = 0.4022): 0.76818; contrast (v-0.5)*0.95+0.5: 0.75477; haze + (0.5-v)*0.06:
  // 0.73948
  CHECK(Near(hi.r, 0.73948, 1e-4));
  // black void at the saturated height stays exactly black (no contrast lift, no haze)
  const Rgb black = ApplyAtmosphere(a, {0, 0, 0}, 1024, hz, 1.0f);
  CHECK(black.r == 0.0f && black.g == 0.0f && black.b == 0.0f);
  const Rgb off = ApplyAtmosphere(a, c, 1024, hz, 0.0f);
  CHECK(off.r == c.r);
  AreaScene s;
  const Rgb amb{0.6f, 0.6f, 0.6f};
  CHECK(HazeColour(s, amb).r == 0.6f);  // no key line: the ambient
  s.has_key = true;
  s.key_ambient[0] = 1.2f;
  s.key_ambient[1] = 1.0f;
  s.key_ambient[2] = 0.8f;
  const Rgb h = HazeColour(s, amb);  // key hue at the ambient luma 0.6: luma(key) = 1.037
  CHECK(Near(h.r, 1.2 / 1.037 * 0.6, 1e-3) && Near(h.b, 0.8 / 1.037 * 0.6, 1e-3));
}

TEST_CASE("lighting fx: emit list (vfxemit.txt) and the emit rule") {
  const auto names = ParseEmitList("# header\r\nA003INTR\r\ns031axst\nS031AXST\n\nTOOLONGNAME\n  \nSPMAGMIS\n");
  CHECK(names.size() == 3);  // case-folded, deduplicated, comments/blank/too long skipped
  CHECK(PackResRef("a003intr") == PackResRef("A003INTR") && PackResRef("AB") != PackResRef("ABC"));
  CHECK(Emits(names, "A003INTR", 0x1));         // normal blend, listed (CGameSprite::ApplyCastingEffect)
  CHECK(Emits(names, "s031axst", 0x30091));     // listed, any blend
  CHECK(!Emits(names, "DSTDDKKB", 0x30091));    // a creature, normal blend: no
  CHECK(Emits(names, "DSTDDKKB", 0x208));       // anything additive emits
  CHECK(!Emits(names, "", 0x1) && Emits(names, "", 0x200));  // unknown cell: only by blend
  CHECK(!Emits(names, "GRNDGLOW", 0x208 | 0x20000000));      // multiply is not a glow
}

TEST_CASE("lighting: hand light table (Ignus) and pattern match") {
  const auto t = ParseHandLights("# c\n????IGYB 1.0 0.55 0.22 240 55 fire 1.0\nBAD 1 1\nlongpattern1 1 1 1 1 1 fire\n"
                                 "x?z 2 -1 0.5 5 10 nope\nSPARK 0.5 0.5 1 900 20 magic\n");
  CHECK(t.size() == 2);
  CHECK(MatchPattern("????IGYB", "CSTDIGYB") && MatchPattern("????IGYB", "cat1igyb"));
  CHECK(!MatchPattern("????IGYB", "CSTDIGY") && !MatchPattern("????IGYB", "CSTDMRTB") && !MatchPattern("????IGYB", ""));
  CHECK(MatchPattern("SPARK", "SPARK") && !MatchPattern("SPARK", "SPARKS") && !MatchPattern("SPARK", "SPAR"));
  CHECK(FindHandLight(t, "DWLKIGYB") == &t[0] && !FindHandLight(t, "DSTDDKKB") && !FindHandLight(t, ""));
  CHECK(t[1].light.radius == 600.0f && t[1].light.kind == Kind::Magic);  // radius clamped
  GlowDraw d;
  d.x = 100;
  d.y = 200;
  d.w = 60;
  d.h = 90;
  const BakedLight l = HandLightAt(t[0], d, 0, 0.0, 1.0f);  // fire flicker at t=0, index 0: 0.88
  CHECK(Near(l.x, 130) && Near(l.y, 290) && Near(l.z, 55) && Near(l.radius, 240));
  CHECK(Near(l.r, 0.88) && Near(l.g, 0.55 * 0.88) && Near(l.b, 0.22 * 0.88, 1e-4));
}

TEST_CASE("lighting: creature lights (anim:N, Ignus = RESDATA [36]) are fixed to the creature") {
  const auto t = ParseHandLights("anim:36 1.0 0.55 0.22 240 55 steady 1.0\nanim:x 1 1 1 1 1 fire\n"
                                 "anim:70000 1 1 1 1 1 fire\n????IGYB 1 1 1 100 10 fire\n");
  CHECK(t.size() == 2 && t[0].anim == 36 && t[0].pattern[0] == 0 && t[1].anim == -1);
  CHECK(FindHandLight(t, "CSTDIGYB") == &t[1]);  // anim rows never match a BAM
  CHECK(FindCreatureLight(t, 36) == &t[0] && FindCreatureLight(t, 0x1024) == &t[0]);  // exact, then the low byte
  CHECK(!FindCreatureLight(t, 37) && !FindCreatureLight(t, -1));
  // Same light whatever the time: steady, at the feet, owned.
  const BakedLight a = CreatureLightAt(t[0], 500, 700, 0.0, 1.5f), b = CreatureLightAt(t[0], 500, 700, 12345.0, 1.5f);
  CHECK(Near(a.x, 500) && Near(a.y, 700) && Near(a.z, 55) && Near(a.radius, 240) && a.owned);
  CHECK(Near(a.r, 1.5) && Near(a.g, 0.55 * 1.5) && Near(b.r, a.r) && Near(b.b, a.b));
  // The owner's own sprite does not block its light; another creature 50 px away does.
  CHECK(OwnerSprite(500, 700, 520, 703) && !OwnerSprite(500, 700, 550, 700) && !OwnerSprite(500, 700, 500, 712));
}

TEST_CASE("lighting: contact shadow (replaces the BAM blob)") {
  CHECK(Near(ContactShadow(0, 0, 22, 11, 0.45f), 0.45) && Near(ContactShadow(6, 0, 22, 11, 0.45f), 0.45));  // flat core
  CHECK(Near(ContactShadow(0, 7.15f, 22, 11, 0.45f), 0.225, 1e-3));  // r = 0.65: halfway down the smooth rim
  CHECK(ContactShadow(22, 0, 22, 11, 0.45f) == 0.0f && ContactShadow(0, 11.5f, 22, 11, 0.45f) == 0.0f);
  CHECK(ContactShadow(15, 0, 22, 11, 0.45f) > ContactShadow(15, 5, 22, 11, 0.45f));  // flatter than wide
  CHECK(ContactShadow(0, 0, 0, 11, 1.0f) == 0.0f);
}

TEST_CASE("lighting: day / night (engine tint undone, our ambient, lightmap pools)") {
  const DayNight d;
  CHECK(Nightness(0xFFFFFF, d.night_luma) == 0.0f);
  CHECK(Near(Nightness(0x6E6E6E, 110 / 255.0f), 1.0, 1e-4));  // the night tint itself: full night
  CHECK(Near(Nightness(0xB6B6B6, 110 / 255.0f), (1 - 182 / 255.0) / (1 - 110 / 255.0), 1e-4));
  CHECK(Nightness(0x000000, 0.43f) == 1.0f);  // clamped
  const Rgb day{0.86f, 0.86f, 0.86f};
  const Rgb a0 = TimeOfDayAmbient(day, 0.0f, d), a1 = TimeOfDayAmbient(day, 1.0f, d), ah = TimeOfDayAmbient(day, 0.5f, d);
  CHECK(Near(a0.r, 0.86) && Near(a0.b, 0.86));  // day: unchanged
  CHECK(Near(a1.r, 0.86 * 0.45 * 0.66, 1e-4) && Near(a1.b, 0.86 * 0.45 * 1.28, 1e-4));  // moonlight: cool, dark
  CHECK(ah.r > ah.b && Luma(ah) < Luma(a0) && Luma(ah) > Luma(a1));  // dusk: warm, in between
  CHECK(NightLightGain(0, d) == 1.0f && Near(NightLightGain(1, d), 1.5) && Near(NightLightGain(0.5f, d), 1.25));
  {
    // night tint C86464 (r g 0.39, b 0.78): an untinted effect pixel stays as drawn, the ground is undone
    const Rgb t = TintRgb(0xC86464);
    const Rgb w = UndoTintFx({0.7f, 0.7f, 0.75f}, t, 1.0f);  // the hammer: kept as drawn
    CHECK(Near(w.r, 0.7) && Near(w.b, 0.75));
    CHECK(Near(EffectCover({0.005f, 0, 0}), 0.0) && Near(EffectCover({0.2f, 0.1f, 0}), 1.0));
    CHECK(Near(EffectCover({0.065f, 0, 0}), 0.5));
    CHECK(SoftenAt(0.0f) && SoftenAt(0.49f) && !SoftenAt(0.5f) && !SoftenAt(Nightness(0xC86464, 0.43f)));  // the Hive at 1 am
    const Rgb n = UndoTintFx({0.2f, 0.2f, 0.4f}, t, 0.0f);  // no effect: the plain undo
    const Rgb o = UndoTint({0.2f, 0.2f, 0.4f}, t);
    CHECK(Near(n.r, o.r) && Near(n.b, o.b));
    // Deathbolt's dark casting layer (A009NECR, flags 0x40: untinted, normal blend, alpha 0.6 -> mask 0.36):
    // only 40 % of the pixel is the tinted ground behind it, so only that much is undone (r, g x2.55 at C86464)
    const Rgb dark{0.12f, 0.14f, 0.18f};
    const float k = UntintedCover({0, 0, 0}, 0.36f);
    CHECK(Near(k, 0.6, 1e-5));
    const Rgb d = UndoTintFx(dark, t, k);
    CHECK(d.r < 0.2f && Near(d.r, 0.12 + (0.12 / (100 / 255.0) - 0.12) * 0.4, 1e-4));
    CHECK(Near(d.b, 0.18 + (0.18 / (200 / 255.0) - 0.18) * 0.4, 1e-4));
    CHECK(Near(UntintedCover({0.3f, 0.2f, 0.1f}, 0.0f), 1.0));  // an additive glow: by its light
    CHECK(Near(UntintedCover({0.0f, 0.0f, 0.0f}, 0.0f), 0.0));  // the black of an additive BAM: not covered
    CHECK(Near(FxCover({0.05f, 0.04f, 0.03f}, 1.0f), 1.0));   // Ignus' dark body: covered by his alpha
    CHECK(Near(FxCover({0.05f, 0.04f, 0.03f}, 0.0f), EffectCover({0.05f, 0.04f, 0.03f})));
    CHECK(UntintedCell(0x40) && UntintedCell(0x41) && !UntintedCell(0x300C0) && !UntintedCell(0x10000));
    CHECK(FxMaskKind(0x1, false) == 1 && FxMaskKind(0x8, false) == 2 && FxMaskKind(0x200, false) == 2);
    CHECK(FxMaskKind(0x1, true) == 1 && FxMaskKind(0x20000001, false) == 0);  // softened: still its alpha
  }
  const Rgb u = UndoTint({0.2f, 0.3f, 0.6f}, TintRgb(0x80A0FF));  // tint r 1.0, g 160/255, b 128/255
  CHECK(Near(u.r, 0.2) && Near(u.g, 0.3 / (160 / 255.0), 1e-4) && Near(u.b, 1.0));  // b capped at 1
  CHECK(Near(UndoTint({0.1f, 0.1f, 0.1f}, {0.0f, 0.0f, 0.0f}).r, 0.5));            // tint floor 0.2
  const Rgb pool = LightmapPool({0.9f, 0.6f, 0.3f}, 0.4f, 0.6f);  // luma 0.6555: 0.2555 above the mean
  CHECK(Near(pool.r, 0.9 * 0.6 * 0.2555 / 0.6555, 1e-3) && pool.r > pool.b);
  CHECK(LightmapPool({0.3f, 0.3f, 0.3f}, 0.4f, 0.6f).r == 0.0f);  // darker than the mean: no pool
}

TEST_CASE("lighting: colour effect lights (ops 8, 9, 52, 61, 353)") {
  Rgb c;
  // op 8 solid: param1 0xBBGGRR00 = red 0xFF, green 0x80, blue 0
  CHECK(ColorEffectLight(8, 0x0080FF00u, 0, 0, 0, 1.0f, &c) && Near(c.r, 1.0) && Near(c.g, 128 / 255.0) && c.b == 0.0f);
  CHECK(ColorEffectLight(52, 0xFF000000u, 0, 0, 0, 0.5f, &c) && Near(c.b, 0.5) && c.r == 0.0f);
  CHECK(ColorEffectLight(9, 0x00FF0000u, 0, 0, 375.0, 1.0f, &c) && Near(c.g, 1.0));    // pulse peak (1/4 cycle)
  CHECK(ColorEffectLight(9, 0x00FF0000u, 0, 0, 1125.0, 1.0f, &c) && Near(c.g, 0.1));   // trough
  // op 61 fade: speed 10 (high word of param2) -> 255/10 steps * 66.7 ms = 1700.9 ms
  CHECK(ColorEffectLight(61, 0x0000FF00u, 10u << 16, 850.45, 0, 1.0f, &c) && Near(c.r, 0.5, 1e-3));
  CHECK(!ColorEffectLight(61, 0x0000FF00u, 10u << 16, 1800, 0, 1.0f, &c));             // faded out
  // op 353 tint screen: colour in the low bytes (SPWI406 0x808080), rises and falls over 1.5 s
  CHECK(ColorEffectLight(353, 0x808080u, 5, 750, 0, 1.0f, &c) && Near(c.r, 128 / 255.0) && Near(c.b, 128 / 255.0));
  CHECK(!ColorEffectLight(353, 0x808080u, 5, 1600, 0, 1.0f, &c));
  CHECK(!ColorEffectLight(8, 0, 0, 0, 0, 1.0f, &c));     // black: no light
  {  // equipped weapon glow (MPUNCH, timing 2): small and weak; spell durations light the whole body
    const BuffLightShape w = BuffLightFor(2, 70.0f, 0.3f), b = BuffLightFor(0, 70.0f, 0.3f);
    CHECK(w.radius == 70.0f && w.gain == 0.3f && w.z == 30.0f);
    CHECK(b.radius == 170.0f && b.gain == 1.0f && b.z == 45.0f);
  }
  CHECK(!ColorEffectLight(51, 0x00FF0000u, 0, 0, 0, 1.0f, &c));  // solid tint darkens: no light
}

TEST_CASE("lighting: emissive weight of an effect pixel (brightness x saturation)") {
  CHECK(EmissiveWeight(0, 0, 0) == 0.0f);                  // black: no glow
  CHECK(EmissiveWeight(0.06f, 0.06f, 0.06f) == 0.0f);      // below the ramp (dark edges)
  CHECK(Near(EmissiveWeight(1, 1, 1), 0.55));              // white core: full ramp, no saturation
  CHECK(Near(EmissiveWeight(1, 0, 1), 1.0));               // saturated magenta (Magic Missile) glows most
  CHECK(Near(EmissiveWeight(0.34f, 0.34f, 0.34f), 0.5f * 0.55f, 1e-3));  // mid grey smoke: half ramp
}

TEST_CASE("lighting: glow overrides (vfxglow.tsv)") {
  const auto t = ParseGlowOverrides("# c\nS0??SMK 0 0\nspmagmis 1 0\nbad\n");
  CHECK(t.size() == 2);
  const GlowOverride* a = FindGlowOverride(t, "S012SMK");
  CHECK(a && !a->glow && !a->soften);
  const GlowOverride* b = FindGlowOverride(t, "SPMAGMIS");
  CHECK(b && b->glow && !b->soften);
  CHECK(!FindGlowOverride(t, "A003INTR") && !FindGlowOverride(t, ""));
}

TEST_CASE("lighting: effect light saturation") {
  const Rgb pale{1.0f, 0.85f, 0.99f};  // SPMAGMIS frame colour
  const Rgb v = Saturate(pale, 1.6f);
  CHECK(Near(Luma(v), Luma(pale), 1e-4));            // same brightness
  // luma 0.9108: r 1.0535, g 0.8135, b 1.0375 (green drops: violet)
  CHECK(Near(v.r, 1.0535, 1e-3) && Near(v.g, 0.8135, 1e-3) && Near(v.b, 1.0375, 1e-3));
  const Rgb g = Saturate({0.5f, 0.5f, 0.5f}, 3.0f);  // grey stays grey
  CHECK(Near(g.r, 0.5) && Near(g.b, 0.5));
  const Rgb same = Saturate(pale, 1.0f);
  CHECK(Near(same.g, 0.85));
}

TEST_CASE("lighting: sprite buffer packing, sprite light, key shadow projection") {
  float lm = 0, d = 0;
  UnpackSpriteG(PackSpriteG(0.6f, 12.25f), &lm, &d);
  CHECK(Near(lm, 9 / 15.0) && Near(d, 12.25));           // 0.6 * 15 = 9 levels
  UnpackSpriteG(PackSpriteG(1.0f, 80.0f), &lm, &d);       // depth capped at 63
  CHECK(Near(lm, 1.0) && Near(d, 63.0));
  UnpackSpriteG(PackSpriteG(0.0f, 0.5f), &lm, &d);
  CHECK(lm == 0.0f && Near(d, 0.5));
  // sprite light: no light above ambient = the scene's darkening; a torch adds relative to the LM
  CHECK(Near(SpriteLight(0.66f, 0.66f, 0.5f, 1.0f), 0.66));
  CHECK(Near(SpriteLight(0.96f, 0.66f, 0.5f, 1.0f), 1.26));
  CHECK(Near(SpriteLight(0.96f, 0.66f, 0.1f, 1.0f), 1.66));  // LM floor 0.3: dark nooks do not explode
  CHECK(Near(SpriteLight(0.5f, 0.66f, 0.5f, 1.0f), 0.66));   // occluded floor value: never below ambient
  // key shadow (GemRB project): dir (0.6, 0.8), k 0.85 -> sx 0.51, sy 0.85*0.8*0.72 - 0.15*0.2 = 0.4596
  float ax = 0, ay = 0;
  KeyShadowAxes(0.6f, 0.8f, 0.85f, &ax, &ay);
  CHECK(Near(ax, 0.51) && Near(ay, 0.4596));
  float sx = 0, sy = 0, z = 0;
  CHECK(KeyShadowSource(100.0f + 40 * ax, 500.0f + 40 * ay, 500.0f, ax, ay, 90.0f, &sx, &sy, &z));
  CHECK(Near(z, 40.0, 1e-3) && Near(sx, 100.0, 1e-3) && Near(sy, 460.0, 1e-3));
  CHECK(!KeyShadowSource(112.0f, 499.0f, 500.0f, ax, ay, 90.0f, &sx, &sy, &z));  // up-screen of the feet
  CHECK(!KeyShadowSource(112.0f, 560.0f, 500.0f, ax, ay, 90.0f, &sx, &sy, &z));  // past the head (130 px)
  KeyShadowAxes(1.0f, 0.0f, 0.85f, &ax, &ay);  // sideways: GemRB -0.15 up-screen, at least 0.22
  CHECK(Near(ax, 0.85) && Near(ay, -0.22));
  KeyShadowAxes(0.9f, 0.2f, 0.85f, &ax, &ay);  // 0.1224 - 0.12 = 0.0024: clamped to 0.22
  CHECK(Near(ay, 0.22));
}

TEST_CASE("lighting: sprite AO (ao.bin) darkens only the creature's ambient") {
  SceneMap ao;  // 2x2 texels of 2 px: open, half, enclosed, open
  ao.w = 2;
  ao.h = 2;
  ao.texel = 2;
  ao.data = {255, 128, 0, 255};
  CHECK(Near(AoAt(ao, 0.5f, 0.5f), 1.0) && Near(AoAt(ao, 3.9f, 1.0f), 128 / 255.0));
  CHECK(Near(AoAt(ao, 1.0f, 2.0f), 0.0) && Near(AoAt(ao, -50, 99), 0.0));  // clamped to the edge texels
  CHECK(AoAt(SceneMap(), 10, 10) == 1.0f);                                 // no map: open
  CHECK(Near(SpriteAoFactor(1.0f, 0.6f), 1.0) && Near(SpriteAoFactor(0.0f, 0.6f), 0.4));
  CHECK(Near(SpriteAoFactor(0.5f, 1.0f), 0.5) && Near(SpriteAoFactor(0.0f, 0.0f), 1.0));
  CHECK(Near(SpriteAoFactor(-1.0f, 2.0f), 0.0));  // clamped
  // ambient 0.66 x 0.4, the torch's 0.3 above the ambient untouched (LM 0.5: x2)
  CHECK(Near(SpriteLight(0.66f, 0.66f, 0.5f, 1.0f, 0.4f), 0.264));
  CHECK(Near(SpriteLight(0.96f, 0.66f, 0.5f, 1.0f, 0.4f), 0.864));
}

TEST_CASE("lighting: painted emitters of baked lights glow") {
  BakedLight l;  // AR0105 hanging lantern (GemRB LIGHTING_PLAN 14: emitter 1921,730, ground 1921,768, z 38)
  l.x = 1921;
  l.y = 768;
  l.z = 38;
  l.radius = 100;
  l.kind = Kind::Steady;
  float ex = 0, ey = 0, core = 0;
  CHECK(PaintedEmitter(l, 0.2f, &ex, &ey, &core) && Near(ex, 1921) && Near(ey, 730) && Near(core, 20));
  l.radius = 600;
  CHECK(PaintedEmitter(l, 0.2f, &ex, &ey, &core) && Near(core, 48));  // 8..48
  l.radius = 10;
  CHECK(PaintedEmitter(l, 0.2f, &ex, &ey, &core) && Near(core, 8));
  l.kind = Kind::Pool;  // light-map pools have nothing painted
  CHECK(!PaintedEmitter(l, 0.2f, &ex, &ey, &core));
  CHECK(Near(PaintedGlowWeight(1.0f, 0.8f, 0.4f, 5, 20), 1.0));     // bright flame pixel at the core
  CHECK(PaintedGlowWeight(0.6f, 0.5f, 0.4f, 0, 20) == 0.0f);        // the wall around it: no
  CHECK(Near(PaintedGlowWeight(0.81f, 0.81f, 0.81f, 0, 20), 0.5));  // halfway up the 0.70..0.92 ramp
  CHECK(Near(PaintedGlowWeight(1.0f, 1.0f, 1.0f, 15, 20), 0.5));    // halfway between half core and core
  CHECK(PaintedGlowWeight(1.0f, 1.0f, 1.0f, 20, 20) == 0.0f && PaintedGlowWeight(1, 1, 1, 0, 0) == 0.0f);
}

TEST_CASE("lighting: hand area lights (arealights.tsv)") {
  const auto t = ParseAreaLights("# WED x y z radius r g b strength kind shadow\r\n"
                                 "ar0508 1466 1700 27 180 1.0 0.6 0.3 0.8 fire 1\r\n"
                                 "AR0508 10 20 30 900 2 0.5 -1 0.5 steady 0\n"
                                 "AR0106 1 2 3 4 5 6\nTOOLONGWED 1 2 3 4 1 1 1 1 fire 0\nAR0106 1 2 3 40 1 1 1 1 lamp 0\n");
  CHECK(t.size() == 2);
  std::vector<BakedLight> out(1);
  CHECK(AddAreaLights(t, "Ar0508", &out) == 2 && out.size() == 3);
  CHECK(Near(out[1].x, 1466) && Near(out[1].y, 1700) && Near(out[1].z, 27) && Near(out[1].radius, 180));
  CHECK(out[1].kind == Kind::Fire && out[1].casts_shadow && Near(out[1].strength, 0.8));
  CHECK(Near(out[2].radius, 600) && out[2].r == 1.0f && out[2].b == 0.0f && !out[2].casts_shadow);  // clamps
  CHECK(AddAreaLights(t, "AR0106", &out) == 0 && out.size() == 3);
}
