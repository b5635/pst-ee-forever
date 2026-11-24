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
#include <sstream>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/shot.h"
#include "engine/frame.h"
#include "engine/gl_world.h"
#include "engine/lua.h"
#include "engine/combat.h"
#include "engine/render.h"
#include "engine/sprites.h"
#include "modules/dev/lua_bridge.h"
#include "modules/lighting/lighting_logic.h"
#include "modules/lighting/sprite_depth.h"

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
  // phase 4
  bool emissive = true, bloom = true, fx_lights = true, fx_soften = true, untinted_fx = true, self_lit = true;
  float emissive_keep = 0.8f, bloom_strength = 0.35f, bloom_threshold = 0.45f, fx_light_gain = 1.0f;
  float fx_boost = 0.15f, fx_fringe = 1.5f, fx_light_saturation = 1.6f;  // additive boost of glowing pixels, bloom colour fringe (px)
  int bloom_passes = 2;                       // blur iterations: 1 tight, 2+ a softer, wider halo
  int fx_budget = 24;
  // phase 5
  bool atmosphere = true;
  float atmosphere_strength = 1.0f;
  // hand lights (vfxlight.tsv) and buff colour lights
  bool hand_lights = true, buff_lights = true;
  // phase 2: sprite buffer (depth maps), LM as the sprites' ambient, sprite occlusion, key-light shadows
  bool sprite_lighting = true, sprite_occlusion = true, key_shadows = true;
  float sprite_normal_strength = 0.6f, sprite_light_gain = 1.0f, key_shadow_strength = 0.8f,
        key_shadow_length = 0.55f;
  float weapon_glow_radius = 70.0f, weapon_glow_gain = 0.3f;
  // our own contact shadow under every creature (replaces the BAM blob when bam_shadows = false)
  bool contact_shadows = true, bam_shadows = false;
  float contact_shadow_strength = 0.7f, contact_shadow_size = 22.0f, contact_shadow_flat = 0.5f;
  float sprite_shadow_strength = 1.0f;  // how much a creature blocks a dynamic light (sprite occlusion)
  // day / night: undo the engine's time-of-day tint, our own ambient for the time of day
  bool day_night = true;
  DayNight dn;
  float hand_light_gain = 1.5f, buff_light_gain = 0.7f;
  // sprite AO (ao.bin: creatures in corners and under arches lose part of their ambient), painted emitter
  // glow (the bright pixels of a baked light's painted flame are emissive: kept bright, bloom), hand area
  // lights (arealights.tsv)
  float sprite_ao = 0.6f, painted_glow_gain = 1.0f, painted_core = 0.2f;
  bool painted_glow = true, area_lights = true;
};
Settings g_set;
DebugMode g_mode = DebugMode::Lit;

engine::Gl g_gl = {};
bool g_ready = false, g_off = false;  // GL set up / turned itself off
unsigned g_light_prog = 0, g_comp_prog = 0, g_fill_prog = 0, g_post_prog = 0, g_paint_prog = 0;
unsigned g_scene_tex = 0, g_light_tex = 0, g_light_fbo = 0, g_height_tex = 0, g_normal_tex = 0, g_ao_tex = 0;
unsigned g_emis_tex = 0, g_emis_fbo = 0, g_bloom_tex[2] = {}, g_bloom_fbo[2] = {};
int g_scene_w = 0, g_scene_h = 0, g_light_w = 0, g_light_h = 0;

struct LightUni {
  int target, scene, height, normal, map, to_phys, light_pos, light_color, has_height, has_normals, occl_steps,
      occl_bias, occl_thick, texel, bump, normal_strength, debug, info, cov, sprites, sprite_occl, vp_origin,
      sprite_gain, owner, sprite_shadow;
} g_lu = {};
struct CompUni {
  int scene, light, vp_origin, vp_size, mode, keep, max_light, emis, bloom, height, map, texel, has_height,
      emis_keep, bloom_strength, atmo, haze_colour, fx_boost, fringe, info, cov, shadow, sprites, ambient,
      shadow_strength, cov_valid, engine_tint, lm, lm_pool, lm_mean, lm_area, ao, sprite_ao, ao_texel, fxm,
      fxm_valid;
} g_cu = {};
struct PaintUni {
  int target, scene, vp_origin, map, emitter, gain, engine_tint;
} g_pnu = {};
std::vector<AreaLight> g_area_lights;  // assets/lighting/arealights.tsv
size_t g_area_added = 0;               // hand area lights in the current area
int g_painted = 0;                     // probe: painted emitters drawn into the emissive buffer this frame
bool g_sprmean_req = false;            // probe: mean luma of the lit creature pixels on the next frame
double g_sprmean = -1;
long long g_sprmean_px = 0;
struct FillUni {
  int target;
} g_fu = {};
struct PostUni {
  int mode, src, emis, src_size, dir, threshold, to_phys, target, vp_origin;
} g_pu = {};

// Phase 4: the effect table and this frame's glow draws.
VfxTable g_vfx;
std::vector<GlowDraw> g_glows;
// Emitters beyond the additive blend: effect BAMs named by the exe's effect code or by effect data
// (assets/vfx/vfxemit.txt from tools/vfxscan.py). Packed upper-case names.
std::vector<uint64_t> g_emit_names;
std::vector<HandLight> g_hand;   // assets/vfx/vfxlight.tsv
uint64_t g_last_area = 0;  // area of the last composite (probes, game thread)
bool g_has_creature_lights = false;  // g_hand has "anim:N" rows
float g_cl_x = -1, g_cl_y = -1, g_cl_r = 0;  // probe: last creature light (area px, red channel)
std::vector<std::pair<float, float>> g_noshadow;  // feet of "noshadow" creatures this frame (area px)
std::vector<std::pair<float, float>> g_cl_trace;  // creature light position per drawn frame (probe)
std::vector<std::pair<float, float>> g_cl_trace_anchor;  // its sprite draw anchor per drawn frame (probe)
int g_cl_trace_left = 0;
bool NoShadowAt(float x, float y) {
  for (const auto& p : g_noshadow)
    if (std::fabs(p.first - x) < 6.0f && std::fabs(p.second - y) < 6.0f) return true;
  return false;
}
// The sprite draw whose feet are nearest (x, y) within 48 px (draw space + offset): the creature's drawn feet.
void NearestFeet(const engine::EngineSpriteDraw* d, int n, float ox, float oy, float* x, float* y) {
  float best = 48.0f * 48.0f, bx = *x, by = *y;
  for (int i = 0; d && i < n; ++i) {
    const float dx = float(d[i].anchor_x) + ox - *x, dy = float(d[i].anchor_y) + oy - *y;
    if (dx * dx + dy * dy < best) {
      best = dx * dx + dy * dy;
      bx = float(d[i].anchor_x) + ox;
      by = float(d[i].anchor_y) + oy;
    }
  }
  *x = bx;
  *y = by;
}
std::vector<GlowOverride> g_glow_over;  // assets/vfx/vfxglow.tsv
// Buff colour lights: first time each colour effect was seen (age for the fade), pruned each frame.
std::vector<std::pair<uint64_t, double>> g_color_seen;
int g_hand_lights = 0, g_buff_lights = 0;  // probe: this frame
// Emissive capture (engine/gl_world.h SetEmissiveCallbacks): this frame's buffer state.
bool g_cap_used = false;     // something was captured this frame (cleared at the pre-draw)
int g_cap_w = 0, g_cap_h = 0;
int g_cap_prev_fbo = 0;
uint64_t g_cap_draws = 0;    // probe: captured draws since start
bool g_cov_req = false;      // probe: measure the capture's coverage on the next frame with emitters
double g_cov = -1;           // lit capture pixels / sum of the emitting draws' frame rects (px)
int g_fx_lights = 0, g_fx_hits = 0, g_fx_glows = 0;
uint64_t g_fx_frames_with_light = 0;
std::vector<BakedLight> g_last_fx;  // probe: this frame's effect lights
ViewMap g_last_vm;                  // probe: last frame's mapping
float g_last_ox = 0, g_last_oy = 0;  // probe: last composite's draw -> area offset
int g_last_vw = 0, g_last_vh = 0, g_last_vx = 0, g_last_vy = 0;
float g_bloom_max = 0;              // probe: brightest bloom texel of the last frame with glow draws
bool g_bloom_req = false;

// Phase 2: sprite buffer.
unsigned g_info_prog = 0, g_shadow_prog = 0;
unsigned g_info_tex = 0, g_info_fbo = 0, g_cov_tex = 0, g_cov_fbo = 0, g_shadow_tex = 0, g_shadow_fbo = 0;
int g_info_w = 0, g_info_h = 0, g_cov_w = 0, g_cov_h = 0, g_cov_prev_fbo = 0;
bool g_cov_used = false;     // a creature's coverage was captured this frame
// Effect mask (window px): rgb = what untinted draws added, a = normal-blend untinted draws' alpha.
unsigned g_fxm_tex = 0, g_fxm_fbo = 0;
int g_fxm_w = 0, g_fxm_h = 0, g_fxm_prev_fbo = 0, g_fxm_mask[4] = {1, 1, 1, 1};
bool g_fxm_used = false;
uint64_t g_fxm_draws = 0;
int g_cov_mask[4] = {1, 1, 1, 1};  // the engine's colour write mask around a coverage draw
bool g_sprites_valid = false;  // the info buffer holds this frame's creatures
struct InfoUni {
  int target, depth, map, rect, feet, mirror, lm, strength;
} g_iu = {};
struct ShadowUni {
  int target, cov, map, vp_origin, to_phys, src, feet, dir, k, height, texel, has_height, mode;
} g_su = {};
bool g_shadow_drawn = false;  // the shadow buffer holds something this frame (key or contact shadows)
int g_contact_drawn = 0;      // probe: contact shadows this frame
std::vector<uint64_t> g_dep_names;  // sorted packed names of the .DEP files in assets\sprites\depth
struct DepEntry {
  uint64_t name = 0;
  DepFile file;
  bool ok = false;
  uint64_t used = 0;
};
std::vector<DepEntry> g_deps;  // loaded depth files (LRU, 48)
struct DepTex {
  uint64_t name = 0;
  int index = -1;
  unsigned tex = 0;
  int w = 0, h = 0;
  uint64_t used = 0;
};
std::vector<DepTex> g_dep_tex;  // uploaded frames (LRU, 384)
int g_spr_drawn = 0, g_spr_depth = 0, g_spr_seen = 0;  // probe: this frame
float g_spr_lm_sum = 0;
uint64_t g_spr_frames = 0;
bool g_spr_check_req = false;  // probe: read the buffers back on the next frame with creatures
long long g_spr_info_px = -1, g_spr_cov_px = -1, g_spr_both_px = -1;

// Current area.
std::string g_wed;
AreaScene g_scene;
uint16_t g_area_type = 0;
bool g_uploaded = false;
Rgb g_ambient;                     // the area's day ambient (Ambient)
Rgb g_amb{1, 1, 1};                // this frame's ambient (time of day: DayNightFrame)
Rgb g_engine_tint{1, 1, 1};        // the engine's global tint this frame (undone in the composite)
float g_night = 0, g_night_gain = 1;
uint64_t g_lm_area = 0;            // area whose lightmap is in g_lm_tex
unsigned g_lm_tex = 0;
int g_lm_w = 0, g_lm_h = 0;
float g_lm_mean = 0;
bool g_lm_ok = false;

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
uniform sampler2D u_info;  // sprite buffer (framebuffer px, viewport sized; G < 0 = none)
uniform sampler2D u_cov;   // creature coverage (window px)
uniform int u_sprites;
uniform int u_spriteOccl;
uniform vec2 u_vpOrigin;
uniform float u_spriteGain;
uniform float u_spriteShadow;  // how much a creature blocks the light (sprite_shadow_strength)
uniform vec3 u_owner;  // owned light: owner x, feet y, 1 (lighting_logic OwnerSprite); 2 = unblocked; 0 none

bool spriteAt(ivec2 ip, out vec4 inf) {
  inf = vec4(0.0, -1.0, 0.0, 0.0);
  if (u_sprites == 0) return false;
  ivec2 sz = textureSize(u_info, 0);
  if (ip.x < 0 || ip.y < 0 || ip.x >= sz.x || ip.y >= sz.y) return false;
  inf = texelFetch(u_info, ip, 0);
  if (inf.g < 0.0) return false;
  ivec2 cp = ip + ivec2(u_vpOrigin);
  ivec2 cs = textureSize(u_cov, 0);
  if (cp.x >= cs.x || cp.y >= cs.y) return false;
  return texelFetch(u_cov, cp, 0).a > 0.5;
}

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
float occlusion(vec3 P, vec3 Lp, float selfFeet) {
  const float T = 1.285714;
  float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  float hits = 0.0, shits = 0.0;
  float len = length(Lp - P);
  for (int i = 0; i < u_occlSteps; ++i) {
    float s = (float(i) + jitter) / float(u_occlSteps);
    if (s * len < 4.0) continue;
    vec3 Q = mix(P, Lp, s);
    vec2 scr = vec2(Q.x, Q.y - Q.z);
    if (u_hasHeight == 1) {
      float hs = heightAt(scr);
      float dQ = -Q.y - Q.z * T;
      float dS = -(scr.y + hs) - hs * T;
      float diff = dQ - dS;
      if (diff > u_occlBias && diff < u_occlThickness) hits += 1.0;
    }
    // Creatures block light: the screen point of Q is a sprite pixel whose feet are on Q's ground line,
    // so that pixel stands exactly Q.z above the floor there (not the creature the ray starts from).
    if (u_spriteOccl == 1) {
      vec4 si;
      if (spriteAt(ivec2(floor((scr - u_map.zw) / u_map.xy)), si)) {
        float sf = scr.y + si.r;
        bool owner = u_owner.z > 1.5 || (u_owner.z > 0.5 && abs(sf - u_owner.y) < 8.0 && abs(Q.x - u_owner.x) < 40.0);
        if (abs(sf - Q.y) < 10.0 && abs(sf - selfFeet) > 4.0 && !owner) shits += 1.0;
      }
    }
  }
  return (1.0 - clamp(hits / 2.0, 0.0, 1.0)) * (1.0 - clamp(shits / 2.0, 0.0, 1.0) * u_spriteShadow);
}
void main() {
  vec2 phys = gl_FragCoord.xy * u_toPhys;
  vec2 m = phys * u_map.xy + u_map.zw;
  vec4 si;
  if (spriteAt(ivec2(floor(phys)), si)) {
    // Sprite pixel: stands si.r above its feet line, si.g's depth toward the camera, normal from the map.
    float lmq = floor(si.g / 64.0);
    float dep = si.g - lmq * 64.0;
    float feet = m.y + si.r;
    vec3 Ps = vec3(m.x, feet + dep, si.r);
    vec3 Ls = u_lightPos.xyz - Ps;
    float ds = length(vec3(Ls.x, Ls.y / 0.72, Ls.z)) / u_lightPos.w;
    if (ds >= 1.0) discard;
    float as = 1.0 - ds * ds;
    as *= as;
    if (u_occlSteps > 0) as *= occlusion(vec3(m.x, feet, si.r), u_lightPos.xyz, feet);
    vec3 ns = vec3(si.b, sqrt(max(0.0, 1.0 - si.b * si.b - si.a * si.a)), si.a);
    float lam = lambert(ns, normalize(Ls));
    if (u_debug == 1) {
      gl_FragColor = vec4(1.0, as * 0.5, (ns.x * 0.5 + 0.5) * as, 1.0);
      return;
    }
    gl_FragColor = vec4(u_lightColor * as * lam * u_spriteGain, 1.0);
    return;
  }
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
  if (u_occlSteps > 0) att *= occlusion(P, u_lightPos.xyz, -1e9);
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

// Composite: lit = scene x light (soft shoulder, highlight keep); glow pixels keep their own brightness
// (emissive keep); atmosphere by height; bloom of the glow draws added screen-ish (GemRB PostFS).
const char* kCompFs = R"(#version 130
uniform sampler2D u_scene;
uniform sampler2D u_light;
uniform sampler2D u_emis;    // emissive capture: the emitting draws over black, window px
uniform sampler2D u_bloom;   // blurred bright pass of the glow draws
uniform sampler2D u_height;  // R32F, area px
uniform vec2 u_vpOrigin;
uniform vec2 u_vpSize;
uniform int u_mode;  // 0 lit, 1/3 light buffer, 2 round trip, 5 emissive + bloom
uniform float u_keep;
uniform float u_maxLight;
uniform vec4 u_map;      // area = phys * xy + zw
uniform float u_texel;
uniform int u_hasHeight;
uniform float u_emisKeep;
uniform float u_bloomStrength;
uniform vec4 u_atmo;     // haze, desat, contrast, strength (0 = off)
uniform vec3 u_hazeColour;
uniform sampler2D u_info;    // sprite buffer (viewport px)
uniform sampler2D u_cov;     // creature coverage (window px)
uniform sampler2D u_shadow;  // key-light shadows (light-buffer px)
uniform int u_sprites;
uniform vec3 u_ambient;
uniform float u_shadowStrength;
uniform int u_covValid;  // the coverage buffer was drawn this frame
uniform float u_fxBoost;   // additive boost of glowing pixels
uniform float u_fringe;    // bloom colour fringe, light-buffer px
uniform vec3 u_engineTint; // the engine's global (time-of-day) tint: divided back out (lighting_logic UndoTint)
uniform sampler2D u_lm;    // area lightmap, one texel per 16x12 area px
uniform float u_lmPool;    // night light of the lightmap's bright pools (0 = off; LightmapPool k)
uniform float u_lmMean;    // the lightmap's mean luma
uniform vec2 u_lmArea;     // area px the lightmap texture covers
uniform sampler2D u_ao;    // ao.bin (R8, 1 = open), area px / u_aoTexel
uniform float u_spriteAO;  // sprite_ao strength (0 = off)
uniform float u_aoTexel;
uniform sampler2D u_fxm;   // effect mask: rgb what untinted draws added, a their alpha (window px)
uniform int u_fxmValid;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
// lighting_logic EmissiveWeight: brightness ramp raised by saturation.
float emisWeight(vec3 c) {
  float m = max(c.r, max(c.g, c.b)), n = min(c.r, min(c.g, c.b));
  float sat = m > 1e-4 ? (m - n) / m : 0.0;
  return smoothstep(0.08, 0.6, m) * (0.55 + 0.45 * sat);
}
float heightAt(vec2 area) {
  ivec2 sz = textureSize(u_height, 0);
  ivec2 t = clamp(ivec2(floor(area / max(u_texel, 1.0))), ivec2(0), sz - ivec2(1));
  return texelFetch(u_height, t, 0).r;
}
void main() {
  vec2 p = gl_FragCoord.xy - u_vpOrigin;
  vec4 c = texelFetch(u_scene, ivec2(floor(p)), 0);
  if (u_mode == 2) { gl_FragColor = c; return; }
  vec2 uv = p / u_vpSize;
  vec3 l = texture(u_light, uv).rgb;
  if (u_mode == 1 || u_mode == 3) { gl_FragColor = vec4(l, c.a); return; }
  vec4 ecs = texelFetch(u_emis, ivec2(floor(gl_FragCoord.xy)), 0);
  vec3 ec = ecs.rgb * step(0.0001, u_emisKeep);
  // a.k.a. EffectCover; the capture's alpha = a self-lit creature (Ignus) drawn there: all of it unlit
  float fxCover = max(smoothstep(0.01, 0.12, max(ec.r, max(ec.g, ec.b))), ecs.a);
  // albedo: the engine's night tint out, except where the engine drew without it (lighting_logic UntintedCover,
  // UndoTintFx): most spell BAMs are untinted; undone they turned blue and blew up to opaque blobs at night
  ivec2 fq = ivec2(floor(gl_FragCoord.xy));
  ivec2 fz = textureSize(u_fxm, 0);
  vec4 fm = (u_fxmValid == 1 && fq.x < fz.x && fq.y < fz.y) ? texelFetch(u_fxm, fq, 0) : vec4(0.0);
  float untinted = max(sqrt(clamp(fm.a, 0.0, 1.0)), smoothstep(0.01, 0.12, max(fm.r, max(fm.g, fm.b))));
  vec3 drawn = c.rgb;  // what the engine drew: an untinted effect's pixels are shown as that (unlit)
  c.rgb = min(c.rgb / max(u_engineTint, vec3(0.2)), vec3(1.0));  // the rest (and what shows through) is lit
  float e = emisWeight(ec);
  // Colour fringe: red and blue of the halo sampled a little apart along the direction from the centre.
  vec2 fd = normalize(uv - 0.5 + 1e-5) * u_fringe / vec2(textureSize(u_bloom, 0));
  vec3 b = vec3(texture(u_bloom, uv + fd).r, texture(u_bloom, uv).g, texture(u_bloom, uv - fd).b);
  if (u_mode == 5) { gl_FragColor = vec4(e, luma(b) * 4.0, luma(c.rgb) * 0.25, c.a); return; }
  if (u_mode == 7) { gl_FragColor = vec4(max(ec, c.rgb * 0.25), c.a); return; }
  if (u_mode == 8) { gl_FragColor = vec4(untinted, fxCover, luma(c.rgb) * 0.25, c.a); return; }
  vec4 si = texelFetch(u_info, ivec2(floor(p)), 0);
  bool spr = false;
  if (u_sprites == 1 && si.g >= 0.0) {
    ivec2 cp = ivec2(floor(gl_FragCoord.xy));
    ivec2 cs = textureSize(u_cov, 0);
    spr = cp.x < cs.x && cp.y < cs.y && texelFetch(u_cov, cp, 0).a > 0.5;
  }
  if (u_mode == 6) {  // creatures violet (brighter = normal facing right), engine shadow blobs and other
    // coverage-only pixels green, buffer-only red, key shadows yellow, the rest dimmed grey
    float cov = texelFetch(u_cov, ivec2(floor(gl_FragCoord.xy)), 0).a;
    float sh = texture(u_shadow, uv).r;
    vec3 g = vec3(luma(c.rgb) * 0.3);
    if (spr) g = vec3(0.45 + si.b * 0.3, 0.15, 0.6 + si.b * 0.3);
    else if (si.g >= 0.0) g = vec3(0.8, 0.0, 0.0);
    else if (cov > 0.5) g = vec3(0.0, 0.7, 0.0);
    else g += vec3(sh, sh, 0.0) * 0.8;
    gl_FragColor = vec4(g, c.a);
    return;
  }
  if (spr) {
    // LM as the sprite's ambient: the scene's darkening, plus what the lights add above the area ambient,
    // relative to the creature's own LM (lighting_logic SpriteLight).
    float lm = floor(si.g / 64.0) / 15.0;
    float aof = 1.0;  // lighting_logic SpriteAoFactor(AoAt(x, feet)): enclosed spots take ambient away
    if (u_spriteAO > 0.0) {
      vec2 m = (p + 0.5) * u_map.xy + u_map.zw;
      ivec2 az = textureSize(u_ao, 0);
      ivec2 at = clamp(ivec2(floor(vec2(m.x, m.y + si.r) / max(u_aoTexel, 1.0))), ivec2(0), az - ivec2(1));
      aof = 1.0 - (1.0 - texelFetch(u_ao, at, 0).r) * u_spriteAO;
    }
    l = u_ambient * aof + max(l - u_ambient, vec3(0.0)) / max(lm, 0.3);
  } else {
    // never on a creature pixel (creatures without a depth file are not in the sprite buffer)
    ivec2 cq = ivec2(floor(gl_FragCoord.xy));
    ivec2 cz = textureSize(u_cov, 0);
    bool creature = u_covValid == 1 && cq.x < cz.x && cq.y < cz.y && texelFetch(u_cov, cq, 0).a > 0.5;
    if (!creature && u_lmPool > 0.0) {  // lighting_logic LightmapPool: the LM's painted light pools at night
      vec2 m = (p + 0.5) * u_map.xy + u_map.zw;
      vec3 lmc = texture(u_lm, m / u_lmArea).rgb;
      float over = luma(lmc) - u_lmMean;
      if (over > 0.0) l += lmc * (u_lmPool * over / max(luma(lmc), 1e-4));
    }
    if (!creature && u_shadowStrength > 0.0) l *= 1.0 - texture(u_shadow, uv).r * u_shadowStrength;
  }
  vec3 over = max(l - 1.0, vec3(0.0));
  l = min(l, vec3(1.0)) + over / (1.0 + over / max(u_maxLight - 1.0, 0.001));
  vec3 lit = c.rgb * l;
  float hi = smoothstep(0.72, 1.0, max(c.r, max(c.g, c.b)));
  lit = mix(lit, max(lit, c.rgb), hi * u_keep);
  lit = mix(lit, max(lit, c.rgb), e * u_emisKeep);  // emitting pixels keep their own brightness
  lit = mix(lit, c.rgb, fxCover);  // where a (tinted) effect is drawn it shows unlit, its tint taken out
  lit = mix(lit, drawn, untinted);  // an untinted one as the engine drew it (lighting_logic UndoTintFx)
  lit += ec * e * u_fxBoost;                          // and shine a little past it
  if (u_atmo.w > 0.0 && u_hasHeight == 1) {
    vec2 m = (p + 0.5) * u_map.xy + u_map.zw;
    float t = smoothstep(200.0, 1024.0, heightAt(m)) * u_atmo.w;
    if (t > 0.0) {
      float surface = smoothstep(0.02, 0.15, luma(lit));  // the void stays black (ApplyAtmosphere)
      lit = mix(lit, vec3(luma(lit)), u_atmo.y * t);
      lit = (lit - 0.5) * (1.0 - u_atmo.z * t * surface) + 0.5;
      lit = mix(lit, u_hazeColour, u_atmo.x * t * surface);
    }
  }
  lit = clamp(lit, 0.0, 1.0);
  if (u_bloomStrength > 0.0) {
    vec3 bb = b * u_bloomStrength;
    lit = lit + bb * (1.0 - lit * 0.5);
  }
  gl_FragColor = vec4(min(lit, vec3(1.0)), c.a);
}
)";

// Solid fill (the glow mask): quads in light-buffer pixels.
const char* kFillFs = R"(#version 130
void main() { gl_FragColor = vec4(1.0); }
)";

// Painted emitter glow (lighting_logic PaintedGlowWeight): the scene's bright pixels around a baked light's
// painted flame, into the emissive buffer (window px; blend MAX with the effect captures).
const char* kPaintFs = R"(#version 130
uniform sampler2D u_scene;  // the scene copy (viewport px)
uniform vec2 u_vpOrigin;
uniform vec4 u_map;         // area = phys * xy + zw
uniform vec3 u_emitter;     // area x, y, core radius
uniform float u_gain;
uniform vec3 u_engineTint;
void main() {
  vec2 p = gl_FragCoord.xy - u_vpOrigin;
  ivec2 ip = ivec2(floor(p));
  ivec2 sz = textureSize(u_scene, 0);
  if (ip.x < 0 || ip.y < 0 || ip.x >= sz.x || ip.y >= sz.y) discard;
  vec3 c = min(texelFetch(u_scene, ip, 0).rgb / max(u_engineTint, vec3(0.2)), vec3(1.0));
  vec2 m = (p + 0.5) * u_map.xy + u_map.zw;
  float d = length(m - u_emitter.xy);
  float v = max(c.r, max(c.g, c.b));
  float w = smoothstep(0.70, 0.92, v) * (1.0 - smoothstep(0.5 * u_emitter.z, u_emitter.z, d));
  if (w <= 0.0) discard;
  gl_FragColor = vec4(min(c * w * u_gain, vec3(1.0)), 1.0);
}
)";

// Bloom (GemRB PostFS modes 0/1): bright pass of the scene under the glow mask, separable 9-tap blur.
const char* kPostFs = R"(#version 130
uniform int u_mode;
uniform sampler2D u_src;   // mode 0: the scene copy (framebuffer px); mode 1: the previous bloom buffer
uniform sampler2D u_emis;
uniform vec2 u_srcSize;    // light-buffer size
uniform vec2 u_dir;
uniform float u_threshold;
uniform float u_toPhys;
uniform vec2 u_vpOrigin;
void main() {
  vec2 uv = gl_FragCoord.xy / u_srcSize;
  if (u_mode == 0) {
    vec3 c = texelFetch(u_emis, ivec2(floor(gl_FragCoord.xy * u_toPhys + u_vpOrigin)), 0).rgb;
    float e = 1.0;
    float l = max(c.r, max(c.g, c.b));
    float knee = 0.18;
    float soft = clamp(l - u_threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.0001);
    float w = max(soft, l - u_threshold) / max(l, 0.0001);
    gl_FragColor = vec4(c * w * e, 1.0);
    return;
  }
  vec2 d = u_dir / u_srcSize;
  vec3 c = texture(u_src, uv).rgb * 0.2270270;
  c += (texture(u_src, uv + d * 1.3846154).rgb + texture(u_src, uv - d * 1.3846154).rgb) * 0.3162162;
  c += (texture(u_src, uv + d * 3.2307692).rgb + texture(u_src, uv - d * 3.2307692).rgb) * 0.0702703;
  gl_FragColor = vec4(c, 1.0);
}
)";

// Sprite info pass: one quad per creature frame, writes the sprite buffer (lighting_logic PackSpriteG).
const char* kInfoFs = R"(#version 130
uniform sampler2D u_depth;  // R32F heights of the frame (px)
uniform vec4 u_map;         // area = phys * xy + zw
uniform vec4 u_rect;        // frame left, top (area px), w, h
uniform float u_feet;       // area y of the feet
uniform int u_mirror;
uniform float u_lm;
uniform float u_strength;
float hAt(ivec2 t) {
  ivec2 s = textureSize(u_depth, 0);
  if (t.x < 0 || t.y < 0 || t.x >= s.x || t.y >= s.y) return 0.0;
  return texelFetch(u_depth, t, 0).r;
}
void main() {
  vec2 m = gl_FragCoord.xy * u_map.xy + u_map.zw;
  vec2 f = m - u_rect.xy;
  if (f.x < 0.0 || f.y < 0.0 || f.x >= u_rect.z || f.y >= u_rect.w) discard;
  ivec2 t = ivec2(floor(f));
  if (u_mirror == 1) t.x = int(u_rect.z) - 1 - t.x;
  float h = hAt(t);
  if (h <= 0.0) discard;
  float gx = (hAt(t + ivec2(1, 0)) - hAt(t - ivec2(1, 0))) * 0.5;
  float gu = (hAt(t - ivec2(0, 1)) - hAt(t + ivec2(0, 1))) * 0.5;
  if (u_mirror == 1) gx = -gx;
  vec3 n = normalize(vec3(-gx * u_strength, 1.0, -gu * u_strength));  // sprite_depth SpriteNormal
  if (n.y < 0.5) {  // never more than 60 degrees off the camera direction
    vec2 xz = normalize(n.xz) * 0.8660254;
    n = vec3(xz.x, 0.5, xz.y);
  }
  float g = floor(clamp(u_lm, 0.0, 1.0) * 15.0 + 0.5) * 64.0 + min(h, 63.0);
  gl_FragColor = vec4(max(u_feet - m.y, 0.0), g, n.x, n.z);
}
)";

// Key-light shadow of one creature (lighting_logic KeyShadowSource): for each floor point, the sprite pixel
// whose projection lands there; its coverage darkens (blend MAX into the shadow buffer).
const char* kShadowFs = R"(#version 130
uniform sampler2D u_cov;
uniform sampler2D u_height;
uniform vec4 u_map;
uniform vec2 u_vpOrigin;
uniform float u_toPhys;
uniform vec4 u_src;   // frame left, top, w, h (area px)
uniform float u_feet;
uniform vec2 u_dir;   // KeyShadowAxes sx, sy
uniform float u_k;     // strength (the value written; the composite multiplies the light by 1 - value)
uniform int u_mode;    // 0 key-light silhouette, 1 contact shadow: ellipse at (u_src.x, u_feet), radii u_dir
uniform float u_texel;
uniform int u_hasHeight;
bool wallAt(vec2 m) {
  if (u_hasHeight == 0) return false;
  ivec2 sz = textureSize(u_height, 0);
  ivec2 t = clamp(ivec2(floor(m / max(u_texel, 1.0))), ivec2(0), sz - ivec2(1));
  return texelFetch(u_height, t, 0).r > 6.0;  // a wall rises here: no shadow pasted on it
}
void main() {
  vec2 phys = gl_FragCoord.xy * u_toPhys;
  vec2 m = phys * u_map.xy + u_map.zw;
  if (u_mode == 1) {  // lighting_logic ContactShadow
    float r = length((m - vec2(u_src.x, u_feet)) / u_dir);
    if (r >= 1.0 || wallAt(m)) discard;
    gl_FragColor = vec4(u_k * (1.0 - smoothstep(0.3, 1.0, r)));
    return;
  }
  float z = (m.y - u_feet) / u_dir.y;
  float top = u_feet - u_src.y;
  if (z <= 0.0 || z > top) discard;
  vec2 s = vec2(m.x - u_dir.x * z, u_feet - z);
  if (s.x < u_src.x || s.x >= u_src.x + u_src.z) discard;
  if (wallAt(m)) discard;
  ivec2 cp = ivec2(floor((s - u_map.zw) / u_map.xy + u_vpOrigin));
  ivec2 cs = textureSize(u_cov, 0);
  if (cp.x < 0 || cp.y < 0 || cp.x >= cs.x || cp.y >= cs.y) discard;
  float a = texelFetch(u_cov, cp, 0).a;
  gl_FragColor = vec4(u_k * a * (1.0 - 0.5 * z / max(top, 1.0)));  // GemRB: the tip at half
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
  g_fill_prog = Link(kLightVs, kFillFs, "fill");
  g_post_prog = Link(kLightVs, kPostFs, "bloom");
  g_info_prog = Link(kLightVs, kInfoFs, "sprite info");
  g_shadow_prog = Link(kLightVs, kShadowFs, "key shadow");
  g_paint_prog = Link(kLightVs, kPaintFs, "painted glow");
  if (!g_light_prog || !g_comp_prog || !g_fill_prog || !g_post_prog || !g_info_prog || !g_shadow_prog ||
      !g_paint_prog) {
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
          gl.GetUniformLocation(lp, "u_debug"),        gl.GetUniformLocation(lp, "u_info"),
          gl.GetUniformLocation(lp, "u_cov"),          gl.GetUniformLocation(lp, "u_sprites"),
          gl.GetUniformLocation(lp, "u_spriteOccl"),   gl.GetUniformLocation(lp, "u_vpOrigin"),
          gl.GetUniformLocation(lp, "u_spriteGain"),   gl.GetUniformLocation(lp, "u_owner"),
          gl.GetUniformLocation(lp, "u_spriteShadow")};
  g_cu = {gl.GetUniformLocation(cp, "u_scene"),  gl.GetUniformLocation(cp, "u_light"),
          gl.GetUniformLocation(cp, "u_vpOrigin"), gl.GetUniformLocation(cp, "u_vpSize"),
          gl.GetUniformLocation(cp, "u_mode"),   gl.GetUniformLocation(cp, "u_keep"),
          gl.GetUniformLocation(cp, "u_maxLight"), gl.GetUniformLocation(cp, "u_emis"),
          gl.GetUniformLocation(cp, "u_bloom"),  gl.GetUniformLocation(cp, "u_height"),
          gl.GetUniformLocation(cp, "u_map"),    gl.GetUniformLocation(cp, "u_texel"),
          gl.GetUniformLocation(cp, "u_hasHeight"), gl.GetUniformLocation(cp, "u_emisKeep"),
          gl.GetUniformLocation(cp, "u_bloomStrength"), gl.GetUniformLocation(cp, "u_atmo"),
          gl.GetUniformLocation(cp, "u_hazeColour"), gl.GetUniformLocation(cp, "u_fxBoost"),
          gl.GetUniformLocation(cp, "u_fringe"), gl.GetUniformLocation(cp, "u_info"),
          gl.GetUniformLocation(cp, "u_cov"), gl.GetUniformLocation(cp, "u_shadow"),
          gl.GetUniformLocation(cp, "u_sprites"), gl.GetUniformLocation(cp, "u_ambient"),
          gl.GetUniformLocation(cp, "u_shadowStrength"), gl.GetUniformLocation(cp, "u_covValid"),
          gl.GetUniformLocation(cp, "u_engineTint"), gl.GetUniformLocation(cp, "u_lm"),
          gl.GetUniformLocation(cp, "u_lmPool"), gl.GetUniformLocation(cp, "u_lmMean"),
          gl.GetUniformLocation(cp, "u_lmArea"), gl.GetUniformLocation(cp, "u_ao"),
          gl.GetUniformLocation(cp, "u_spriteAO"), gl.GetUniformLocation(cp, "u_aoTexel"),
          gl.GetUniformLocation(cp, "u_fxm"), gl.GetUniformLocation(cp, "u_fxmValid")};
  const unsigned np = g_paint_prog;
  g_pnu = {gl.GetUniformLocation(np, "u_target"),  gl.GetUniformLocation(np, "u_scene"),
           gl.GetUniformLocation(np, "u_vpOrigin"), gl.GetUniformLocation(np, "u_map"),
           gl.GetUniformLocation(np, "u_emitter"), gl.GetUniformLocation(np, "u_gain"),
           gl.GetUniformLocation(np, "u_engineTint")};
  g_fu = {gl.GetUniformLocation(g_fill_prog, "u_target")};
  const unsigned pp = g_post_prog;
  g_pu = {gl.GetUniformLocation(pp, "u_mode"),    gl.GetUniformLocation(pp, "u_src"),
          gl.GetUniformLocation(pp, "u_emis"),    gl.GetUniformLocation(pp, "u_srcSize"),
          gl.GetUniformLocation(pp, "u_dir"),     gl.GetUniformLocation(pp, "u_threshold"),
          gl.GetUniformLocation(pp, "u_toPhys"),  gl.GetUniformLocation(pp, "u_target"),
          gl.GetUniformLocation(pp, "u_vpOrigin")};
  const unsigned ip = g_info_prog, sp = g_shadow_prog;
  g_iu = {gl.GetUniformLocation(ip, "u_target"), gl.GetUniformLocation(ip, "u_depth"),
          gl.GetUniformLocation(ip, "u_map"),    gl.GetUniformLocation(ip, "u_rect"),
          gl.GetUniformLocation(ip, "u_feet"),   gl.GetUniformLocation(ip, "u_mirror"),
          gl.GetUniformLocation(ip, "u_lm"),     gl.GetUniformLocation(ip, "u_strength")};
  g_su = {gl.GetUniformLocation(sp, "u_target"), gl.GetUniformLocation(sp, "u_cov"),
          gl.GetUniformLocation(sp, "u_map"),    gl.GetUniformLocation(sp, "u_vpOrigin"),
          gl.GetUniformLocation(sp, "u_toPhys"), gl.GetUniformLocation(sp, "u_src"),
          gl.GetUniformLocation(sp, "u_feet"),   gl.GetUniformLocation(sp, "u_dir"),
          gl.GetUniformLocation(sp, "u_k"),      gl.GetUniformLocation(sp, "u_height"),
          gl.GetUniformLocation(sp, "u_texel"),  gl.GetUniformLocation(sp, "u_hasHeight"),
          gl.GetUniformLocation(sp, "u_mode")};
  gl.ActiveTexture(kTexture0);
  g_scene_tex = NewTexture(kNearest);
  g_light_tex = NewTexture(kLinear);
  g_emis_tex = NewTexture(kNearest);  // emissive capture, sized to the world viewport (Capture*)
  g_bloom_tex[0] = NewTexture(kLinear);
  g_bloom_tex[1] = NewTexture(kLinear);
  gl.GenFramebuffers(1, &g_light_fbo);
  gl.GenFramebuffers(1, &g_emis_fbo);
  gl.GenFramebuffers(2, g_bloom_fbo);
  g_info_tex = NewTexture(kNearest);
  g_cov_tex = NewTexture(kNearest);
  g_shadow_tex = NewTexture(kLinear);
  gl.GenFramebuffers(1, &g_info_fbo);
  gl.GenFramebuffers(1, &g_cov_fbo);
  gl.GenFramebuffers(1, &g_shadow_fbo);
  g_fxm_tex = NewTexture(kNearest);
  gl.GenFramebuffers(1, &g_fxm_fbo);
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
  g_area_added = g_set.area_lights ? AddAreaLights(g_area_lights, wed, &g_scene.lights) : 0;
  if (ReadFile(dir + "height.bin", &buf)) ReadMap(buf.data(), buf.size(), "GLH1", 2, g_scene.height);
  if (ReadFile(dir + "normal.bin", &buf)) ReadMap(buf.data(), buf.size(), "GLN1", 3, g_scene.normal);
  if (ReadFile(dir + "ao.bin", &buf)) ReadMap(buf.data(), buf.size(), "GLA1", 1, g_scene.ao);
  g_ambient = Ambient(type, g_set.ambient, &g_scene, g_set.key_light);
  Log("lighting: %s: %zu baked lights (%zu by hand), key %d, height %dx%d, normals %d, ao %d, type 0x%04X, "
      "ambient %.3f %.3f %.3f",
      wed.c_str(), g_scene.lights.size(), g_area_added, g_scene.has_key ? 1 : 0, g_scene.height.w, g_scene.height.h,
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
  put(&g_ao_tex, g_scene.ao, kR8, kRed, kUByte, g_scene.ao.data.data(), kNearest);  // sprite AO (AoAt)
}

void EnsureTargets(int w, int h) {
  auto& gl = g_gl;
  gl.ActiveTexture(kTexture0);
  if (w != g_scene_w || h != g_scene_h) {
    gl.BindTexture(kTex2D, g_scene_tex);
    gl.TexImage2D(kTex2D, 0, kRgba8, w, h, 0, kRgba, kUByte, nullptr);
    g_scene_w = w;
    g_scene_h = h;
    gl.BindTexture(kTex2D, g_info_tex);
    gl.TexImage2D(kTex2D, 0, kRgba16F, w, h, 0, kRgba, kFloat, nullptr);
    gl.BindFramebuffer(kFramebuffer, g_info_fbo);
    gl.FramebufferTexture2D(kFramebuffer, kColorAttach0, kTex2D, g_info_tex, 0);
    if (gl.CheckFramebufferStatus(kFramebuffer) != kFbComplete) Log("lighting: sprite buffer %dx%d incomplete", w, h);
    g_info_w = w;
    g_info_h = h;
  }
  const int lw = std::max(1, (w + 1) / 2), lh = std::max(1, (h + 1) / 2);
  if (lw != g_light_w || lh != g_light_h) {
    gl.BindTexture(kTex2D, g_light_tex);
    gl.TexImage2D(kTex2D, 0, kRgba16F, lw, lh, 0, kRgba, kFloat, nullptr);
    gl.BindFramebuffer(kFramebuffer, g_light_fbo);
    gl.FramebufferTexture2D(kFramebuffer, kColorAttach0, kTex2D, g_light_tex, 0);
    const unsigned st = gl.CheckFramebufferStatus(kFramebuffer);
    if (st != kFbComplete) Log("lighting: light buffer %dx%d incomplete (%X)", lw, lh, st);
    auto attach = [&](unsigned tex, unsigned fbo, int internal, unsigned type, const char* what) {
      gl.BindTexture(kTex2D, tex);
      gl.TexImage2D(kTex2D, 0, internal, lw, lh, 0, kRgba, type, nullptr);
      gl.BindFramebuffer(kFramebuffer, fbo);
      gl.FramebufferTexture2D(kFramebuffer, kColorAttach0, kTex2D, tex, 0);
      const unsigned s2 = gl.CheckFramebufferStatus(kFramebuffer);
      if (s2 != kFbComplete) Log("lighting: %s buffer %dx%d incomplete (%X)", what, lw, lh, s2);
    };
    attach(g_bloom_tex[0], g_bloom_fbo[0], kRgba16F, kFloat, "bloom 0");
    attach(g_bloom_tex[1], g_bloom_fbo[1], kRgba16F, kFloat, "bloom 1");
    attach(g_shadow_tex, g_shadow_fbo, kRgba8, kUByte, "shadow");
    g_light_w = lw;
    g_light_h = lh;
  }
}

void Quad(float x0, float y0, float x1, float y1) {
  auto& gl = g_gl;
  gl.Begin(kQuads);
  gl.Vertex2f(x0, y0);
  gl.Vertex2f(x1, y0);
  gl.Vertex2f(x1, y1);
  gl.Vertex2f(x0, y1);
  gl.End();
}

// This frame's glow draws (engine adapter, draw space) -> area rects with their table rows, and the
// effect lights they make.
// Buff colour lights: creatures in view with a colour glow effect (8/9/52/61) carry a light of that colour.
// Creature lights (vfxlight.tsv "anim:N", e.g. Ignus burns): fixed at the creature's feet, owned.
void GatherBuffLights(const ViewMap& vm, float ox, float oy, int vw, int vh, double t, std::vector<BakedLight>* out) {
  g_buff_lights = 0;
  g_noshadow.clear();
  const bool creature_lights = g_set.hand_lights && g_has_creature_lights;
  int nd = 0;
  const engine::EngineSpriteDraw* draws = creature_lights ? engine::SpriteDraws(&nd) : nullptr;
  if (!g_set.buff_lights && !creature_lights) return;
  float x0, y0, x1, y1;
  ToArea(vm, 0, float(vh), &x0, &y0);
  ToArea(vm, float(vw), 0, &x1, &y1);
  const float margin = 200;
  std::vector<std::pair<uint64_t, double>> seen;
  for (const engine::SpriteInfo& sp : engine::AllSprites()) {
    if (!sp.alive || sp.area == 0 || sp.x < x0 - margin || sp.x > x1 + margin || sp.y < y0 - margin ||
        sp.y > y1 + margin + 100)
      continue;
    if (creature_lights)
      if (const HandLight* h = FindCreatureLight(g_hand, engine::SpriteAnimId(sp.id))) {
        // Where the creature is SHOWN this frame ([smooth] interpolation slides it between ticks; the engine
        // position steps every other tick, which made the light stutter), else its position.
        float fx = float(sp.x), fy = float(sp.y);
        int shown_x = 0, shown_y = 0;
        if (engine::InterpDrawPos(sp.id, &shown_x, &shown_y)) {
          fx = float(shown_x);
          fy = float(shown_y);
        }
        out->push_back(CreatureLightAt(*h, fx, fy, t, g_set.hand_light_gain));
        if (h->noshadow) g_noshadow.emplace_back(fx, fy);
        if (g_cl_trace_left > 0) {  // PSTF_LightCreatureTrace: one sample per drawn frame
          --g_cl_trace_left;
          g_cl_trace.emplace_back(fx, fy);
          float ax = fx, ay = fy;  // the sprite draw's anchor nearest the creature (what the sprite pass uses)
          NearestFeet(draws, nd, ox, oy, &ax, &ay);
          g_cl_trace_anchor.emplace_back(ax, ay);
        }
        g_cl_x = out->back().x;
        g_cl_y = out->back().y;
        g_cl_r = out->back().r;
        ++g_hand_lights;
      }
    if (!g_set.buff_lights) continue;
    for (const engine::ColorEffectView& e : engine::ColorEffects(sp.id)) {
      const BuffLightShape shape = BuffLightFor(e.timing, g_set.weapon_glow_radius, g_set.weapon_glow_gain);
      if (shape.gain <= 0.0f) continue;
      double first = t;
      for (const auto& p : g_color_seen)
        if (p.first == e.key) first = p.second;
      seen.emplace_back(e.key, first);
      Rgb c;
      if (!ColorEffectLight(e.opcode, e.param1, e.param2, t - first, t, g_set.buff_light_gain * shape.gain, &c))
        continue;
      BakedLight l;
      l.x = float(sp.x);
      l.y = float(sp.y);
      l.z = shape.z;
      l.radius = shape.radius;
      l.r = c.r;
      l.g = c.g;
      l.b = c.b;
      l.owned = true;
      out->push_back(l);
      ++g_buff_lights;
    }
  }
  g_color_seen.swap(seen);
}

std::vector<BakedLight> GatherFx(float ox, float oy, const ViewMap& vm, int vw, int vh) {
  int n = 0;
  const engine::EngineGlowDraw* d = engine::GlowDraws(&n);
  g_glows.clear();
  g_fx_hits = 0;
  g_hand_lights = 0;
  const double t = double(GetTickCount64());
  std::vector<BakedLight> lights, fixed;
  for (int i = 0; i < n; ++i) {
    GlowDraw g;
    g.x = float(d[i].x) + ox;
    g.y = float(d[i].y) + oy;
    g.w = float(d[i].w);
    g.h = float(d[i].h);
    if (const HandLight* h = g_set.hand_lights ? FindHandLight(g_hand, d[i].resref) : nullptr) {
      fixed.push_back(HandLightAt(*h, g, size_t(i), t, g_set.hand_light_gain));
      ++g_hand_lights;
      continue;
    }
    if (d[i].resref[0]) g.vfx = g_vfx.Find(d[i].resref, d[i].cycle, d[i].frame);
    if (g.vfx) ++g_fx_hits;
    static int misses_logged = 0;
    if (!g.vfx && misses_logged < 40) {
      ++misses_logged;
      Log("lighting: glow draw not in the vfx table: '%s' cycle %u frame %u, %dx%d, flags %08X", d[i].resref,
          d[i].cycle, d[i].frame, d[i].w, d[i].h, d[i].flags);
    }
    g_glows.push_back(g);
    BakedLight l;
    if (g_set.fx_lights && FxLight(g, 2.6f, &l)) lights.push_back(l);
  }
  g_fx_glows = int(g_glows.size());
  lights = MergeFxLights(std::move(lights), 48.0f, size_t(g_set.fx_budget));
  for (BakedLight& l : lights) {
    const Rgb sc = Saturate({l.r, l.g, l.b}, g_set.fx_light_saturation);
    l.r = sc.r;
    l.g = sc.g;
    l.b = sc.b;
    l.r *= g_set.fx_light_gain;
    l.g *= g_set.fx_light_gain;
    l.b *= g_set.fx_light_gain;
  }
  g_fx_lights = int(lights.size());
  if (!lights.empty()) ++g_fx_frames_with_light;
  g_last_fx = lights;
  GatherBuffLights(vm, ox, oy, vw, vh, t, &fixed);
  lights.insert(lights.end(), fixed.begin(), fixed.end());
  return lights;
}

// Painted emitters of the baked lights in view: their bright pixels go into the emissive buffer (after the
// effect captures of this frame, blend MAX), so painted flames keep their brightness and bloom.
bool EnsureEmisBuffer(int w, int h, int prev_draw);
void PaintPass(const ViewMap& vm, int vx, int vy, int vw, int vh) {
  g_painted = 0;
  if (!g_set.painted_glow || !g_set.baked_lights || !(g_set.emissive || g_set.bloom) || g_scene.lights.empty() ||
      g_set.painted_glow_gain <= 0)
    return;
  float ax0, ay0, ax1, ay1;  // the view in area px
  ToArea(vm, 0, float(vh), &ax0, &ay0);
  ToArea(vm, float(vw), 0, &ax1, &ay1);
  struct Em {
    float x, y, core;
    size_t i;
  };
  std::vector<Em> em;
  for (size_t i = 0; i < g_scene.lights.size(); ++i) {
    Em e{0, 0, 0, i};
    if (!PaintedEmitter(g_scene.lights[i], g_set.painted_core, &e.x, &e.y, &e.core)) continue;
    if (e.x + e.core < std::min(ax0, ax1) || e.x - e.core > std::max(ax0, ax1) ||
        e.y + e.core < std::min(ay0, ay1) || e.y - e.core > std::max(ay0, ay1))
      continue;
    em.push_back(e);
  }
  if (em.empty()) return;
  auto& gl = g_gl;
  const int w = vx + vw, h = vy + vh;
  int draw = 0;
  gl.GetIntegerv(0x8CA6 /*draw fbo*/, &draw);
  if (!EnsureEmisBuffer(w, h, draw)) return;
  gl.BindFramebuffer(kFramebuffer, g_emis_fbo);
  gl.Viewport(0, 0, w, h);
  if (!g_cap_used) {  // no effect capture this frame: the buffer still holds an older frame
    gl.ColorMask(1, 1, 1, 1);
    gl.ClearColor(0, 0, 0, 0);
    gl.Clear(kColorBit);
    g_cap_used = true;
  }
  gl.ColorMask(1, 1, 1, 0);  // painted glow is colour only: alpha is the self-lit creature cover
  gl.UseProgram(g_paint_prog);
  gl.Uniform2f(g_pnu.target, float(w), float(h));
  gl.Uniform1i(g_pnu.scene, 0);
  gl.Uniform2f(g_pnu.vp_origin, float(vx), float(vy));
  gl.Uniform4f(g_pnu.map, vm.ax, vm.ay, vm.bx, vm.by);
  gl.Uniform3f(g_pnu.engine_tint, g_engine_tint.r, g_engine_tint.g, g_engine_tint.b);
  gl.ActiveTexture(kTexture0);
  gl.BindTexture(kTex2D, g_scene_tex);
  gl.Enable(kBlend);
  gl.BlendEquationSeparate(0x8008 /*MAX*/, 0x8008);
  gl.BlendFuncSeparate(kOne, kOne, kOne, kOne);
  const double t = double(GetTickCount64());
  for (const Em& e : em) {
    const BakedLight& l = g_scene.lights[e.i];
    gl.Uniform3f(g_pnu.emitter, e.x, e.y, e.core);
    gl.Uniform1f(g_pnu.gain, g_set.painted_glow_gain * Flicker(l.kind, e.i, t));
    float x0, y0, x1, y1;
    ToPhys(vm, e.x - e.core, e.y - e.core, &x0, &y0);
    ToPhys(vm, e.x + e.core, e.y + e.core, &x1, &y1);
    Quad(std::min(x0, x1) + float(vx) - 1, std::min(y0, y1) + float(vy) - 1, std::max(x0, x1) + float(vx) + 1,
         std::max(y0, y1) + float(vy) + 1);
    ++g_painted;
  }
  gl.BlendEquationSeparate(kFuncAdd, kFuncAdd);
  gl.Disable(kBlend);
}

// Bright pass of the emissive capture into bloom 0, blur H into 1, blur V back into 0.
void BloomPass(float to_phys, int vx, int vy) {
  auto& gl = g_gl;
  gl.BindFramebuffer(kFramebuffer, g_bloom_fbo[0]);
  gl.Viewport(0, 0, g_light_w, g_light_h);
  gl.ClearColor(0, 0, 0, 0);
  gl.Clear(kColorBit);
  if (!g_set.bloom || !g_cap_used) return;
  gl.UseProgram(g_post_prog);
  gl.Uniform2f(g_pu.vp_origin, float(vx), float(vy));
  gl.Uniform2f(g_pu.target, float(g_light_w), float(g_light_h));
  gl.Uniform2f(g_pu.src_size, float(g_light_w), float(g_light_h));
  gl.Uniform1f(g_pu.threshold, g_set.bloom_threshold);
  gl.Uniform1f(g_pu.to_phys, to_phys);
  gl.Uniform1i(g_pu.src, 0);
  gl.Uniform1i(g_pu.emis, 4);
  gl.ActiveTexture(kTexture0 + 4);
  gl.BindTexture(kTex2D, g_emis_tex);
  gl.ActiveTexture(kTexture0);
  gl.BindTexture(kTex2D, g_scene_tex);
  gl.Uniform1i(g_pu.mode, 0);
  Quad(0, 0, float(g_light_w), float(g_light_h));
  gl.Uniform1i(g_pu.mode, 1);
  // Each iteration blurs H then V with a wider step, so 2+ iterations give a soft halo around the core.
  for (int it = 0; it < std::max(1, g_set.bloom_passes); ++it)
    for (int pass = 0; pass < 2; ++pass) {
      const float step = 1.0f + 0.5f * float(it);
      gl.BindFramebuffer(kFramebuffer, g_bloom_fbo[1 - pass]);
      gl.BindTexture(kTex2D, g_bloom_tex[pass]);
      gl.Uniform2f(g_pu.dir, pass == 0 ? step : 0.0f, pass == 0 ? 0.0f : step);
      Quad(0, 0, float(g_light_w), float(g_light_h));
    }
  if (g_bloom_req) {  // bloom fbo 0 is bound: the final blur
    std::vector<float> px(size_t(g_light_w) * g_light_h * 4);
    gl.PixelStorei(kPackAlign, 4);
    gl.ReadPixels(0, 0, g_light_w, g_light_h, kRgba, kFloat, px.data());
    float m = 0;
    for (size_t i = 0; i < px.size(); i += 4) m = std::max(m, std::max(px[i], std::max(px[i + 1], px[i + 2])));
    g_bloom_max = m;
    g_bloom_req = false;
    Log("lighting: bloom max %.3f over %zu glow draws", m, g_glows.size());
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

// --- Phase 2: sprite buffer ---------------------------------------------------------------------------
bool HasDepth(const char* res) {
  if (!res || !res[0]) return false;
  return std::binary_search(g_dep_names.begin(), g_dep_names.end(), PackResRef(res));
}

const DepFile* LoadDep(const char* res) {
  const uint64_t name = PackResRef(res);
  for (DepEntry& e : g_deps)
    if (e.name == name) {
      e.used = g_frames;
      return e.ok ? &e.file : nullptr;
    }
  if (g_deps.size() >= 48) {  // evict the least recently used
    size_t old = 0;
    for (size_t i = 1; i < g_deps.size(); ++i)
      if (g_deps[i].used < g_deps[old].used) old = i;
    g_deps.erase(g_deps.begin() + std::ptrdiff_t(old));
  }
  DepEntry e;
  e.name = name;
  e.used = g_frames;
  std::vector<uint8_t> buf;
  std::string r(res);
  const std::string path = std::string(DataDir()) + "\\assets\\sprites\\depth\\" + r + ".DEP";
  e.ok = ReadFile(path, &buf) && ParseDep(std::move(buf), &e.file);
  if (!e.ok) Log("lighting: depth file %s unreadable", path.c_str());
  g_deps.push_back(std::move(e));
  return g_deps.back().ok ? &g_deps.back().file : nullptr;
}

// The depth texture of a creature draw's frame (R32F, frame px) and the frame's size and centre; 0 when
// there is none.
unsigned DepthTexture(const char* res, int cycle, int frame, int* w, int* h, int* cx, int* cy) {
  const DepFile* dep = LoadDep(res);
  if (!dep) return 0;
  const int index = dep->FrameIndex(cycle, frame);
  if (index < 0) return 0;
  const uint64_t name = PackResRef(res);
  for (DepTex& t : g_dep_tex)
    if (t.name == name && t.index == index) {
      t.used = g_frames;
      *w = t.w;
      *h = t.h;
      *cx = dep->frames[size_t(index)].cx;
      *cy = dep->frames[size_t(index)].cy;
      return t.tex;
    }
  std::vector<float> heights;
  if (!dep->Heights(index, &heights)) return 0;
  const DepFrame& f = dep->frames[size_t(index)];
  if (!f.w || !f.h) return 0;
  DepTex* slot = nullptr;
  if (g_dep_tex.size() >= 384) {
    slot = &g_dep_tex[0];
    for (DepTex& t : g_dep_tex)
      if (t.used < slot->used) slot = &t;
  } else {
    g_dep_tex.push_back(DepTex());
    slot = &g_dep_tex.back();
    slot->tex = NewTexture(kNearest);
  }
  slot->name = name;
  slot->index = index;
  slot->w = f.w;
  slot->h = f.h;
  slot->used = g_frames;
  auto& gl = g_gl;
  gl.ActiveTexture(kTexture0);
  gl.BindTexture(kTex2D, slot->tex);
  gl.PixelStorei(kUnpackAlign, 4);
  gl.PixelStorei(kUnpackRow, 0);
  gl.TexImage2D(kTex2D, 0, kR32F, f.w, f.h, 0, kRed, kFloat, heights.data());
  *w = f.w;
  *h = f.h;
  *cx = f.cx;
  *cy = f.cy;
  return slot->tex;
}

// Builds the sprite buffer (one quad per creature frame drawn this frame) and the key-light shadows.
void SpritePass(const ViewMap& vm, float ox, float oy, float to_phys, int vx, int vy, int vw, int vh, bool has_h) {
  auto& gl = g_gl;
  g_sprites_valid = false;
  g_spr_drawn = g_spr_depth = 0;
  g_spr_lm_sum = 0;
  int n = 0;
  const engine::EngineSpriteDraw* d = engine::SpriteDraws(&n);
  g_spr_seen = n;
  g_shadow_drawn = false;
  g_contact_drawn = 0;
  gl.BindFramebuffer(kFramebuffer, g_info_fbo);
  gl.Viewport(0, 0, vw, vh);
  gl.ClearColor(0, -1, 0, 0);
  gl.Clear(kColorBit);
  gl.BindFramebuffer(kFramebuffer, g_shadow_fbo);
  gl.Viewport(0, 0, g_light_w, g_light_h);
  gl.ClearColor(0, 0, 0, 0);
  gl.Clear(kColorBit);
  if (!g_set.sprite_lighting || n == 0 || !g_cov_used) return;
  const uint64_t area = engine::WorldPassArea();
  gl.BindFramebuffer(kFramebuffer, g_info_fbo);
  gl.Viewport(0, 0, vw, vh);
  gl.UseProgram(g_info_prog);
  gl.Uniform2f(g_iu.target, float(vw), float(vh));
  gl.Uniform4f(g_iu.map, vm.ax, vm.ay, vm.bx, vm.by);
  gl.Uniform1f(g_iu.strength, g_set.sprite_normal_strength);
  gl.Uniform1i(g_iu.depth, 0);
  struct Drawn {
    float left, top, w, h, feet, ax;
  };
  std::vector<Drawn> drawn;
  for (int i = 0; i < n; ++i) {
    int tw = 0, th = 0, tcx = 0, tcy = 0;
    const unsigned tex = DepthTexture(d[i].resref, d[i].cycle, d[i].frame, &tw, &th, &tcx, &tcy);
    if (!tex) continue;
    // frame rect from the depth file (FxCellRect with the BAM frame's own size and centre)
    const float left = float(d[i].mirror ? d[i].anchor_x - (tw - tcx) : d[i].anchor_x - tcx);
    const float top = float(d[i].anchor_y - tcy);
    ++g_spr_depth;
    float lm = engine::AreaTintLuma(area, int(float(d[i].anchor_x) + ox), int(float(d[i].anchor_y) + oy));
    if (lm < 0) lm = 1.0f;
    g_spr_lm_sum += lm;
    const Drawn r{left + ox, top + oy, float(tw), float(th), float(d[i].anchor_y) + oy, float(d[i].anchor_x) + ox};
    gl.UseProgram(g_info_prog);
    gl.ActiveTexture(kTexture0);
    gl.BindTexture(kTex2D, tex);
    gl.Uniform4f(g_iu.rect, r.left, r.top, r.w, r.h);
    gl.Uniform1f(g_iu.feet, r.feet);
    gl.Uniform1i(g_iu.mirror, d[i].mirror ? 1 : 0);
    gl.Uniform1f(g_iu.lm, lm);
    float x0, y0, x1, y1;
    ToPhys(vm, r.left, r.top, &x0, &y0);
    ToPhys(vm, r.left + r.w, r.top + r.h, &x1, &y1);
    Quad(std::min(x0, x1) - 1, std::min(y0, y1) - 1, std::max(x0, x1) + 1, std::max(y0, y1) + 1);
    drawn.push_back(r);
    ++g_spr_drawn;
  }
  g_sprites_valid = g_spr_drawn > 0;
  if (g_sprites_valid) ++g_spr_frames;
  const bool key = g_sprites_valid && g_set.key_shadows && g_scene.has_key && g_set.key_shadow_strength > 0;
  const bool contact = g_set.contact_shadows && g_set.contact_shadow_strength > 0 && g_set.contact_shadow_size > 0;
  if (!key && !contact) return;
  float sx = 0, sy = 0;
  KeyShadowAxes(g_scene.key_dir[0], g_scene.key_dir[1], g_set.key_shadow_length, &sx, &sy);
  gl.BindFramebuffer(kFramebuffer, g_shadow_fbo);
  gl.Viewport(0, 0, g_light_w, g_light_h);
  gl.UseProgram(g_shadow_prog);
  gl.Uniform1i(g_su.mode, 0);
  gl.Uniform1f(g_su.k, KeyShadowK(g_set.key_shadow_strength, g_scene.key_strength));
  gl.Uniform2f(g_su.target, float(g_light_w), float(g_light_h));
  gl.Uniform4f(g_su.map, vm.ax, vm.ay, vm.bx, vm.by);
  gl.Uniform2f(g_su.vp_origin, float(vx), float(vy));
  gl.Uniform1f(g_su.to_phys, to_phys);
  gl.Uniform2f(g_su.dir, sx, sy);
  gl.Uniform1f(g_su.texel, float(g_scene.height.texel > 0 ? g_scene.height.texel : 2));
  gl.Uniform1i(g_su.has_height, has_h ? 1 : 0);
  gl.Uniform1i(g_su.cov, 7);
  gl.Uniform1i(g_su.height, 2);
  gl.ActiveTexture(kTexture0 + 7);
  gl.BindTexture(kTex2D, g_cov_tex);
  gl.ActiveTexture(kTexture0 + 2);
  gl.BindTexture(kTex2D, has_h ? g_height_tex : 0);
  gl.ActiveTexture(kTexture0);
  gl.Enable(kBlend);
  gl.BlendEquationSeparate(0x8008 /*MAX*/, 0x8008);
  gl.BlendFuncSeparate(kOne, kOne, kOne, kOne);
  // Contact shadows: a soft ellipse at every creature's feet (all creature draws, depth file or not), so
  // creatures stand on the floor without the BAM blob (bam_shadows=0) and in areas without a key line.
  if (contact) {
    const float rx = g_set.contact_shadow_size, ry = g_set.contact_shadow_size * g_set.contact_shadow_flat;
    gl.Uniform1i(g_su.mode, 1);
    gl.Uniform1f(g_su.k, g_set.contact_shadow_strength);
    gl.Uniform2f(g_su.dir, rx, ry);
    for (int i = 0; i < n; ++i) {
      const float fx = float(d[i].anchor_x) + ox, fy = float(d[i].anchor_y) + oy;
      if (NoShadowAt(fx, fy)) continue;  // a burning creature is the light
      gl.Uniform4f(g_su.src, fx, fy, 0, 0);
      gl.Uniform1f(g_su.feet, fy);
      float x0, y0, x1, y1;
      ToPhys(vm, fx - rx, fy - ry, &x0, &y0);
      ToPhys(vm, fx + rx, fy + ry, &x1, &y1);
      Quad(std::min(x0, x1) / to_phys - 1, std::min(y0, y1) / to_phys - 1, std::max(x0, x1) / to_phys + 1,
           std::max(y0, y1) / to_phys + 1);
      ++g_contact_drawn;
    }
    g_shadow_drawn = g_contact_drawn > 0;
    gl.Uniform1i(g_su.mode, 0);
    gl.Uniform1f(g_su.k, KeyShadowK(g_set.key_shadow_strength, g_scene.key_strength));
    gl.Uniform2f(g_su.dir, sx, sy);
  }
  if (key) g_shadow_drawn = g_shadow_drawn || !drawn.empty();
  for (size_t i = 0; key && i < drawn.size(); ++i) {
    const Drawn& r = drawn[i];
    if (NoShadowAt(r.ax, r.feet)) continue;
    const float top_h = r.feet - r.top;  // tallest pixel above the feet
    gl.Uniform4f(g_su.src, r.left, r.top, r.w, r.h);
    gl.Uniform1f(g_su.feet, r.feet);
    // bounds of the projection of the frame rect (area), then light-buffer px
    const float xa = r.left + std::min(0.0f, sx * top_h), xb = r.left + r.w + std::max(0.0f, sx * top_h);
    const float ya = r.feet + std::min(0.0f, sy * top_h), yb = r.feet + std::max(0.0f, sy * top_h);
    float x0, y0, x1, y1;
    ToPhys(vm, xa, ya, &x0, &y0);
    ToPhys(vm, xb, yb, &x1, &y1);
    Quad(std::min(x0, x1) / to_phys - 1, std::min(y0, y1) / to_phys - 1, std::max(x0, x1) / to_phys + 1,
         std::max(y0, y1) / to_phys + 1);
  }
  gl.BlendEquationSeparate(kFuncAdd, kFuncAdd);
  gl.Disable(kBlend);
}

// Probe: pixels of the sprite buffer with a sprite (G >= 0), of the coverage buffer (alpha > 0.5), and both.
void SpriteCheck(int vx, int vy, int vw, int vh) {
  auto& gl = g_gl;
  std::vector<float> info(size_t(vw) * vh * 4);
  gl.BindFramebuffer(0x8CA8 /*read*/, g_info_fbo);
  gl.PixelStorei(kPackAlign, 4);
  gl.ReadPixels(0, 0, vw, vh, kRgba, kFloat, info.data());
  std::vector<uint8_t> cov(size_t(g_cov_w) * g_cov_h * 4);
  gl.BindFramebuffer(0x8CA8 /*read*/, g_cov_fbo);
  gl.PixelStorei(kPackAlign, 1);
  gl.ReadPixels(0, 0, g_cov_w, g_cov_h, kRgba, kUByte, cov.data());
  long long ni = 0, nc = 0, nb = 0;
  for (int y = 0; y < vh; ++y)
    for (int x = 0; x < vw; ++x) {
      const bool i = info[(size_t(y) * vw + x) * 4 + 1] >= 0.0f;
      const int cx = x + vx, cy = y + vy;
      const bool c = cx < g_cov_w && cy < g_cov_h && cov[(size_t(cy) * g_cov_w + cx) * 4 + 3] > 127;
      ni += i;
      nc += c;
      nb += i && c;
    }
  g_spr_info_px = ni;
  g_spr_cov_px = nc;
  g_spr_both_px = nb;
  g_spr_check_req = false;
  Log("lighting: sprite check: %lld px in the sprite buffer, %lld covered, %lld both (%d creatures)", ni, nc, nb,
      g_spr_drawn);
}

void LightPass(const ViewMap& vm, float to_phys, const std::vector<BakedLight>& fx) {
  auto& gl = g_gl;
  gl.BindFramebuffer(kFramebuffer, g_light_fbo);
  gl.Viewport(0, 0, g_light_w, g_light_h);
  const bool dbg = g_mode == DebugMode::Falloff;
  if (dbg) gl.ClearColor(0, 0, 0, 1);
  else gl.ClearColor(g_amb.r, g_amb.g, g_amb.b, 1);
  gl.Clear(kColorBit);
  g_drawn = 0;
  const bool baked = g_set.baked_lights && !g_scene.lights.empty();
  const bool with_fx = !fx.empty() && !g_cmp_req;  // the GPU/CPU compare covers baked lights only
  if (!baked && !with_fx) return;

  const bool has_h = g_set.height_map && g_height_tex && !g_scene.height.empty();
  const bool sprites = g_sprites_valid && !g_cmp_req;  // the GPU/CPU compare covers the floor model only
  const bool spr_occl = sprites && g_set.sprite_occlusion;
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
  gl.Uniform1i(g_lu.occl_steps, (has_h && g_set.occlusion) || spr_occl ? g_set.occlusion_steps : 0);
  gl.Uniform1i(g_lu.sprites, sprites ? 1 : 0);
  gl.Uniform1i(g_lu.sprite_occl, spr_occl ? 1 : 0);
  gl.Uniform1f(g_lu.sprite_gain, g_set.sprite_light_gain);
  gl.Uniform1f(g_lu.sprite_shadow, g_set.sprite_shadow_strength);
  gl.Uniform1i(g_lu.info, 6);
  gl.Uniform1i(g_lu.cov, 7);
  gl.Uniform2f(g_lu.vp_origin, float(g_last_vx), float(g_last_vy));
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
  gl.ActiveTexture(kTexture0 + 6);
  gl.BindTexture(kTex2D, g_info_tex);
  gl.ActiveTexture(kTexture0 + 7);
  gl.BindTexture(kTex2D, g_cov_tex);
  gl.ActiveTexture(kTexture0);
  gl.Enable(kBlend);
  gl.BlendEquationSeparate(kFuncAdd, kFuncAdd);
  gl.BlendFuncSeparate(kOne, kOne, kOne, kOne);

  const float amb_luma = Luma(g_amb);
  const float ng = g_night_gain;  // lights matter more at night (DayNight night_light)
  const double t = double(GetTickCount64());
  for (size_t i = 0; baked && i < g_scene.lights.size() && g_drawn < g_set.budget; ++i) {
    const BakedLight& l = g_scene.lights[i];
    Rect r;
    if (!FootprintInBuffer(vm, l, to_phys, g_light_w, g_light_h, &r)) continue;
    const float k = LightScale(l, g_set.light_gain, Flicker(l.kind, i, t), amb_luma) * ng;
    gl.Uniform4f(g_lu.light_pos, l.x, l.y, l.z, l.radius);
    gl.Uniform3f(g_lu.owner, 0, 0, 0);
    gl.Uniform3f(g_lu.light_color, l.r * k, l.g * k, l.b * k);
    Quad(r.x0, r.y0, r.x1, r.y1);
    ++g_drawn;
  }
  // Effect lights: colour already scaled (FxLight, fx_light_gain). Same model, occlusion included.
  for (size_t i = 0; with_fx && i < fx.size(); ++i) {
    const BakedLight& l = fx[i];
    Rect r;
    if (!FootprintInBuffer(vm, l, to_phys, g_light_w, g_light_h, &r)) continue;
    gl.Uniform4f(g_lu.light_pos, l.x, l.y, l.z, l.radius);
    gl.Uniform3f(g_lu.owner, l.x, l.y, l.unblocked ? 2.0f : l.owned ? 1.0f : 0.0f);
    gl.Uniform3f(g_lu.light_color, l.r * ng, l.g * ng, l.b * ng);
    Quad(r.x0, r.y0, r.x1, r.y1);
  }
  gl.Disable(kBlend);
  if (g_cmp_req && dbg) Compare(vm, to_phys, has_h);
}

// The area lightmap (LM: what the engine tints creatures with) as a texture, one texel per LM cell (16x12
// area px, CGameArea::GetTintColor's grid), and its mean luma: at night its bright pools light the floor.
void BuildLightmap(uint64_t area) {
  g_lm_area = area;
  g_lm_ok = false;
  int aw = g_scene.size_w, ah = g_scene.size_h;
  if ((aw <= 0 || ah <= 0) && !g_scene.height.empty()) {
    aw = g_scene.height.w * g_scene.height.texel;
    ah = g_scene.height.h * g_scene.height.texel;
  }
  if (aw <= 0 || ah <= 0 || aw > 32768 || ah > 32768) return;
  const int w = (aw + 15) / 16, h = (ah + 11) / 12;
  std::vector<uint8_t> px(size_t(w) * h * 4);
  double sum = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const uint32_t c = engine::AreaTintColor(area, x * 16 + 8, y * 12 + 6);
      if (c == 0xFFFFFFFFu) return;
      uint8_t* p = &px[(size_t(y) * w + x) * 4];
      p[0] = uint8_t(c & 0xFF);
      p[1] = uint8_t((c >> 8) & 0xFF);
      p[2] = uint8_t((c >> 16) & 0xFF);
      p[3] = 255;
      sum += Luma(TintRgb(c));
    }
  g_lm_mean = float(sum / (double(w) * h));
  g_lm_w = w;
  g_lm_h = h;
  auto& gl = g_gl;
  if (!g_lm_tex) g_lm_tex = NewTexture(kLinear);
  gl.ActiveTexture(kTexture0);
  gl.BindTexture(kTex2D, g_lm_tex);
  gl.PixelStorei(kUnpackAlign, 1);
  gl.TexImage2D(kTex2D, 0, kRgba8, w, h, 0, kRgba, kUByte, px.data());
  g_lm_ok = true;
  Log("lighting: %s: lightmap %dx%d cells, mean luma %.3f", g_wed.c_str(), w, h, g_lm_mean);
}

// Day / night of this frame: the engine's time-of-day tint (undone in the composite), our ambient for the
// time of day, the night light gain. Areas without the cycle (or [lighting] day_night=0) stay as by day.
void DayNightFrame(uint64_t area) {
  engine::GlobalLight g;
  const bool have = engine::AreaGlobalLight(area, &g);
  g_engine_tint = have ? TintRgb(g.applied) : Rgb{1, 1, 1};
  g_night = (have && g_set.day_night) ? Nightness(g.tod, g_set.dn.night_luma) : 0.0f;
  if (!g_set.day_night) g_engine_tint = {1, 1, 1};
  g_amb = TimeOfDayAmbient(g_ambient, g_night, g_set.dn);
  g_night_gain = NightLightGain(g_night, g_set.dn);
  if (g_lm_area != area && g_set.day_night && g_set.dn.lightmap > 0) BuildLightmap(area);
  static float logged = -1;
  if (std::fabs(g_night - logged) > 0.05f) {
    logged = g_night;
    Log("lighting: time of day: tod %06X applied %06X night %.2f ambient %.3f %.3f %.3f light x%.2f", g.tod, g.applied,
        g_night, g_amb.r, g_amb.g, g_amb.b, g_night_gain);
  }
}

void ReadBack(int x, int y, int w, int h, std::vector<uint8_t>* out) {
  out->resize(size_t(w) * h * 4);
  g_gl.PixelStorei(kPackAlign, 1);
  g_gl.ReadPixels(x, y, w, h, kRgba, kUByte, out->data());
}

// Probe: mean luma (0..255) of the composited creature pixels (sprite buffer and coverage), read back from
// the engine's target right after our composite.
void SpriteMean(int draw_fbo, int vx, int vy, int vw, int vh) {
  auto& gl = g_gl;
  std::vector<float> info(size_t(vw) * vh * 4);
  gl.BindFramebuffer(0x8CA8 /*read*/, g_info_fbo);
  gl.PixelStorei(kPackAlign, 4);
  gl.ReadPixels(0, 0, vw, vh, kRgba, kFloat, info.data());
  std::vector<uint8_t> cov(size_t(g_cov_w) * g_cov_h * 4), px;
  gl.BindFramebuffer(0x8CA8 /*read*/, g_cov_fbo);
  gl.PixelStorei(kPackAlign, 1);
  gl.ReadPixels(0, 0, g_cov_w, g_cov_h, kRgba, kUByte, cov.data());
  gl.BindFramebuffer(0x8CA8 /*read*/, unsigned(draw_fbo));
  ReadBack(vx, vy, vw, vh, &px);
  double sum = 0;
  long long n = 0;
  for (int y = 0; y < vh; ++y)
    for (int x = 0; x < vw; ++x) {
      const size_t i = size_t(y) * vw + x;
      const int cx = x + vx, cy = y + vy;
      if (info[i * 4 + 1] < 0.0f || cx >= g_cov_w || cy >= g_cov_h || cov[(size_t(cy) * g_cov_w + cx) * 4 + 3] <= 127)
        continue;
      sum += 0.299 * px[i * 4] + 0.587 * px[i * 4 + 1] + 0.114 * px[i * 4 + 2];
      ++n;
    }
  g_sprmean = n ? sum / double(n) : -1;
  g_sprmean_px = n;
  g_sprmean_req = false;
  Log("lighting: creature pixels %lld, mean luma %.2f (sprite_ao %.2f)", n, g_sprmean, g_set.sprite_ao);
}

// The composite (engine/gl_world.h): once per frame, after all tiles and objects, before fog.
void Composite() {
  if (g_mode == DebugMode::Off || g_off) return;
  const uint64_t area = engine::WorldPassArea();
  if (!area) return;
  g_last_area = area;
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
  DayNightFrame(area);

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
  g_last_vm = vm;
  g_last_ox = ox;
  g_last_oy = oy;
  g_last_vw = vw;
  g_last_vh = vh;
  g_last_vx = vx;
  g_last_vy = vy;
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
  // 2. glow draws -> effect lights, glow mask, bloom; light buffer (all skipped for the round trip)
  const float to_phys = float(vw) / float(g_light_w);
  const bool has_h = g_height_tex && !g_scene.height.empty();
  if (g_mode != DebugMode::RoundTrip) {
    const std::vector<BakedLight> fx = GatherFx(ox, oy, vm, vw, vh);
    SpritePass(vm, ox, oy, to_phys, vx, vy, vw, vh, has_h);
    LightPass(vm, to_phys, fx);
    PaintPass(vm, vx, vy, vw, vh);
    BloomPass(to_phys, vx, vy);
  }
  if (g_cov_req && g_cap_used && !g_glows.empty()) {
    std::vector<uint8_t> px(size_t(g_cap_w) * g_cap_h * 4);
    gl.BindFramebuffer(0x8CA8 /*read*/, g_emis_fbo);
    gl.PixelStorei(kPackAlign, 1);
    gl.ReadPixels(0, 0, g_cap_w, g_cap_h, kRgba, kUByte, px.data());
    long long lit = 0;
    for (size_t i = 0; i < px.size(); i += 4)
      if (std::max(px[i], std::max(px[i + 1], px[i + 2])) > 10) ++lit;
    double rects = 0;
    for (const GlowDraw& g : g_glows) rects += double(g.w) * g.h / std::fabs(double(vm.ax) * vm.ay);
    g_cov = rects > 0 ? double(lit) / rects : -1;
    g_cov_req = false;
    Log("lighting: emissive capture covers %lld px for %zu draws (%.0f px of frame rects): ratio %.3f", lit,
        g_glows.size(), rects, g_cov);
  }
  // 3. composite back into the engine's target
  gl.BindFramebuffer(kFramebuffer, static_cast<unsigned>(saved.draw_fbo));
  gl.Viewport(vx, vy, vw, vh);
  gl.Disable(kBlend);
  gl.UseProgram(g_comp_prog);
  gl.Uniform1i(g_cu.scene, 0);
  gl.Uniform1i(g_cu.light, 1);
  gl.Uniform1i(g_cu.height, 2);
  gl.Uniform1i(g_cu.emis, 4);
  gl.Uniform1i(g_cu.bloom, 5);
  gl.Uniform2f(g_cu.vp_origin, float(vx), float(vy));
  gl.Uniform2f(g_cu.vp_size, float(vw), float(vh));
  gl.Uniform1i(g_cu.mode, static_cast<int>(g_mode));
  gl.Uniform1f(g_cu.keep, g_set.keep);
  gl.Uniform1f(g_cu.max_light, g_set.max_light);
  gl.Uniform4f(g_cu.map, vm.ax, vm.ay, vm.bx, vm.by);
  gl.Uniform1f(g_cu.texel, float(g_scene.height.texel > 0 ? g_scene.height.texel : 2));
  gl.Uniform1i(g_cu.has_height, has_h ? 1 : 0);
  gl.Uniform1f(g_cu.emis_keep, g_set.emissive && g_cap_used ? g_set.emissive_keep : 0.0f);
  gl.Uniform1f(g_cu.bloom_strength, g_set.bloom && g_cap_used ? g_set.bloom_strength : 0.0f);
  const Atmosphere atmo;
  gl.Uniform4f(g_cu.atmo, atmo.haze, atmo.desat, atmo.contrast, g_set.atmosphere ? g_set.atmosphere_strength : 0.0f);
  const Rgb hz = HazeColour(g_scene, g_amb);
  gl.Uniform3f(g_cu.haze_colour, hz.r, hz.g, hz.b);
  gl.Uniform1f(g_cu.fx_boost, g_set.emissive && g_cap_used ? g_set.fx_boost : 0.0f);
  gl.Uniform1f(g_cu.fringe, g_set.fx_fringe);
  const bool spr_on = g_sprites_valid && g_mode != DebugMode::RoundTrip;
  gl.Uniform1i(g_cu.sprites, spr_on ? 1 : 0);
  gl.Uniform1i(g_cu.info, 6);
  gl.Uniform1i(g_cu.cov, 7);
  gl.Uniform1i(g_cu.shadow, 3);
  gl.Uniform3f(g_cu.ambient, g_amb.r, g_amb.g, g_amb.b);
  gl.Uniform3f(g_cu.engine_tint, g_engine_tint.r, g_engine_tint.g, g_engine_tint.b);
  const bool lm_on = g_lm_ok && g_lm_area == area && g_set.day_night;
  gl.Uniform1f(g_cu.lm_pool, lm_on ? g_set.dn.lightmap * g_night : 0.0f);
  gl.Uniform1f(g_cu.lm_mean, g_lm_mean);
  gl.Uniform2f(g_cu.lm_area, float(g_lm_w * 16), float(g_lm_h * 12));
  gl.Uniform1i(g_cu.lm, 8);
  gl.ActiveTexture(kTexture0 + 8);
  gl.BindTexture(kTex2D, lm_on ? g_lm_tex : 0);
  const bool ao_on = g_set.sprite_ao > 0 && g_ao_tex && !g_scene.ao.empty();
  gl.Uniform1i(g_cu.ao, 9);
  gl.Uniform1f(g_cu.sprite_ao, ao_on ? g_set.sprite_ao : 0.0f);
  gl.Uniform1f(g_cu.ao_texel, float(g_scene.ao.texel > 0 ? g_scene.ao.texel : 2));
  gl.ActiveTexture(kTexture0 + 9);
  gl.BindTexture(kTex2D, ao_on ? g_ao_tex : 0);
  // The shadow buffer holds the final darkening (key and contact passes write their own strength).
  gl.Uniform1f(g_cu.shadow_strength, g_shadow_drawn && g_mode != DebugMode::RoundTrip ? 1.0f : 0.0f);
  gl.Uniform1i(g_cu.cov_valid, g_cov_used ? 1 : 0);
  gl.Uniform1i(g_cu.fxm, 10);
  gl.Uniform1i(g_cu.fxm_valid, g_fxm_used ? 1 : 0);
  gl.ActiveTexture(kTexture0 + 10);
  gl.BindTexture(kTex2D, g_fxm_tex);
  gl.ActiveTexture(kTexture0 + 7);
  gl.BindTexture(kTex2D, g_cov_tex);
  gl.ActiveTexture(kTexture0 + 6);
  gl.BindTexture(kTex2D, g_info_tex);
  gl.ActiveTexture(kTexture0 + 3);
  gl.BindTexture(kTex2D, g_shadow_tex);
  gl.ActiveTexture(kTexture0 + 5);
  gl.BindTexture(kTex2D, g_bloom_tex[0]);
  gl.ActiveTexture(kTexture0 + 4);
  gl.BindTexture(kTex2D, g_emis_tex);
  gl.ActiveTexture(kTexture0 + 2);
  gl.BindTexture(kTex2D, has_h ? g_height_tex : 0);
  gl.ActiveTexture(kTexture0 + 1);
  gl.BindTexture(kTex2D, g_light_tex);
  gl.ActiveTexture(kTexture0);
  gl.BindTexture(kTex2D, g_scene_tex);
  if (g_spr_check_req && g_sprites_valid) {
    SpriteCheck(vx, vy, vw, vh);
    gl.BindFramebuffer(kFramebuffer, static_cast<unsigned>(saved.draw_fbo));
  }
  Quad(-1, -1, 1, 1);
  if (g_sprmean_req && g_sprites_valid && g_mode == DebugMode::Lit) SpriteMean(saved.draw_fbo, vx, vy, vw, vh);

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

void PreDraw() {
  engine::ArmWorldComposite();
  g_cap_used = false;
  g_cov_used = false;
  g_fxm_used = false;
}

int EmitDecision(const char* res, uint16_t cycle, uint16_t frame, uint32_t flags) {
  if (g_mode == DebugMode::Off || g_mode == DebugMode::RoundTrip) return 0;  // debug "vanilla" modes
  if (g_set.hand_lights && FindHandLight(g_hand, res)) return 2;           // light, no glow mask
  if (g_set.hand_lights && g_set.emissive && res && res[0])
    for (const HandLight& h : g_hand)
      if (h.glow[0] && MatchPattern(h.glow, res)) return 1 | 8;  // Ignus: his BAMs glow, all of him unlit
  if (!(g_set.emissive || g_set.bloom || g_set.fx_lights)) return 0;
  const GlowOverride* over = FindGlowOverride(g_glow_over, res);
  if (over && !over->glow) return 0;
  const bool glow = IsGlowBlend(flags);
  const bool listed = !glow && (over != nullptr || Emits(g_emit_names, res, 0));
  // Not at night: the screen blend adds the sprite untinted, so a dark-grey part the engine's own (tinted) draw
  // keeps near black there shows at full strength (Spiritual Hammer's oval became an opaque glowing blob).
  // Painting black over the scene, what the soften is for, does not show on a night scene anyway.
  const int soft = (listed && g_set.fx_soften && SoftenAt(g_night) && (!over || over->soften)) ? 4 : 0;
  static int logged = 0;
  if ((glow || listed) && logged < 40) {
    ++logged;
    Log("lighting: emit '%s' cycle %u frame %u flags %08X (%s)", res, cycle, frame, flags, glow ? "additive" : "listed");
  }
  return (glow || listed) ? (1 | soft) : 0;
}

// (Re)sizes the emissive buffer to w x h window px. Leaves the draw framebuffer as `prev_draw` and the read
// framebuffer, active texture and its binding as they were.
bool EnsureEmisBuffer(int w, int h, int prev_draw) {
  if (w == g_cap_w && h == g_cap_h) return true;
  auto& gl = g_gl;
  int prev_read = 0;
  gl.GetIntegerv(0x8CAA /*read fbo*/, &prev_read);
  int active = 0, bound = 0;
  gl.GetIntegerv(0x84E0, &active);
  gl.ActiveTexture(kTexture0);
  gl.GetIntegerv(0x8069, &bound);
  gl.BindTexture(kTex2D, g_emis_tex);
  gl.TexImage2D(kTex2D, 0, kRgba8, w, h, 0, kRgba, kUByte, nullptr);
  gl.BindTexture(kTex2D, unsigned(bound));
  gl.ActiveTexture(unsigned(active));
  gl.BindFramebuffer(kFramebuffer, g_emis_fbo);
  gl.FramebufferTexture2D(kFramebuffer, kColorAttach0, kTex2D, g_emis_tex, 0);
  const bool complete = gl.CheckFramebufferStatus(kFramebuffer) == kFbComplete;
  gl.BindFramebuffer(0x8CA8 /*read*/, unsigned(prev_read));  // GL_FRAMEBUFFER bound both: restore read
  gl.BindFramebuffer(0x8CA9 /*draw*/, unsigned(prev_draw));
  if (!complete) {
    Log("lighting: emissive capture %dx%d incomplete", w, h);
    g_cap_w = g_cap_h = 0;
    return false;
  }
  g_cap_w = w;
  g_cap_h = h;
  return true;
}

// Binds the emissive buffer (cleared on the first capture of the frame) as the draw target. `whole`: the
// draw's alpha goes into the buffer too (a self-lit creature: the composite keeps all its pixels unlit).
int g_cap_mask[4] = {1, 1, 1, 1};
bool CaptureBegin(bool whole) {
  if (!g_ready || !(g_set.emissive || g_set.bloom)) return false;
  auto& gl = g_gl;
  int vp[4];
  gl.GetIntegerv(kViewport, vp);
  const int w = vp[0] + vp[2], h = vp[1] + vp[3];
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return false;
  gl.GetIntegerv(0x8CA6 /*draw fbo*/, &g_cap_prev_fbo);
  if (!EnsureEmisBuffer(w, h, g_cap_prev_fbo)) return false;
  gl.BindFramebuffer(0x8CA9 /*draw*/, g_emis_fbo);
  gl.GetIntegerv(0x0C23 /*colour writemask*/, g_cap_mask);  // the engine's, put back by CaptureEnd
  if (!g_cap_used) {
    gl.ColorMask(1, 1, 1, 1);  // the clear zeroes alpha too (the self-lit cover)
    float cc[4];
    gl.GetFloatv(0x0C22, cc);
    const bool sc = gl.IsEnabled(kScissor) != 0;
    if (sc) gl.Disable(kScissor);
    gl.ClearColor(0, 0, 0, 0);
    gl.Clear(kColorBit);
    gl.ClearColor(cc[0], cc[1], cc[2], cc[3]);
    if (sc) gl.Enable(kScissor);
    g_cap_used = true;
  }
  gl.ColorMask(1, 1, 1, whole && g_set.self_lit ? 1 : 0);
  ++g_cap_draws;
  return true;
}

void CaptureEnd() {
  g_gl.ColorMask(g_cap_mask[0] != 0, g_cap_mask[1] != 0, g_cap_mask[2] != 0, g_cap_mask[3] != 0);
  g_gl.BindFramebuffer(0x8CA9 /*draw*/, unsigned(g_cap_prev_fbo));
}

// Creature coverage (sprite buffer): same as the emissive capture, own buffer.
bool SpriteWant(const char* res) {
  if (!g_ready || !g_set.sprite_lighting || g_mode == DebugMode::Off || g_mode == DebugMode::RoundTrip) return false;
  return HasDepth(res);
}

bool CovBegin() {
  if (!g_ready) return false;
  auto& gl = g_gl;
  int vp[4];
  gl.GetIntegerv(kViewport, vp);
  const int w = vp[0] + vp[2], h = vp[1] + vp[3];
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return false;
  gl.GetIntegerv(0x8CA6 /*draw fbo*/, &g_cov_prev_fbo);
  if (w != g_cov_w || h != g_cov_h) {
    int prev_read = 0, active = 0, bound = 0;
    gl.GetIntegerv(0x8CAA /*read fbo*/, &prev_read);
    gl.GetIntegerv(0x84E0, &active);
    gl.ActiveTexture(kTexture0);
    gl.GetIntegerv(0x8069, &bound);
    gl.BindTexture(kTex2D, g_cov_tex);
    gl.TexImage2D(kTex2D, 0, kRgba8, w, h, 0, kRgba, kUByte, nullptr);
    gl.BindTexture(kTex2D, unsigned(bound));
    gl.ActiveTexture(unsigned(active));
    gl.BindFramebuffer(kFramebuffer, g_cov_fbo);
    gl.FramebufferTexture2D(kFramebuffer, kColorAttach0, kTex2D, g_cov_tex, 0);
    const bool complete = gl.CheckFramebufferStatus(kFramebuffer) == kFbComplete;
    gl.BindFramebuffer(0x8CA8 /*read*/, unsigned(prev_read));
    if (!complete) {
      Log("lighting: coverage buffer %dx%d incomplete", w, h);
      gl.BindFramebuffer(0x8CA9 /*draw*/, unsigned(g_cov_prev_fbo));
      return false;
    }
    g_cov_w = w;
    g_cov_h = h;
  }
  gl.BindFramebuffer(0x8CA9 /*draw*/, g_cov_fbo);
  gl.GetIntegerv(0x0C23 /*colour writemask*/, g_cov_mask);
  gl.ColorMask(1, 1, 1, 1);  // the engine draws with alpha writes off: coverage lives in alpha
  if (!g_cov_used) {
    float cc[4];
    gl.GetFloatv(0x0C22, cc);
    const bool sc = gl.IsEnabled(kScissor) != 0;
    if (sc) gl.Disable(kScissor);
    gl.ClearColor(0, 0, 0, 0);
    gl.Clear(kColorBit);
    gl.ClearColor(cc[0], cc[1], cc[2], cc[3]);
    if (sc) gl.Enable(kScissor);
    g_cov_used = true;
  }
  return true;
}

void CovEnd() {
  g_gl.ColorMask(g_cov_mask[0] != 0, g_cov_mask[1] != 0, g_cov_mask[2] != 0, g_cov_mask[3] != 0);
  g_gl.BindFramebuffer(0x8CA9 /*draw*/, unsigned(g_cov_prev_fbo));
}

// Effect mask (gl_world SetFxMaskCallbacks): kind 1 writes alpha only, 2 colour only.
bool FxMaskBegin(int kind) {
  if (!g_ready || g_mode == DebugMode::Off || g_mode == DebugMode::RoundTrip || !g_set.untinted_fx) return false;
  auto& gl = g_gl;
  int vp[4];
  gl.GetIntegerv(kViewport, vp);
  const int w = vp[0] + vp[2], h = vp[1] + vp[3];
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return false;
  gl.GetIntegerv(0x8CA6 /*draw fbo*/, &g_fxm_prev_fbo);
  if (w != g_fxm_w || h != g_fxm_h) {
    int prev_read = 0, active = 0, bound = 0;
    gl.GetIntegerv(0x8CAA /*read fbo*/, &prev_read);
    gl.GetIntegerv(0x84E0, &active);
    gl.ActiveTexture(kTexture0);
    gl.GetIntegerv(0x8069, &bound);
    gl.BindTexture(kTex2D, g_fxm_tex);
    gl.TexImage2D(kTex2D, 0, kRgba8, w, h, 0, kRgba, kUByte, nullptr);
    gl.BindTexture(kTex2D, unsigned(bound));
    gl.ActiveTexture(unsigned(active));
    gl.BindFramebuffer(kFramebuffer, g_fxm_fbo);
    gl.FramebufferTexture2D(kFramebuffer, kColorAttach0, kTex2D, g_fxm_tex, 0);
    const bool complete = gl.CheckFramebufferStatus(kFramebuffer) == kFbComplete;
    gl.BindFramebuffer(0x8CA8 /*read*/, unsigned(prev_read));
    if (!complete) {
      Log("lighting: effect mask %dx%d incomplete", w, h);
      gl.BindFramebuffer(0x8CA9 /*draw*/, unsigned(g_fxm_prev_fbo));
      g_fxm_w = g_fxm_h = 0;
      return false;
    }
    g_fxm_w = w;
    g_fxm_h = h;
  }
  gl.BindFramebuffer(0x8CA9 /*draw*/, g_fxm_fbo);
  gl.GetIntegerv(0x0C23 /*colour writemask*/, g_fxm_mask);
  if (!g_fxm_used) {
    gl.ColorMask(1, 1, 1, 1);
    float cc[4];
    gl.GetFloatv(0x0C22, cc);
    const bool sc = gl.IsEnabled(kScissor) != 0;
    if (sc) gl.Disable(kScissor);
    gl.ClearColor(0, 0, 0, 0);
    gl.Clear(kColorBit);
    gl.ClearColor(cc[0], cc[1], cc[2], cc[3]);
    if (sc) gl.Enable(kScissor);
    g_fxm_used = true;
  }
  // kind 1 (normal blend): colour and alpha. FX-surface blits are premultiplied: a spell's glow pixels have
  // alpha 0 and add their colour (Spiritual Hammer's oval), a creature's are opaque. kind 2: colour only.
  if (kind == 1) gl.ColorMask(1, 1, 1, 1);
  else gl.ColorMask(1, 1, 1, 0);
  ++g_fxm_draws;
  return true;
}

void FxMaskEnd() {
  g_gl.ColorMask(g_fxm_mask[0] != 0, g_fxm_mask[1] != 0, g_fxm_mask[2] != 0, g_fxm_mask[3] != 0);
  g_gl.BindFramebuffer(0x8CA9 /*draw*/, unsigned(g_fxm_prev_fbo));
}

// --- Lua probes ------------------------------------------------------------------------------------
// PSTF_LightDebug(mode) -> mode name. 0 lit, 1 light buffer, 2 round trip, 3 falloff, 4 off, 5 emissive.
int __cdecl LuaDebug(engine::LuaState* L) {
  const int m = static_cast<int>(engine::LuaNumberArg(L, 1, -1));
  if (m >= 0 && m <= 8) g_mode = static_cast<DebugMode>(m);
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

// PSTF_LightFx() -> glow draws, table hits, effect lights (this frame), frames with an effect light,
// and the strongest effect light's drawn point x, y (area px; -1 without one). Logs a summary.
// PSTF_LightBloom() -> brightest bloom texel measured on the last frame with glow draws (asks for a new
// measurement on the next such frame).
int __cdecl LuaBloom(engine::LuaState* L) {
  engine::LuaPushNumber(L, g_bloom_max);
  g_bloom_req = true;
  return 1;
}

// PSTF_LightAtmo() -> share of the last frame's pixels the atmosphere changes (t > 0.01, every 8th
// pixel, CPU from the height map), mean t over those. Logs.
int __cdecl LuaAtmo(engine::LuaState* L) {
  const Atmosphere a;
  int n = 0, hit = 0;
  double sum = 0;
  for (int y = 0; y < g_last_vh; y += 8)
    for (int x = 0; x < g_last_vw; x += 8) {
      float mx, my;
      ToArea(g_last_vm, x + 0.5f, y + 0.5f, &mx, &my);
      const float t = AtmosphereT(a, HeightAt(g_scene.height, mx, my)) * g_set.atmosphere_strength;
      ++n;
      if (g_set.atmosphere && t > 0.01f) {
        ++hit;
        sum += t;
      }
    }
  const double share = n ? double(hit) / n : 0.0, mean = hit ? sum / hit : 0.0;
  Log("lua: PSTF_LightAtmo -> %s: %.3f of the view hazed, mean t %.3f", g_wed.c_str(), share, mean);
  engine::LuaPushNumber(L, share);
  engine::LuaPushNumber(L, mean);
  return 2;
}

// PSTF_LightAo(x, y) -> sprite AO factor at area px (x, y) with the current sprite_ao, raw ao (0..1, 1 = open or
// no map), hand area lights in this area, painted emitters drawn last frame.
int __cdecl LuaAo(engine::LuaState* L) {
  const float x = float(engine::LuaNumberArg(L, 1, 0)), y = float(engine::LuaNumberArg(L, 2, 0));
  const float ao = AoAt(g_scene.ao, x, y);
  const float f = SpriteAoFactor(ao, g_set.sprite_ao);
  Log("lua: PSTF_LightAo(%.0f, %.0f) -> factor %.3f ao %.3f, area lights %zu, painted %d", x, y, f, ao, g_area_added,
      g_painted);
  engine::LuaPushNumber(L, f);
  engine::LuaPushNumber(L, ao);
  engine::LuaPushNumber(L, double(g_area_added));
  engine::LuaPushNumber(L, g_painted);
  return 4;
}

// PSTF_LightSpriteMean() -> mean luma (0..255) of the lit creature pixels and their count, measured on the
// last frame asked for (asks again for the next frame; -1 before the first measurement).
int __cdecl LuaSpriteMean(engine::LuaState* L) {
  engine::LuaPushNumber(L, g_sprmean);
  engine::LuaPushNumber(L, double(g_sprmean_px));
  g_sprmean_req = true;
  return 2;
}

// PSTF_LightSet(name, value) -> 1 if known: runtime toggles for A/B tests (ini names; percents as 0..300).
int __cdecl LuaSet(engine::LuaState* L) {
  const std::string k = engine::LuaStringArg(L, 1);
  const double v = engine::LuaNumberArg(L, 2, 0);
  bool ok = true;
  if (k == "atmosphere") g_set.atmosphere = v != 0;
  else if (k == "atmosphere_strength") g_set.atmosphere_strength = float(v) / 100.0f;
  else if (k == "bloom") g_set.bloom = v != 0;
  else if (k == "bloom_strength") g_set.bloom_strength = float(v) / 100.0f;
  else if (k == "emissive") g_set.emissive = v != 0;
  else if (k == "fx_lights") g_set.fx_lights = v != 0;
  else if (k == "fx_soften") g_set.fx_soften = v != 0;
  else if (k == "untinted_fx") g_set.untinted_fx = v != 0;
  else if (k == "self_lit") g_set.self_lit = v != 0;
  else if (k == "sprite_lighting") g_set.sprite_lighting = v != 0;
  else if (k == "sprite_occlusion") g_set.sprite_occlusion = v != 0;
  else if (k == "key_shadows") g_set.key_shadows = v != 0;
  else if (k == "contact_shadows") g_set.contact_shadows = v != 0;
  else if (k == "contact_shadow_strength") g_set.contact_shadow_strength = float(v) / 100.0f;
  else if (k == "contact_shadow_size") g_set.contact_shadow_size = float(v);
  else if (k == "contact_shadow_flat") g_set.contact_shadow_flat = float(v) / 100.0f;
  else if (k == "sprite_shadow_strength") g_set.sprite_shadow_strength = float(v) / 100.0f;
  else if (k == "key_shadow_length") g_set.key_shadow_length = float(v) / 100.0f;
  else if (k == "hand_lights") g_set.hand_lights = v != 0;
  else if (k == "day_night") g_set.day_night = v != 0;
  else if (k == "night_level") g_set.dn.night_level = float(v) / 100.0f;
  else if (k == "night_light_gain") g_set.dn.night_light = float(v) / 100.0f;
  else if (k == "dusk_warmth") g_set.dn.dusk_warmth = float(v) / 100.0f;
  else if (k == "lightmap_night") g_set.dn.lightmap = float(v) / 100.0f;
  else if (k == "night_tint_luma") g_set.dn.night_luma = float(v) / 100.0f;
  else if (k == "key_shadow_strength") g_set.key_shadow_strength = float(v) / 100.0f;
  else if (k == "sprite_normal_strength") g_set.sprite_normal_strength = float(v) / 100.0f;
  else if (k == "sprite_light_gain") g_set.sprite_light_gain = float(v) / 100.0f;
  else if (k == "fx_boost") g_set.fx_boost = float(v) / 100.0f;
  else if (k == "fx_fringe") g_set.fx_fringe = float(v);
  else if (k == "bloom_passes") g_set.bloom_passes = int(v);
  else if (k == "baked_lights") g_set.baked_lights = v != 0;
  else if (k == "sprite_ao") g_set.sprite_ao = float(v) / 100.0f;
  else if (k == "painted_glow") g_set.painted_glow = v != 0;
  else if (k == "painted_glow_gain") g_set.painted_glow_gain = float(v) / 100.0f;
  else if (k == "area_lights") {
    g_set.area_lights = v != 0;
    g_wed.clear();  // reload the area's lights on the next frame
  }
  else if (k == "occlusion") g_set.occlusion = v != 0;
  else if (k == "light_gain") g_set.light_gain = float(v) / 100.0f;
  else if (k == "ambient") {
    g_set.ambient = float(v) / 100.0f;
    g_ambient = Ambient(g_area_type, g_set.ambient, &g_scene, g_set.key_light);
  } else ok = false;
  Log("lua: PSTF_LightSet(%s, %g) -> %d", k.c_str(), v, ok ? 1 : 0);
  engine::LuaPushNumber(L, ok ? 1 : 0);
  return 1;
}

// PSTF_LightTrace(prefix): log how cells whose resref starts with prefix are drawn (path, flags).
int __cdecl LuaTrace(engine::LuaState* L) {
  const std::string p = engine::LuaStringArg(L, 1);
  engine::TraceDraws(p.c_str());
  engine::LuaPushNumber(L, 1);
  return 1;
}

// PSTF_LightCoverage() -> last measured capture coverage (lit capture px / frame-rect px; -1 none yet) and
// asks for a new measurement. A per-pixel capture stays well below 1; the old rect mask was exactly 1.
int __cdecl LuaCoverage(engine::LuaState* L) {
  engine::LuaPushNumber(L, g_cov);
  engine::LuaPushNumber(L, static_cast<double>(g_cap_draws));
  g_cov_req = true;
  return 2;
}

int __cdecl LuaFx(engine::LuaState* L) {
  float lx = -1, ly = -1;
  if (!g_last_fx.empty()) {
    lx = g_last_fx[0].x;
    ly = g_last_fx[0].y - g_last_fx[0].z;
  }
  uint64_t rt = 0, rtw = 0, r3 = 0;
  uint32_t fl = 0;
  engine::GlowStats(&rt, &rtw, &r3, &fl);
  static int calls = 0;
  if (g_fx_glows > 0 || ++calls % 50 == 1)
  Log("lua: PSTF_LightFx -> glows %d hits %d lights %d frames %llu dropped %d top %.0f,%.0f (RenderTexture %llu, in "
      "world %llu, Render3d %llu, flags %08X)",
      g_fx_glows, g_fx_hits, g_fx_lights, static_cast<unsigned long long>(g_fx_frames_with_light),
      engine::GlowDrawsDropped(), lx, ly, static_cast<unsigned long long>(rt), static_cast<unsigned long long>(rtw),
      static_cast<unsigned long long>(r3), fl);
  engine::LuaPushNumber(L, g_fx_glows);
  engine::LuaPushNumber(L, g_fx_hits);
  engine::LuaPushNumber(L, g_fx_lights);
  engine::LuaPushNumber(L, static_cast<double>(g_fx_frames_with_light));
  engine::LuaPushNumber(L, lx);
  engine::LuaPushNumber(L, ly);
  engine::LuaPushNumber(L, g_hand_lights);
  engine::LuaPushNumber(L, g_buff_lights);
  return 8;
}

// PSTF_LightFxCells() -> FX-surface blits, pure-effect blits, softened draws, cells (session totals), and
// the last frame's FX-surface cell draws as "RES cycle/frame anchor x,y rect w x h pure;..." (area px).
int __cdecl LuaFxCells(engine::LuaState* L) {
  uint64_t blits = 0, pure = 0, soft = 0, cells = 0;
  engine::FxSurfaceStats(&blits, &pure, &soft, &cells);
  const float ox = g_last_ox, oy = g_last_oy;
  int n = 0;
  const engine::EngineGlowDraw* d = engine::GlowDraws(&n);
  std::string list;
  char buf[128];
  for (int i = 0; i < n && list.size() < 900; ++i) {
    if (!d[i].from_fx) continue;
    snprintf(buf, sizeof buf, "%s %u/%u anchor %.0f,%.0f rect %dx%d pure %d;", d[i].resref, d[i].cycle, d[i].frame,
             d[i].anchor_x + ox, d[i].anchor_y + oy, d[i].w, d[i].h, d[i].pure);
    list += buf;
  }
  Log("lua: PSTF_LightFxCells -> blits %llu pure %llu softened %llu cells %llu | %s",
      static_cast<unsigned long long>(blits), static_cast<unsigned long long>(pure),
      static_cast<unsigned long long>(soft), static_cast<unsigned long long>(cells), list.c_str());
  engine::LuaPushNumber(L, double(blits));
  engine::LuaPushNumber(L, double(pure));
  engine::LuaPushNumber(L, double(soft));
  engine::LuaPushNumber(L, double(cells));
  engine::LuaPushString(L, list.c_str());
  return 5;
}

// PSTF_LightCreatureTrace(frames) starts a per-drawn-frame trace of the (first) creature light; with no
// argument -> samples, moving samples, holds (a moving light that did not move this frame), largest step px,
// largest step px of the creature's sprite draw anchor.
int __cdecl LuaCreatureTrace(engine::LuaState* L) {
  const int frames = int(engine::LuaNumberArg(L, 1, 0));
  if (frames > 0) {
    g_cl_trace.clear();
    g_cl_trace_anchor.clear();
    g_cl_trace_left = std::min(frames, 2000);
    Log("lua: PSTF_LightCreatureTrace(%d) started", frames);
    engine::LuaPushNumber(L, 1);
    return 1;
  }
  int moving = 0, holds = 0;
  float big = 0;
  const auto& v = g_cl_trace;
  for (size_t i = 1; i + 1 < v.size(); ++i) {
    const float d0 = std::hypot(v[i].first - v[i - 1].first, v[i].second - v[i - 1].second);
    const float span = std::hypot(v[i + 1].first - v[i - 1].first, v[i + 1].second - v[i - 1].second);
    big = std::max(big, d0);
    if (span < 0.5f) continue;  // standing still around this frame
    ++moving;
    if (d0 < 0.25f || std::hypot(v[i + 1].first - v[i].first, v[i + 1].second - v[i].second) < 0.25f) ++holds;
  }
  float big_anchor = 0;
  const auto& a = g_cl_trace_anchor;
  for (size_t i = 1; i < a.size(); ++i)
    big_anchor = std::max(big_anchor, std::hypot(a[i].first - a[i - 1].first, a[i].second - a[i - 1].second));
  Log("lua: PSTF_LightCreatureTrace -> samples %zu moving %d holds %d largest step %.2f (sprite anchor %.2f)", v.size(),
      moving, holds, big, big_anchor);
  engine::LuaPushNumber(L, double(v.size()));
  engine::LuaPushNumber(L, moving);
  engine::LuaPushNumber(L, holds);
  engine::LuaPushNumber(L, big);
  engine::LuaPushNumber(L, big_anchor);
  return 5;
}

// PSTF_LightGlobal() -> the engine's global lighting of the visible area: tod tint, applied tint (0xBBGGRR),
// day/night flag (1/0). Logs.
int __cdecl LuaGlobal(engine::LuaState* L) {
  engine::GlobalLight g;
  const bool ok = engine::AreaGlobalLight(g_last_area, &g);
  Log("lua: PSTF_LightGlobal -> %s tod %06X weather %06X add %06X applied %06X daynight %d", ok ? "ok" : "none", g.tod,
      g.weather, g.add, g.applied, g.day_night ? 1 : 0);
  engine::LuaPushNumber(L, double(g.tod));
  engine::LuaPushNumber(L, double(g.applied));
  engine::LuaPushNumber(L, g.day_night ? 1 : 0);
  return 3;
}

// PSTF_LightShadows() -> contact shadows drawn last frame, BAM blobs hidden (1/0), shadow buffer used (1/0),
// and the last creature light's x, y (area px) and red channel (-1 without one).
bool g_bam_hidden = false;
int __cdecl LuaShadows(engine::LuaState* L) {
  Log("lua: PSTF_LightShadows -> contact %d bam_hidden %d buffer %d (key %d)", g_contact_drawn, g_bam_hidden ? 1 : 0,
      g_shadow_drawn ? 1 : 0, g_scene.has_key ? 1 : 0);
  engine::LuaPushNumber(L, g_contact_drawn);
  engine::LuaPushNumber(L, g_bam_hidden ? 1 : 0);
  engine::LuaPushNumber(L, g_shadow_drawn ? 1 : 0);
  engine::LuaPushNumber(L, g_cl_x);
  engine::LuaPushNumber(L, g_cl_y);
  engine::LuaPushNumber(L, g_cl_r);
  return 6;
}

// PSTF_LightSprites() -> creature draws seen, with a depth frame, drawn into the sprite buffer, frames
// with a valid sprite buffer, mean LM luma of the drawn ones, depth files known. Logs.
int __cdecl LuaSprites(engine::LuaState* L) {
  const float lm = g_spr_depth ? g_spr_lm_sum / float(g_spr_depth) : -1.0f;
  Log("lua: PSTF_LightSprites -> seen %d depth %d drawn %d frames %llu lm %.3f (depth files %zu, loaded %zu, "
      "textures %zu, coverage %dx%d)",
      g_spr_seen, g_spr_depth, g_spr_drawn, static_cast<unsigned long long>(g_spr_frames), lm, g_dep_names.size(),
      g_deps.size(), g_dep_tex.size(), g_cov_w, g_cov_h);
  engine::LuaPushNumber(L, g_spr_seen);
  engine::LuaPushNumber(L, g_spr_depth);
  engine::LuaPushNumber(L, g_spr_drawn);
  engine::LuaPushNumber(L, double(g_spr_frames));
  engine::LuaPushNumber(L, lm);
  engine::LuaPushNumber(L, double(g_dep_names.size()));
  return 6;
}

// PSTF_LightSpriteCheck() -> last measured sprite-buffer px, coverage px, both (-1 = none yet); asks for a
// new measurement on the next frame with creatures.
int __cdecl LuaSpriteCheck(engine::LuaState* L) {
  engine::LuaPushNumber(L, double(g_spr_info_px));
  engine::LuaPushNumber(L, double(g_spr_cov_px));
  engine::LuaPushNumber(L, double(g_spr_both_px));
  g_spr_check_req = true;
  return 3;
}

// PSTF_LightShot(path) -> "" or the error: saves the last presented frame (half-size BMP).
int __cdecl LuaShot(engine::LuaState* L) {
  const std::string path = engine::LuaStringArg(L, 1);
  const std::string err = SaveScreenshot(path);
  Log("lua: PSTF_LightShot(%s) -> %s (read fbo was %d)", path.c_str(), err.empty() ? "ok" : err.c_str(),
      g_last_read_fbo);
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
  g_set.emissive = IniBool(ini, "lighting.emissive", true);
  g_set.emissive_keep = pct("lighting.emissive_keep", 80, 0, 100);
  g_set.bloom = IniBool(ini, "lighting.bloom", true);
  g_set.bloom_strength = pct("lighting.bloom_strength", 35, 0, 200);
  g_set.bloom_threshold = pct("lighting.bloom_threshold", 45, 0, 100);
  g_set.fx_lights = IniBool(ini, "lighting.fx_lights", true);
  g_set.fx_soften = IniBool(ini, "lighting.fx_soften", true);
  g_set.untinted_fx = IniBool(ini, "lighting.untinted_fx", true);
  g_set.self_lit = IniBool(ini, "lighting.self_lit", true);
  g_set.sprite_lighting = IniBool(ini, "lighting.sprite_lighting", true);
  g_set.sprite_occlusion = IniBool(ini, "lighting.sprite_occlusion", true);
  g_set.key_shadows = IniBool(ini, "lighting.key_shadows", true);
  g_set.sprite_normal_strength = pct("lighting.sprite_normal_strength", 60, 0, 200);
  g_set.sprite_light_gain = pct("lighting.sprite_light_gain", 100, 0, 300);
  g_set.key_shadow_strength = pct("lighting.key_shadow_strength", 80, 0, 100);
  g_set.key_shadow_length = pct("lighting.key_shadow_length", 85, 20, 200);
  g_set.contact_shadows = IniBool(ini, "lighting.contact_shadows", true);
  g_set.contact_shadow_strength = pct("lighting.contact_shadow_strength", 70, 0, 100);
  g_set.contact_shadow_size = float(std::max(0, std::min(120, IniInt(ini, "lighting.contact_shadow_size", 22))));
  g_set.contact_shadow_flat = pct("lighting.contact_shadow_flat", 50, 20, 100);
  g_set.sprite_shadow_strength = pct("lighting.sprite_shadow_strength", 100, 0, 100);
  g_set.bam_shadows = IniBool(ini, "lighting.bam_shadows", false);
  g_set.day_night = IniBool(ini, "lighting.day_night", true);
  g_set.dn.night_level = pct("lighting.night_level", 45, 5, 100);
  g_set.dn.night_light = pct("lighting.night_light_gain", 150, 100, 400);
  g_set.dn.dusk_warmth = pct("lighting.dusk_warmth", 35, 0, 100);
  g_set.dn.lightmap = pct("lighting.lightmap_night", 60, 0, 200);
  g_set.dn.night_luma = pct("lighting.night_tint_luma", 43, 5, 95);
  g_set.budget = std::max(1, std::min(256, IniInt(ini, "lighting.light_budget", 64)));
  g_set.fx_boost = pct("lighting.fx_boost", 15, 0, 200);
  g_set.fx_light_saturation = pct("lighting.fx_light_saturation", 160, 0, 300);
  g_set.fx_fringe = float(std::max(0, std::min(8, IniInt(ini, "lighting.fx_fringe", 2))));
  g_set.bloom_passes = std::max(1, std::min(3, IniInt(ini, "lighting.bloom_passes", 2)));
  g_set.fx_light_gain = pct("lighting.fx_light_gain", 100, 0, 300);
  g_set.fx_budget = std::max(0, std::min(64, IniInt(ini, "lighting.fx_budget", 24)));
  g_set.atmosphere = IniBool(ini, "lighting.atmosphere", true);
  g_set.hand_lights = IniBool(ini, "lighting.hand_lights", true);
  g_set.hand_light_gain = pct("lighting.hand_light_gain", 150, 0, 300);
  g_set.buff_lights = IniBool(ini, "lighting.buff_lights", true);
  g_set.buff_light_gain = pct("lighting.buff_light_gain", 70, 0, 300);
  g_set.weapon_glow_gain = pct("lighting.weapon_glow_gain", 30, 0, 300);
  g_set.weapon_glow_radius = float(IniInt(ini, "lighting.weapon_glow_radius", 70));
  g_set.atmosphere_strength = pct("lighting.atmosphere_strength", 100, 0, 300);
  g_set.sprite_ao = pct("lighting.sprite_ao", 60, 0, 100);
  g_set.painted_glow = IniBool(ini, "lighting.painted_glow", true);
  g_set.painted_glow_gain = pct("lighting.painted_glow_gain", 100, 0, 300);
  g_set.painted_core = pct("lighting.painted_glow_size", 20, 5, 50);
  g_set.area_lights = IniBool(ini, "lighting.area_lights", true);
  int dbg = IniInt(ini, "lighting.debug", 0);
  if (const char* e = std::getenv("PSTF_LIGHT_DEBUG")) dbg = std::atoi(e);
  if (dbg >= 0 && dbg <= 8) g_mode = static_cast<DebugMode>(dbg);

  if (!engine::InstallWorldComposite(&Composite) || !engine::InstallWorldPreDraw(&PreDraw)) {
    Log("lighting: off, world pass sites do not match");
    return;
  }
  // Phase 4: glow draws need the sprite hooks; without them the rest still runs.
  const bool want_glow = g_set.emissive || g_set.bloom || g_set.fx_lights;
  if (want_glow && !engine::InstallGlowGather()) {
    Log("lighting: glow hooks do not match: no emissive, bloom or effect lights");
    g_set.emissive = g_set.bloom = g_set.fx_lights = false;
  }
  {
    std::vector<uint8_t> buf;
    const std::string path = std::string(DataDir()) + "\\assets\\vfx\\vfxemit.txt";
    if (ReadFile(path, &buf)) g_emit_names = ParseEmitList(std::string(buf.begin(), buf.end()));
    Log("lighting: %zu effect BAMs emit besides the additive draws", g_emit_names.size());
  }
  {
    std::vector<uint8_t> buf;
    const std::string path = std::string(DataDir()) + "\\assets\\vfx\\vfxlight.tsv";
    if (ReadFile(path, &buf)) g_hand = ParseHandLights(std::string(buf.begin(), buf.end()));
    size_t creature = 0;
    for (const HandLight& h : g_hand) creature += h.anim >= 0 ? 1 : 0;
    g_has_creature_lights = creature > 0;
    Log("lighting: %zu hand lights, %zu of them creature lights (vfxlight.tsv)", g_hand.size(), creature);
    const std::string gpath = std::string(DataDir()) + "\\assets\\vfx\\vfxglow.tsv";
    if (ReadFile(gpath, &buf)) g_glow_over = ParseGlowOverrides(std::string(buf.begin(), buf.end()));
    Log("lighting: %zu glow overrides (vfxglow.tsv)", g_glow_over.size());
  }
  {
    std::vector<uint8_t> buf;
    const std::string path = std::string(DataDir()) + "\\assets\\lighting\\arealights.tsv";
    if (ReadFile(path, &buf)) g_area_lights = ParseAreaLights(std::string(buf.begin(), buf.end()));
    Log("lighting: %zu hand area lights (arealights.tsv)", g_area_lights.size());
  }
  engine::SetEmissiveCallbacks(&EmitDecision, &CaptureBegin, &CaptureEnd);
  engine::SetFxMaskCallbacks(&FxMaskBegin, &FxMaskEnd);
  if (!g_set.bam_shadows) {
    g_bam_hidden = engine::HideBamShadows();
    Log(g_bam_hidden ? "lighting: BAM contact shadows hidden (bam_shadows=0)"
                     : "lighting: BAM shadow sites do not match: engine blobs stay");
  }
  if (g_set.sprite_lighting) {
    WIN32_FIND_DATAA fd;
    const std::string pat = std::string(DataDir()) + "\\assets\\sprites\\depth\\*.DEP";
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        std::string n(fd.cFileName);
        const size_t dot = n.find('.');
        if (dot != std::string::npos && dot <= 8) g_dep_names.push_back(PackResRef(n.substr(0, dot).c_str()));
      } while (FindNextFileA(h, &fd));
      FindClose(h);
    }
    std::sort(g_dep_names.begin(), g_dep_names.end());
    engine::SetSpriteCallbacks(&SpriteWant, &CovBegin, &CovEnd);
    Log("lighting: sprite buffer on, %zu sprite depth files", g_dep_names.size());
  }
  if (g_set.fx_lights) {
    std::vector<uint8_t> buf;
    const std::string path = std::string(DataDir()) + "\\assets\\vfx\\vfxauto.tsv";
    if (ReadFile(path, &buf) && ParseVfxTable(std::string(buf.begin(), buf.end()), &g_vfx))
      Log("lighting: vfx table %zu frames", g_vfx.rows.size());
    else
      Log("lighting: no vfx table at %s: glow draws give no light (tools/vfxscan.py run)", path.c_str());
  }
  dev::AddLuaFunction("PSTF_LightDebug", &LuaDebug);
  dev::AddLuaFunction("PSTF_LightState", &LuaState);
  dev::AddLuaFunction("PSTF_LightCheck", &LuaCheck);
  dev::AddLuaFunction("PSTF_LightCheckResult", &LuaCheckResult);
  dev::AddLuaFunction("PSTF_LightShot", &LuaShot);
  dev::AddLuaFunction("PSTF_LightCompare", &LuaCompare);
  dev::AddLuaFunction("PSTF_LightCompareResult", &LuaCompareResult);
  dev::AddLuaFunction("PSTF_LightFx", &LuaFx);
  dev::AddLuaFunction("PSTF_LightFxCells", &LuaFxCells);
  dev::AddLuaFunction("PSTF_LightSprites", &LuaSprites);
  dev::AddLuaFunction("PSTF_LightShadows", &LuaShadows);
  dev::AddLuaFunction("PSTF_LightGlobal", &LuaGlobal);
  dev::AddLuaFunction("PSTF_LightCreatureTrace", &LuaCreatureTrace);
  dev::AddLuaFunction("PSTF_LightSpriteCheck", &LuaSpriteCheck);
  dev::AddLuaFunction("PSTF_LightBloom", &LuaBloom);
  dev::AddLuaFunction("PSTF_LightAtmo", &LuaAtmo);
  dev::AddLuaFunction("PSTF_LightSet", &LuaSet);
  dev::AddLuaFunction("PSTF_LightAo", &LuaAo);
  dev::AddLuaFunction("PSTF_LightSpriteMean", &LuaSpriteMean);
  dev::AddLuaFunction("PSTF_LightTrace", &LuaTrace);
  dev::AddLuaFunction("PSTF_LightCoverage", &LuaCoverage);
  Log("lighting: installed (mode %s, gain %.2f, ambient %.2f, occlusion %d x%d, normals %d)", DebugName(g_mode),
      g_set.light_gain, g_set.ambient, g_set.occlusion ? 1 : 0, g_set.occlusion_steps, g_set.area_normals ? 1 : 0);
}

}  // namespace pstf::lighting
