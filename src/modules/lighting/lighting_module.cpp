// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Contributors to the GemRB project <https://gemrb.org> (shader math, LightFS)
#include "modules/lighting/lighting_module.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/shot.h"
#include "engine/gl_world.h"
#include "engine/lua.h"
#include "engine/render.h"
#include "modules/dev/lua_bridge.h"
#include "modules/lighting/lighting_logic.h"

namespace pstf::lighting {
namespace {

// GL enums.
constexpr unsigned kTex2D = 0x0DE1, kTexture0 = 0x84C0, kFramebuffer = 0x8D40, kColorAttach0 = 0x8CE0,
                   kFbComplete = 0x8CD5, kRgba = 0x1908, kRgb = 0x1907, kRed = 0x1903, kRgba8 = 0x8058,
                   kRgb8 = 0x8051, kR8 = 0x8229, kR32F = 0x822E, kRgba16F = 0x881A, kFloat = 0x1406,
                   kUByte = 0x1401, kMinFilter = 0x2801, kMagFilter = 0x2800, kNearest = 0x2600,
                   kLinear = 0x2601, kWrapS = 0x2802, kWrapT = 0x2803, kClampEdge = 0x812F, kColorBit = 0x4000,
                   kBlend = 0x0BE2, kScissor = 0x0C11, kDepth = 0x0B71, kCull = 0x0B44, kStencil = 0x0B90,
                   kAlphaTest = 0x0BC0, kDither = 0x0BD0, kOne = 1, kZero = 0, kFuncAdd = 0x8006,
                   kQuads = 0x0007, kVertexShader = 0x8B31, kFragmentShader = 0x8B30, kCompileStatus = 0x8B81,
                   kLinkStatus = 0x8B82, kUnpackAlign = 0x0CF5, kPackAlign = 0x0D05, kUnpackRow = 0x0CF2,
                   kViewport = 0x0BA2, kVersion = 0x1F02;

struct Settings {
  bool baked_lights = true, height_map = true, occlusion = true, area_normals = true, key_light = true;
  float light_gain = 1.0f, ambient = 1.0f, normal_strength = 0.8f, max_light = 1.25f, keep = 0.6f;
  int occlusion_steps = 16, budget = 64;
};
Settings g_set;
DebugMode g_mode = DebugMode::Lit;

engine::Gl g_gl = {};
bool g_ready = false, g_off = false;  // GL set up / turned itself off
unsigned g_light_prog = 0, g_comp_prog = 0;
unsigned g_scene_tex = 0, g_light_tex = 0, g_light_fbo = 0, g_height_tex = 0, g_normal_tex = 0, g_ao_tex = 0;
int g_scene_w = 0, g_scene_h = 0, g_light_w = 0, g_light_h = 0;

struct LightUni {
  int target, scene, height, normal, map, to_phys, light_pos, light_color, has_height, has_normals, occl_steps,
      occl_bias, occl_thick, texel, bump, normal_strength, debug;
} g_lu = {};
struct CompUni {
  int scene, light, vp_origin, vp_size, mode, keep, max_light;
} g_cu = {};

// Current area.
std::string g_wed;
AreaScene g_scene;
uint16_t g_area_type = 0;
bool g_uploaded = false;
Rgb g_ambient;

// Counters and the round-trip check (probes).
uint64_t g_frames = 0;
int g_drawn = 0;
bool g_check_req = false, g_check_done = false;
long long g_check_diff = -1, g_check_total = 0;
std::vector<uint8_t> g_before, g_after;
// GPU vs CPU probe (falloff mode): light buffer read back and compared with DebugFalloffAt.
bool g_cmp_req = false, g_cmp_done = false;
int g_cmp_n = 0, g_cmp_bad = 0, g_cmp_lit = 0;
float g_cmp_max = 0;

const char* kLightVs = R"(#version 130
uniform vec2 u_target;
void main() { gl_Position = vec4(gl_Vertex.xy / u_target * 2.0 - 1.0, 0.0, 1.0); }
)";

// GemRB LightFS mode 0 (geometry path), in area pixels (GemRB's view origin is 0 here).
const char* kLightFs = R"(#version 130
uniform sampler2D u_scene;
uniform sampler2D u_height;
uniform sampler2D u_normalMap;
uniform vec4 u_map;       // area = phys * xy + zw
uniform float u_toPhys;   // framebuffer px per light buffer px
uniform vec4 u_lightPos;  // ground x, ground y, height, radius (area px)
uniform vec3 u_lightColor;
uniform int u_hasHeight;
uniform int u_hasNormals;
uniform int u_occlSteps;
uniform float u_occlBias;
uniform float u_occlThickness;
uniform float u_texel;
uniform float u_bump;
uniform float u_normalStrength;
uniform int u_debug;

float luma(ivec2 p) {
  p = clamp(p, ivec2(0), textureSize(u_scene, 0) - 1);
  return dot(texelFetch(u_scene, p, 0).rgb, vec3(0.299, 0.587, 0.114));
}
float lambert(vec3 n, vec3 l) { return clamp((dot(n, l) + 0.4) / 1.4, 0.0, 1.0); }
float heightAt(vec2 area) {
  ivec2 sz = textureSize(u_height, 0);
  ivec2 t = clamp(ivec2(floor(area / max(u_texel, 1.0))), ivec2(0), sz - ivec2(1));
  return texelFetch(u_height, t, 0).r;
}
float occlusion(vec3 P, vec3 Lp) {
  const float T = 1.285714;
  float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  float hits = 0.0;
  float len = length(Lp - P);
  for (int i = 0; i < u_occlSteps; ++i) {
    float s = (float(i) + jitter) / float(u_occlSteps);
    if (s * len < 4.0) continue;
    vec3 Q = mix(P, Lp, s);
    vec2 scr = vec2(Q.x, Q.y - Q.z);
    float hs = heightAt(scr);
    float dQ = -Q.y - Q.z * T;
    float dS = -(scr.y + hs) - hs * T;
    float diff = dQ - dS;
    if (diff > u_occlBias && diff < u_occlThickness) hits += 1.0;
  }
  return 1.0 - clamp(hits / 2.0, 0.0, 1.0);
}
void main() {
  vec2 phys = gl_FragCoord.xy * u_toPhys;
  vec2 m = phys * u_map.xy + u_map.zw;
  float ground = m.y;
  if (u_hasHeight == 1) ground = m.y + heightAt(m);
  float h = max(ground - m.y, 0.0);
  bool vertical = h > 2.0;
  vec3 P = vec3(m.x, ground, h);
  vec3 L = u_lightPos.xyz - P;
  float dist = length(vec3(L.x, L.y / 0.72, L.z)) / u_lightPos.w;
  if (dist >= 1.0) discard;
  float att = 1.0 - dist * dist;
  att *= att;
  if (u_occlSteps > 0) att *= occlusion(P, u_lightPos.xyz);
  vec3 n;
  if (u_hasNormals == 1) {
    vec2 uv = (m / max(u_texel, 1.0) + 0.5) / vec2(textureSize(u_normalMap, 0));
    vec3 nm = texture(u_normalMap, uv).xyz * 2.0 - 1.0;
    float nl = length(nm);
    n = nl > 1e-4 ? nm / nl : vec3(0.0, 0.0, 1.0);
  } else {
    ivec2 rp = ivec2(floor(phys));
    int k = max(1, int(1.0 / abs(u_map.x) + 0.5));
    float gx = (luma(rp + ivec2(k, 0)) - luma(rp - ivec2(k, 0))) * 0.5 * u_bump;
    float gy = (luma(rp - ivec2(0, k)) - luma(rp + ivec2(0, k))) * 0.5 * u_bump;  // area y is down
    n = vertical ? normalize(vec3(-gx, 1.0, gy)) : normalize(vec3(-gx, -gy, 1.0));
  }
  vec3 l = normalize(L);
  float ratio = clamp(lambert(n, l) / max(lambert(vec3(0.0, 0.0, 1.0), l), 0.25), 0.0, 2.5);
  if (u_debug == 1) {
    gl_FragColor = vec4(vertical ? 0.5 : 0.0, att * 0.5, (n.x * 0.5 + 0.5) * att, 1.0);
    return;
  }
  gl_FragColor = vec4(u_lightColor * att * mix(1.0, ratio, u_normalStrength), 1.0);
}
)";

const char* kCompVs = R"(#version 130
void main() { gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0); }
)";

const char* kCompFs = R"(#version 130
uniform sampler2D u_scene;
uniform sampler2D u_light;
uniform vec2 u_vpOrigin;
uniform vec2 u_vpSize;
uniform int u_mode;  // 0 lit, 1/3 light buffer, 2 round trip
uniform float u_keep;
uniform float u_maxLight;
void main() {
  vec2 p = gl_FragCoord.xy - u_vpOrigin;
  vec4 c = texelFetch(u_scene, ivec2(floor(p)), 0);
  if (u_mode == 2) { gl_FragColor = c; return; }
  vec3 l = texture(u_light, p / u_vpSize).rgb;
  if (u_mode == 1 || u_mode == 3) { gl_FragColor = vec4(l, c.a); return; }
  vec3 over = max(l - 1.0, vec3(0.0));
  l = min(l, vec3(1.0)) + over / (1.0 + over / max(u_maxLight - 1.0, 0.001));
  vec3 lit = c.rgb * l;
  float hi = smoothstep(0.72, 1.0, max(c.r, max(c.g, c.b)));
  lit = mix(lit, max(lit, c.rgb), hi * u_keep);
  gl_FragColor = vec4(min(lit, vec3(1.0)), c.a);
}
)";

unsigned Compile(unsigned type, const char* src, const char* what) {
  const auto& gl = g_gl;
  const unsigned s = gl.CreateShader(type);
  gl.ShaderSource(s, 1, &src, nullptr);
  gl.CompileShader(s);
  int ok = 0;
  gl.GetShaderiv(s, kCompileStatus, &ok);
  if (!ok) {
    char buf[1024] = {};
    gl.GetShaderInfoLog(s, sizeof buf - 1, nullptr, buf);
    Log("lighting: %s shader failed: %s", what, buf);
    gl.DeleteShader(s);
    return 0;
  }
  return s;
}

unsigned Link(const char* vs, const char* fs, const char* what) {
  const auto& gl = g_gl;
  const unsigned v = Compile(kVertexShader, vs, what), f = Compile(kFragmentShader, fs, what);
  if (!v || !f) return 0;
  const unsigned p = gl.CreateProgram();
  gl.AttachShader(p, v);
  gl.AttachShader(p, f);
  gl.LinkProgram(p);
  gl.DeleteShader(v);
  gl.DeleteShader(f);
  int ok = 0;
  gl.GetProgramiv(p, kLinkStatus, &ok);
  if (!ok) {
    char buf[1024] = {};
    gl.GetProgramInfoLog(p, sizeof buf - 1, nullptr, buf);
    Log("lighting: %s program failed: %s", what, buf);
    return 0;
  }
  return p;
}

unsigned NewTexture(int filter) {
  const auto& gl = g_gl;
  unsigned t = 0;
  gl.GenTextures(1, &t);
  gl.BindTexture(kTex2D, t);
  gl.TexParameteri(kTex2D, kMinFilter, filter);
  gl.TexParameteri(kTex2D, kMagFilter, filter);
  gl.TexParameteri(kTex2D, kWrapS, kClampEdge);
  gl.TexParameteri(kTex2D, kWrapT, kClampEdge);
  return t;
}

// One-time GL setup (inside the composite: the engine's context is current, state is saved).
bool SetUp() {
  if (g_ready) return true;
  if (g_off) return false;
  g_off = true;  // until everything below succeeded
  if (!engine::EngineProgram(0)) {
    Log("lighting: off, the engine runs without shaders (Baldur.lua Shaders=0)");
    return false;
  }
  const char* ver = reinterpret_cast<const char*>(g_gl.GetString(kVersion));
  g_light_prog = Link(kLightVs, kLightFs, "light");
  g_comp_prog = Link(kCompVs, kCompFs, "composite");
  if (!g_light_prog || !g_comp_prog) {
    Log("lighting: off, shaders did not build (GL %s)", ver ? ver : "?");
    return false;
  }
  auto& gl = g_gl;
  const unsigned lp = g_light_prog, cp = g_comp_prog;
  g_lu = {gl.GetUniformLocation(lp, "u_target"),       gl.GetUniformLocation(lp, "u_scene"),
          gl.GetUniformLocation(lp, "u_height"),       gl.GetUniformLocation(lp, "u_normalMap"),
          gl.GetUniformLocation(lp, "u_map"),          gl.GetUniformLocation(lp, "u_toPhys"),
          gl.GetUniformLocation(lp, "u_lightPos"),     gl.GetUniformLocation(lp, "u_lightColor"),
          gl.GetUniformLocation(lp, "u_hasHeight"),    gl.GetUniformLocation(lp, "u_hasNormals"),
          gl.GetUniformLocation(lp, "u_occlSteps"),    gl.GetUniformLocation(lp, "u_occlBias"),
          gl.GetUniformLocation(lp, "u_occlThickness"), gl.GetUniformLocation(lp, "u_texel"),
          gl.GetUniformLocation(lp, "u_bump"),         gl.GetUniformLocation(lp, "u_normalStrength"),
          gl.GetUniformLocation(lp, "u_debug")};
  g_cu = {gl.GetUniformLocation(cp, "u_scene"),  gl.GetUniformLocation(cp, "u_light"),
          gl.GetUniformLocation(cp, "u_vpOrigin"), gl.GetUniformLocation(cp, "u_vpSize"),
          gl.GetUniformLocation(cp, "u_mode"),   gl.GetUniformLocation(cp, "u_keep"),
          gl.GetUniformLocation(cp, "u_maxLight")};
  gl.ActiveTexture(kTexture0);
  g_scene_tex = NewTexture(kNearest);
  g_light_tex = NewTexture(kLinear);
  gl.GenFramebuffers(1, &g_light_fbo);
  g_off = false;
  g_ready = true;
  Log("lighting: GL ready (%s), programs %u/%u, engine draw program %u", ver ? ver : "?", g_light_prog, g_comp_prog,
      engine::EngineProgram(0));
  return true;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

void LoadArea(const std::string& wed, uint16_t type) {
  g_wed = wed;
  g_area_type = type;
  g_scene = AreaScene();
  g_uploaded = false;
  const std::string dir = std::string(DataDir()) + "\\assets\\lighting\\" + wed + "\\";
  std::vector<uint8_t> buf;
  bool lights_ok = false;
  if (ReadFile(dir + "lights.txt", &buf)) {
    lights_ok = ParseLights(std::string(buf.begin(), buf.end()), g_scene);
    if (!lights_ok) Log("lighting: %s: lights.txt rejected", wed.c_str());
  }
  if (ReadFile(dir + "height.bin", &buf)) ReadMap(buf.data(), buf.size(), "GLH1", 2, g_scene.height);
  if (ReadFile(dir + "normal.bin", &buf)) ReadMap(buf.data(), buf.size(), "GLN1", 3, g_scene.normal);
  if (ReadFile(dir + "ao.bin", &buf)) ReadMap(buf.data(), buf.size(), "GLA1", 1, g_scene.ao);
  g_ambient = Ambient(type, g_set.ambient, &g_scene, g_set.key_light);
  Log("lighting: %s: %zu baked lights, key %d, height %dx%d, normals %d, ao %d, type 0x%04X, ambient %.3f %.3f "
      "%.3f",
      wed.c_str(), g_scene.lights.size(), g_scene.has_key ? 1 : 0, g_scene.height.w, g_scene.height.h,
      g_scene.normal.empty() ? 0 : 1, g_scene.ao.empty() ? 0 : 1, type, g_ambient.r, g_ambient.g, g_ambient.b);
}

void Upload() {
  if (g_uploaded) return;
  g_uploaded = true;
  auto& gl = g_gl;
  gl.ActiveTexture(kTexture0);
  gl.PixelStorei(kUnpackAlign, 1);
  gl.PixelStorei(kUnpackRow, 0);
  auto put = [&](unsigned* tex, const SceneMap& m, int internal, unsigned fmt, unsigned type, const void* data,
                 int filter) {
    if (m.empty()) return;
    if (!*tex) *tex = NewTexture(filter);
    gl.BindTexture(kTex2D, *tex);
    gl.TexImage2D(kTex2D, 0, internal, m.w, m.h, 0, fmt, type, data);
  };
  const std::vector<float> h = HeightFloats(g_scene.height);
  put(&g_height_tex, g_scene.height, kR32F, kRed, kFloat, h.data(), kNearest);
  put(&g_normal_tex, g_scene.normal, kRgb8, kRgb, kUByte, g_scene.normal.data.data(), kLinear);
  put(&g_ao_tex, g_scene.ao, kR8, kRed, kUByte, g_scene.ao.data.data(), kLinear);  // sprite AO: phase 2
}

void EnsureTargets(int w, int h) {
  auto& gl = g_gl;
  gl.ActiveTexture(kTexture0);
  if (w != g_scene_w || h != g_scene_h) {
    gl.BindTexture(kTex2D, g_scene_tex);
    gl.TexImage2D(kTex2D, 0, kRgba8, w, h, 0, kRgba, kUByte, nullptr);
    g_scene_w = w;
    g_scene_h = h;
  }
  const int lw = std::max(1, (w + 1) / 2), lh = std::max(1, (h + 1) / 2);
  if (lw != g_light_w || lh != g_light_h) {
    gl.BindTexture(kTex2D, g_light_tex);
    gl.TexImage2D(kTex2D, 0, kRgba16F, lw, lh, 0, kRgba, kFloat, nullptr);
    gl.BindFramebuffer(kFramebuffer, g_light_fbo);
    gl.FramebufferTexture2D(kFramebuffer, kColorAttach0, kTex2D, g_light_tex, 0);
    const unsigned st = gl.CheckFramebufferStatus(kFramebuffer);
    if (st != kFbComplete) Log("lighting: light buffer %dx%d incomplete (%X)", lw, lh, st);
    g_light_w = lw;
    g_light_h = lh;
  }
}

// Reads the falloff light buffer back (it is bound) and checks every 3rd pixel of each drawn light's
// quad against the CPU model. Overlapping lights add up, so only pixels inside exactly one light count.
void Compare(const ViewMap& vm, float to_phys, bool has_h) {
  std::vector<float> px(size_t(g_light_w) * g_light_h * 4);
  g_gl.PixelStorei(kPackAlign, 4);
  g_gl.ReadPixels(0, 0, g_light_w, g_light_h, kRgba, kFloat, px.data());
  const SceneMap* hm = has_h ? &g_scene.height : nullptr;
  const int steps = has_h && g_set.occlusion ? g_set.occlusion_steps : 0;
  g_cmp_n = g_cmp_bad = g_cmp_lit = 0;
  g_cmp_max = 0;
  for (int y = 0; y < g_light_h; y += 3)
    for (int x = 0; x < g_light_w; x += 3) {
      FalloffPixel want{}, f{};
      int inside = 0;
      for (size_t i = 0; i < g_scene.lights.size() && inside < 2; ++i) {
        Rect r;
        if (!FootprintInBuffer(vm, g_scene.lights[i], to_phys, g_light_w, g_light_h, &r)) continue;
        if (x + 0.5f < r.x0 || x + 0.5f > r.x1 || y + 0.5f < r.y0 || y + 0.5f > r.y1) continue;
        if (DebugFalloffAt(vm, to_phys, x, y, g_scene.lights[i], hm, steps, &f)) {
          want = f;
          ++inside;
        }
      }
      if (inside != 1) continue;
      const float* p = &px[(size_t(y) * g_light_w + x) * 4];
      const float err = std::max(std::fabs(p[0] - want.red), std::fabs(p[1] - want.green));
      ++g_cmp_n;
      if (want.green > 0.01f) ++g_cmp_lit;
      if (err > 0.02f) ++g_cmp_bad;
      g_cmp_max = std::max(g_cmp_max, err);
    }
  g_cmp_req = false;
  g_cmp_done = true;
  Log("lighting: gpu/cpu falloff compare: %d px, %d lit, %d differ > 0.02, max error %.4f", g_cmp_n, g_cmp_lit,
      g_cmp_bad, g_cmp_max);
}

void LightPass(const ViewMap& vm, float to_phys) {
  auto& gl = g_gl;
  gl.BindFramebuffer(kFramebuffer, g_light_fbo);
  gl.Viewport(0, 0, g_light_w, g_light_h);
  const bool dbg = g_mode == DebugMode::Falloff;
  if (dbg) gl.ClearColor(0, 0, 0, 1);
  else gl.ClearColor(g_ambient.r, g_ambient.g, g_ambient.b, 1);
  gl.Clear(kColorBit);
  g_drawn = 0;
  if (!g_set.baked_lights || g_scene.lights.empty()) return;

  const bool has_h = g_set.height_map && g_height_tex && !g_scene.height.empty();
  const bool has_n = g_set.area_normals && g_normal_tex && !g_scene.normal.empty();
  gl.UseProgram(g_light_prog);
  gl.Uniform2f(g_lu.target, float(g_light_w), float(g_light_h));
  gl.Uniform1i(g_lu.scene, 0);
  gl.Uniform1i(g_lu.height, 2);
  gl.Uniform1i(g_lu.normal, 3);
  gl.Uniform4f(g_lu.map, vm.ax, vm.ay, vm.bx, vm.by);
  gl.Uniform1f(g_lu.to_phys, to_phys);
  gl.Uniform1i(g_lu.has_height, has_h ? 1 : 0);
  gl.Uniform1i(g_lu.has_normals, has_n ? 1 : 0);
  gl.Uniform1i(g_lu.occl_steps, has_h && g_set.occlusion ? g_set.occlusion_steps : 0);
  gl.Uniform1f(g_lu.occl_bias, 2.0f);
  gl.Uniform1f(g_lu.occl_thick, 48.0f);
  gl.Uniform1f(g_lu.texel, float(g_scene.height.texel > 0 ? g_scene.height.texel : 2));
  gl.Uniform1f(g_lu.bump, 6.0f);
  gl.Uniform1f(g_lu.normal_strength, g_set.normal_strength);
  gl.Uniform1i(g_lu.debug, dbg ? 1 : 0);
  gl.ActiveTexture(kTexture0 + 0);
  gl.BindTexture(kTex2D, g_scene_tex);
  gl.ActiveTexture(kTexture0 + 2);
  gl.BindTexture(kTex2D, has_h ? g_height_tex : 0);
  gl.ActiveTexture(kTexture0 + 3);
  gl.BindTexture(kTex2D, has_n ? g_normal_tex : 0);
  gl.ActiveTexture(kTexture0);
  gl.Enable(kBlend);
  gl.BlendEquationSeparate(kFuncAdd, kFuncAdd);
  gl.BlendFuncSeparate(kOne, kOne, kOne, kOne);

  const float amb_luma = Luma(g_ambient);
  const double t = double(GetTickCount64());
  for (size_t i = 0; i < g_scene.lights.size() && g_drawn < g_set.budget; ++i) {
    const BakedLight& l = g_scene.lights[i];
    Rect r;
    if (!FootprintInBuffer(vm, l, to_phys, g_light_w, g_light_h, &r)) continue;
    const float k = LightScale(l, g_set.light_gain, Flicker(l.kind, i, t), amb_luma);
    gl.Uniform4f(g_lu.light_pos, l.x, l.y, l.z, l.radius);
    gl.Uniform3f(g_lu.light_color, l.r * k, l.g * k, l.b * k);
    gl.Begin(kQuads);
    gl.Vertex2f(r.x0, r.y0);
    gl.Vertex2f(r.x1, r.y0);
    gl.Vertex2f(r.x1, r.y1);
    gl.Vertex2f(r.x0, r.y1);
    gl.End();
    ++g_drawn;
  }
  gl.Disable(kBlend);
  if (g_cmp_req && dbg) Compare(vm, to_phys, has_h);
}

void ReadBack(int x, int y, int w, int h, std::vector<uint8_t>* out) {
  out->resize(size_t(w) * h * 4);
  g_gl.PixelStorei(kPackAlign, 1);
  g_gl.ReadPixels(x, y, w, h, kRgba, kUByte, out->data());
}

// The composite (engine/gl_world.h): once per frame, after all tiles and objects, before fog.
void Composite() {
  if (g_mode == DebugMode::Off || g_off) return;
  const uint64_t area = engine::WorldPassArea();
  if (!area) return;
  engine::EngineFlushBatch();
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    const std::string err = engine::LoadGl(&g_gl);
    if (!err.empty()) {
      Log("lighting: off, GL %s", err.c_str());
      g_off = true;
      return;
    }
  }
  auto& gl = g_gl;
  const unsigned pending = gl.GetError();  // an error the engine left: not ours, cleared so ours show
  engine::GlState saved;
  engine::SaveGlState(gl, &saved);
  if (!g_ready && !SetUp()) {
    engine::RestoreGlState(gl, saved);
    return;
  }

  const std::string wed = engine::AreaWed(area);
  if (!wed.empty() && wed != g_wed) LoadArea(wed, engine::AreaTypeFlags(area));
  Upload();

  const int vx = saved.viewport[0], vy = saved.viewport[1], vw = saved.viewport[2], vh = saved.viewport[3];
  if (vw <= 0 || vh <= 0 || vw > 16384 || vh > 16384) {
    engine::RestoreGlState(gl, saved);
    return;
  }
  float st[4], ox, oy;
  engine::WorldMatrix(st);
  engine::WorldPassOffset(&ox, &oy);
  if (st[0] == 0 || st[1] == 0) {
    engine::RestoreGlState(gl, saved);
    return;
  }
  const ViewMap vm = MakeViewMap(vw, vh, st, ox, oy);
  const bool check = g_check_req;
  if (check) {
    gl.BindFramebuffer(0x8CA8 /*read*/, static_cast<unsigned>(saved.draw_fbo));
    ReadBack(vx, vy, vw, vh, &g_before);
  }

  for (unsigned cap : {kScissor, kDepth, kCull, kStencil, kAlphaTest, kDither, kBlend}) gl.Disable(cap);
  gl.ColorMask(1, 1, 1, 1);
  EnsureTargets(vw, vh);
  // 1. the scene so far (current framebuffer, viewport rect) into our texture
  gl.BindFramebuffer(0x8CA8 /*read*/, static_cast<unsigned>(saved.draw_fbo));
  gl.ActiveTexture(kTexture0);
  gl.BindTexture(kTex2D, g_scene_tex);
  gl.CopyTexSubImage2D(kTex2D, 0, 0, 0, vx, vy, vw, vh);
  // 2. light buffer (skipped for the round trip)
  const float to_phys = float(vw) / float(g_light_w);
  if (g_mode != DebugMode::RoundTrip) LightPass(vm, to_phys);
  // 3. composite back into the engine's target
  gl.BindFramebuffer(kFramebuffer, static_cast<unsigned>(saved.draw_fbo));
  gl.Viewport(vx, vy, vw, vh);
  gl.Disable(kBlend);
  gl.UseProgram(g_comp_prog);
  gl.Uniform1i(g_cu.scene, 0);
  gl.Uniform1i(g_cu.light, 1);
  gl.Uniform2f(g_cu.vp_origin, float(vx), float(vy));
  gl.Uniform2f(g_cu.vp_size, float(vw), float(vh));
  gl.Uniform1i(g_cu.mode, static_cast<int>(g_mode));
  gl.Uniform1f(g_cu.keep, g_set.keep);
  gl.Uniform1f(g_cu.max_light, g_set.max_light);
  gl.ActiveTexture(kTexture0 + 1);
  gl.BindTexture(kTex2D, g_light_tex);
  gl.ActiveTexture(kTexture0);
  gl.BindTexture(kTex2D, g_scene_tex);
  gl.Begin(kQuads);
  gl.Vertex2f(-1, -1);
  gl.Vertex2f(1, -1);
  gl.Vertex2f(1, 1);
  gl.Vertex2f(-1, 1);
  gl.End();

  if (check) {
    gl.BindFramebuffer(0x8CA8 /*read*/, static_cast<unsigned>(saved.draw_fbo));
    ReadBack(vx, vy, vw, vh, &g_after);
    long long diff = 0;
    for (size_t i = 0; i < g_before.size(); i += 4)
      if (g_before[i] != g_after[i] || g_before[i + 1] != g_after[i + 1] || g_before[i + 2] != g_after[i + 2] ||
          g_before[i + 3] != g_after[i + 3])
        ++diff;
    g_check_diff = diff;
    g_check_total = static_cast<long long>(g_before.size() / 4);
    g_check_done = true;
    g_check_req = false;
    Log("lighting: check (%s) %dx%d at %d,%d: %lld of %lld pixels differ", DebugName(g_mode), vw, vh, vx, vy, diff,
        g_check_total);
    g_before.clear();
    g_after.clear();
  }
  const unsigned err = gl.GetError();
  engine::RestoreGlState(gl, saved);
  static int errors_logged = 0;
  if (++g_frames == 1 || ((err || pending) && errors_logged < 5)) {
    if (err || pending) ++errors_logged;
    Log("lighting: frame %llu: viewport %d,%d %dx%d fbo %d, uST %.5f %.5f %.3f %.3f, off %.0f,%.0f, lights %d, "
        "GL error ours %X engine %X",
        static_cast<unsigned long long>(g_frames), vx, vy, vw, vh, saved.draw_fbo, st[0], st[1], st[2], st[3], ox,
        oy, g_drawn, err, pending);
  }
}

void PreDraw() { engine::ArmWorldComposite(); }

// --- Lua probes ------------------------------------------------------------------------------------
// PSTF_LightDebug(mode) -> mode name. 0 lit, 1 light buffer, 2 round trip, 3 falloff, 4 off.
int __cdecl LuaDebug(engine::LuaState* L) {
  const int m = static_cast<int>(engine::LuaNumberArg(L, 1, -1));
  if (m >= 0 && m <= 4) g_mode = static_cast<DebugMode>(m);
  Log("lua: PSTF_LightDebug(%d) -> %s", m, DebugName(g_mode));
  engine::LuaPushString(L, DebugName(g_mode));
  return 1;
}

// PSTF_LightState() -> "area=AR0202 lights=27 drawn=9 height=1 normals=1 ao=1 mode=lit frames=120 ready=1".
int __cdecl LuaState(engine::LuaState* L) {
  char b[256];
  std::snprintf(b, sizeof b, "area=%s lights=%zu drawn=%d height=%d normals=%d ao=%d key=%d mode=%s frames=%llu ready=%d",
                g_wed.empty() ? "-" : g_wed.c_str(), g_scene.lights.size(), g_drawn, g_scene.height.empty() ? 0 : 1,
                g_scene.normal.empty() ? 0 : 1, g_scene.ao.empty() ? 0 : 1, g_scene.has_key ? 1 : 0,
                DebugName(g_mode), static_cast<unsigned long long>(g_frames), g_ready ? 1 : 0);
  Log("lua: PSTF_LightState -> %s", b);
  engine::LuaPushString(L, b);
  return 1;
}

// PSTF_LightCheck(): on the next composite, read the world back before and after our pass and count
// the pixels that changed (round trip must be 0; lit must not).
int __cdecl LuaCheck(engine::LuaState* L) {
  g_check_req = true;
  g_check_done = false;
  Log("lua: PSTF_LightCheck()");
  engine::LuaPushNumber(L, 1);
  return 1;
}

// PSTF_LightCheckResult() -> done (0/1), differing pixels, total pixels.
int __cdecl LuaCheckResult(engine::LuaState* L) {
  engine::LuaPushNumber(L, g_check_done ? 1 : 0);
  engine::LuaPushNumber(L, static_cast<double>(g_check_diff));
  engine::LuaPushNumber(L, static_cast<double>(g_check_total));
  return 3;
}

// PSTF_LightCompare(): in falloff mode (3), on the next frame compare the GPU light buffer with the CPU
// model. PSTF_LightCompareResult() -> done, pixels compared, pixels lit, pixels off by > 0.02.
int __cdecl LuaCompare(engine::LuaState* L) {
  g_cmp_req = true;
  g_cmp_done = false;
  Log("lua: PSTF_LightCompare()");
  engine::LuaPushNumber(L, 1);
  return 1;
}
int __cdecl LuaCompareResult(engine::LuaState* L) {
  engine::LuaPushNumber(L, g_cmp_done ? 1 : 0);
  engine::LuaPushNumber(L, g_cmp_n);
  engine::LuaPushNumber(L, g_cmp_lit);
  engine::LuaPushNumber(L, g_cmp_bad);
  return 4;
}

// PSTF_LightShot(path) -> "" or the error: saves the last presented frame (half-size BMP).
int __cdecl LuaShot(engine::LuaState* L) {
  const std::string path = engine::LuaStringArg(L, 1);
  const std::string err = SaveScreenshot(path);
  Log("lua: PSTF_LightShot(%s) -> %s", path.c_str(), err.empty() ? "ok" : err.c_str());
  engine::LuaPushString(L, err.c_str());
  return 1;
}

}  // namespace

void InstallLighting(const IniMap& ini) {
  if (!IniBool(ini, "lighting.enabled", false)) {
    Log("lighting: disabled in ini");
    return;
  }
  auto pct = [&](const char* key, int def, int lo, int hi) {
    return std::max(lo, std::min(hi, IniInt(ini, key, def))) / 100.0f;
  };
  g_set.baked_lights = IniBool(ini, "lighting.baked_lights", true);
  g_set.height_map = IniBool(ini, "lighting.height_map", true);
  g_set.occlusion = IniBool(ini, "lighting.occlusion", true);
  g_set.occlusion_steps = std::max(4, std::min(32, IniInt(ini, "lighting.occlusion_steps", 16)));
  g_set.area_normals = IniBool(ini, "lighting.area_normals", true);
  g_set.key_light = IniBool(ini, "lighting.key_light", true);
  g_set.light_gain = pct("lighting.light_gain", 100, 0, 200);
  g_set.ambient = pct("lighting.ambient", 100, 0, 200);
  g_set.normal_strength = pct("lighting.normal_strength", 80, 0, 100);
  g_set.max_light = pct("lighting.max_light", 125, 100, 300);
  g_set.keep = pct("lighting.highlight_keep", 60, 0, 100);
  int dbg = IniInt(ini, "lighting.debug", 0);
  if (const char* e = std::getenv("PSTF_LIGHT_DEBUG")) dbg = std::atoi(e);
  if (dbg >= 0 && dbg <= 4) g_mode = static_cast<DebugMode>(dbg);

  if (!engine::InstallWorldComposite(&Composite) || !engine::InstallWorldPreDraw(&PreDraw)) {
    Log("lighting: off, world pass sites do not match");
    return;
  }
  dev::AddLuaFunction("PSTF_LightDebug", &LuaDebug);
  dev::AddLuaFunction("PSTF_LightState", &LuaState);
  dev::AddLuaFunction("PSTF_LightCheck", &LuaCheck);
  dev::AddLuaFunction("PSTF_LightCheckResult", &LuaCheckResult);
  dev::AddLuaFunction("PSTF_LightShot", &LuaShot);
  dev::AddLuaFunction("PSTF_LightCompare", &LuaCompare);
  dev::AddLuaFunction("PSTF_LightCompareResult", &LuaCompareResult);
  Log("lighting: installed (mode %s, gain %.2f, ambient %.2f, occlusion %d x%d, normals %d)", DebugName(g_mode),
      g_set.light_gain, g_set.ambient, g_set.occlusion ? 1 : 0, g_set.occlusion_steps, g_set.area_normals ? 1 : 0);
}

}  // namespace pstf::lighting
