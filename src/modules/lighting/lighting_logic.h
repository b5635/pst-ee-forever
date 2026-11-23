// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Contributors to the GemRB project <https://gemrb.org> (ParseLights, ReadMap,
// HeightAt, the light model: ported from gemrb/core/AreaSceneData.cpp, Map.cpp and GLShaders.h LightFS)
//
// Lighting module, pure part: the baked scene data per area (lights.txt, height.bin GLH1, normal.bin
// GLN1, ao.bin GLA1, all in area pixels) and CPU mirrors of the GLSL light model, so every number the
// shaders use is unit-tested. No Windows, no GL, no engine.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pstf::lighting {

enum class Kind : uint8_t { Fire, Magic, Steady, Pool };

struct BakedLight {
  float x = 0, y = 0, z = 0, radius = 0, r = 1, g = 1, b = 1, strength = 1;
  Kind kind = Kind::Steady;
  bool casts_shadow = false;
};

struct SceneMap {
  int w = 0, h = 0, texel = 2;
  float unit = 1.0f;
  std::vector<uint8_t> data;  // u16 LE for height, rgb8 for normals, u8 for ao
  bool empty() const { return data.empty(); }
};

struct AreaScene {
  std::vector<BakedLight> lights;
  bool has_key = false;
  float key_dir[2] = {0, 1};
  float key_strength = 0;
  float key_ambient[3] = {1, 1, 1};
  int size_w = 0, size_h = 0;  // "size" line (area pixels), 0 if absent
  SceneMap height, normal, ao;
};

// lights.txt v1. False (out untouched) without a "version 1" line; malformed light lines are skipped.
bool ParseLights(const std::string& text, AreaScene& out);
// 32-byte header: magic, u32 version (1), u32 w, u32 h, u32 texel, f32 unit, 8 spare; then w*h*bpt.
bool ReadMap(const uint8_t* bytes, size_t n, const char magic[4], int bytes_per_texel, SceneMap& out);
// Nearest texel, clamped (the GLSL heightAt samples the uploaded float texture the same way).
float HeightAt(const SceneMap& map, float area_x, float area_y);
// Height texture contents: one float per texel, value * unit (area pixels).
std::vector<float> HeightFloats(const SceneMap& map);

// Per-frame light model (GemRB Map::CollectSceneLights).
// ARE area type flags (ARE +0x48): 0x01 outdoor, 0x20 dungeon.
struct Rgb {
  float r = 0, g = 0, b = 0;
};
Rgb Ambient(uint16_t area_type, float strength /*0..2, 1 = default*/, const AreaScene* scene, bool use_key);
float Luma(const Rgb& c);
// Flicker factor of light `index` of `kind` at presentation time t_ms.
float Flicker(Kind kind, size_t index, double t_ms);
// Colour scale for a baked light: strength * gain * flicker * ((1 - ambient luma) + 0.1).
float LightScale(const BakedLight& l, float gain, float flicker, float ambient_luma);

// Falloff of light (lx, ly ground, lz height, radius) at surface point P = (x, ground, h): the GLSL
// `att` before occlusion (0 outside the radius). The y axis is squashed by 0.72 (isometric floor).
float Falloff(float px, float pground, float ph, float lx, float ly, float lz, float radius);
// Height-map occlusion between surface point P and the light (GLSL occlusion()); `jitter` in [0,1)
// (the shader uses a per-pixel hash; tests pass a fixed value). Returns 1 = lit, 0 = blocked.
float Occlusion(const SceneMap& height, float px, float pground, float ph, float lx, float ly, float lz,
                int steps, float bias, float thickness, float jitter);
// Wrapped Lambert (GLSL lambert()) and the normal ratio against a flat floor normal.
float Lambert(const float n[3], const float l[3]);
float NormalRatio(const float n[3], const float l[3]);
// Composite of scene colour c with light l (GLSL composite: soft shoulder to max_light, highlight keep).
Rgb Composite(const Rgb& c, const Rgb& l, float max_light, float keep);

// Framebuffer <-> area mapping inside the world pass. The engine's vertex shader does
// clip = draw * uST.xy + uST.zw; draw = area - off, off = view - rect.left (CGameSprite::RenderActions).
// phys = framebuffer pixel relative to the viewport origin, y up (GL window coords).
struct ViewMap {
  float ax = 1, ay = 1, bx = 0, by = 0;  // area = phys * a + b
};
ViewMap MakeViewMap(int vp_w, int vp_h, const float st[4], float off_x, float off_y);
void ToArea(const ViewMap& m, float phys_x, float phys_y, float* ax, float* ay);
void ToPhys(const ViewMap& m, float area_x, float area_y, float* px, float* py);
// Light footprint in area pixels (ground ellipse plus up to one radius above it: walls).
struct Rect {
  float x0, y0, x1, y1;
};
Rect LightFootprint(const BakedLight& l);
// Footprint in light-buffer pixels (phys / to_phys), y up; false when it is outside [0,w]x[0,h].
bool FootprintInBuffer(const ViewMap& m, const BakedLight& l, float to_phys, int buf_w, int buf_h, Rect* out);

// The shader's per-pixel jitter: fract(52.9829189 * fract(dot(frag, (0.06711056, 0.00583715)))) at the
// pixel centre (gl_FragCoord = x + 0.5, y + 0.5).
float Jitter(float frag_x, float frag_y);
// What the light shader writes in falloff debug mode for light-buffer pixel (bx, by): red = 0.5 on a
// vertical surface, green = 0.5 * falloff * occlusion. False when the pixel is outside the light.
struct FalloffPixel {
  float red, green;
};
bool DebugFalloffAt(const ViewMap& m, float to_phys, int bx, int by, const BakedLight& l, const SceneMap* height,
                    int steps, FalloffPixel* out);
// Debug modes (PSTF_LightDebug): what the composite does.
enum class DebugMode : int {
  Lit = 0,        // normal
  LightOnly = 1,  // the light buffer as colour
  RoundTrip = 2,  // copy the scene and draw it back unchanged (pixel-identical: phase-0 test)
  Falloff = 3,    // GemRB debug: red = vertical surface, green = falloff, blue = normal x
  Off = 4,        // no pass at all (the hooks just call through)
};
const char* DebugName(DebugMode m);

}  // namespace pstf::lighting
