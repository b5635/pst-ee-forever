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
  // Carried by a creature standing at (x, y): its own sprite does not block it (no self-shadow ring).
  bool owned = false;
  // A burning creature (Ignus): no creature blocks this light at all (it is all around him).
  bool unblocked = false;
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
// --- Phase 4: emissive draws, effect lights, bloom -------------------------------------------------
// CVidCell::RenderTexture (lin 687BA0 / win 1404AEA60) picks the blend from its flags: flags & 0x208 =
// 0 normal (SRC_ALPHA, 1-SRC_ALPHA), 0x8 screen (1-DST, ONE), 0x200 (DST, ONE), 0x208 (SRC, ONE): the three
// additive ones are the "glow" draws; bit 0x20000000 overrides with multiply (DST, 1-SRC_ALPHA).
bool IsGlowBlend(uint32_t flags);

// Emitters beyond the additive blend: effect BAMs named by the exe or effect data (vfxemit.txt), as
// sorted packed names. A draw emits when its blend is additive or its cell's BAM is listed.
uint64_t PackResRef(const char* resref);  // up to 8 chars, upper case, packed little-endian
bool Emits(const std::vector<uint64_t>& sorted_names, const char* resref, uint32_t flags);
// vfxemit.txt: one name per line, '#' comments, CR/LF; returns the sorted packed names.
std::vector<uint64_t> ParseEmitList(const std::string& text);

// One frame of a BAM from tools/vfxscan.py (assets/vfx/vfxauto.tsv): luminance-weighted colour (max
// channel 1), strength = mean luminance over the frame rect, bright centroid in frame pixels, bright px.
struct VfxFrame {
  float r = 1, g = 1, b = 1, strength = 0, cx = 0, cy = 0;
  int bright = 0;
};
uint64_t VfxKey(const char* resref, int cycle, int frame);
struct VfxTable {
  std::vector<std::pair<uint64_t, VfxFrame>> rows;  // sorted by VfxKey
  const VfxFrame* Find(const char* resref, int cycle, int frame) const;
};
// "BAM cycle frame r g b strength cx cy bright" (whitespace separated), '#' comments; bad lines skipped.
bool ParseVfxTable(const std::string& text, VfxTable* out);

// A glow draw of this frame, in area pixels (frame rect top-left x, y, size w, h).
struct GlowDraw {
  float x = 0, y = 0, w = 0, h = 0;
  const VfxFrame* vfx = nullptr;  // null: not in the table (no light, still emissive)
};
// Light of a glow draw (GemRB AddGlowLight): on the frame's bright centroid, ground at the frame bottom,
// radius from the frame size (60..380), colour * clamp(strength * gain, 0.12, 0.9). False = no light.
bool FxLight(const GlowDraw& d, float gain, BakedLight* out);
// Merges lights whose drawn points are closer than `dist` (colours add, capped at 1.5; larger radius)
// and keeps the `budget` strongest.
std::vector<BakedLight> MergeFxLights(std::vector<BakedLight> in, float dist, size_t budget);
// Bloom bright pass soft knee (GemRB PostFS): weight of a pixel of luminance l, threshold t, knee k.
float SoftKnee(float l, float t, float k);
// Emissive mask of a captured effect pixel (colour over black, 0..1): brightness through a soft ramp,
// raised by saturation, so dark edges and grey smoke stay unlit while bright or coloured parts glow.
// Every effect is blitted alone (one cell per FX surface, measured), so the capture holds exactly the
// effect's pixels and this per-pixel weight is the effect's emissive map. Mirrored by emisWeight() in
// the composite shader.
float EmissiveWeight(float r, float g, float b);
// Pushes a light colour away from its grey (k = 1 unchanged, 1.6 = the default for effect lights, so a
// pale pink-white Magic Missile frame lights violet); channels stay >= 0, the luminance is kept.
Rgb Saturate(const Rgb& c, float k);

// --- Phase 2: sprite buffer ------------------------------------------------------------------------
// The sprite info buffer (RGBA16F, one texel per framebuffer pixel; G < 0 = no sprite):
//   R = z, height of the pixel above its creature's feet line (area px)
//   G = LM level * 64 + depth, LM level = round(lm * 15) (the creature's own LM luma, its ambient), depth =
//       height of the surface toward the camera (sprite_depth .DEP, px, capped at 63)
//   B, A = normal x, z (y = toward the camera = sqrt(1 - x^2 - z^2))
float PackSpriteG(float lm, float depth);
void UnpackSpriteG(float g, float* lm, float* depth);
// Light factor of a sprite pixel: the engine already drew it with its LM tint (the LM is its ambient), so
// it gets the same scene-wide darkening as the floor (the area ambient, `ambient` in the ini) and gains
// what the lights add above that ambient relative to its own LM (a torch next to a creature in a painted
// dark nook still lights its lit side). l = light buffer value.
// ao_factor (SpriteAoFactor) darkens only the ambient part: a torch still lights a creature in a corner.
float SpriteLight(float l, float ambient, float lm, float gain, float ao_factor = 1.0f);
// Sprite ambient occlusion (ao.bin GLA1: 255 open .. 0 enclosed; GemRB LIGHTING_PLAN 8.1.2, built for a
// creature standing on the floor): nearest texel at (x, feet y), clamped, 0..1; 1 without a map.
float AoAt(const SceneMap& ao, float x, float y);
// Multiplier of a sprite pixel's ambient: 1 - (1 - ao) * strength (strength 0..1, sprite_ao).
float SpriteAoFactor(float ao, float strength);

// Painted emitters: a baked light from the lights stage stands at its emitter's ground point, z above it
// (GemRB lights 6.1 F / depth 7.2.7: y = ey + h, z = h), so the painted flame is at (x, y - z). Pools have
// no emitter in the art. core = radius * core_scale, 8..48 px. False = no emitter.
bool PaintedEmitter(const BakedLight& l, float core_scale, float* ex, float* ey, float* core);
// Glow weight of a scene pixel (colour 0..1) at `dist` px from a painted emitter (GLSL paintFs): bright
// pixels only (max channel 0.70..0.92 ramp), full inside half the core, 0 at the core's edge.
float PaintedGlowWeight(float r, float g, float b, float dist, float core);

// Hand area lights (assets/lighting/arealights.tsv, shipped from data/arealights.tsv): baked lights placed
// by hand where the GemRB pipeline found none. Line: WED x y z radius r g b strength kind shadow, the
// lights.txt light fields after the area's WED ('#' comments; bad lines skipped).
struct AreaLight {
  char wed[9] = {};
  BakedLight light;
};
std::vector<AreaLight> ParseAreaLights(const std::string& text);
// Appends the rows of `wed` (case-insensitive) to `out`; returns how many.
size_t AddAreaLights(const std::vector<AreaLight>& table, const std::string& wed, std::vector<BakedLight>* out);
// Key-light shadow (projected silhouette, GemRB ScenePolish::DrawShadows `project`): the key line's dir
// is where shadows fall; a sprite pixel h px above the feet lands on the floor at (x + h*sx, feet + h*sy)
// with sx = k*dirX, sy = k*dirY*0.72 - 0.15*(1 - |dirY|) (sideways casts keep a little depth), |sy| >= 0.22
// so the inverse stays defined and low key lights still give a readable silhouette. KeyShadowSource inverts it for a floor point: the source pixel and h;
// false when no pixel of a sprite up to max_h lands there.
void KeyShadowAxes(float dir_x, float dir_y, float k, float* sx, float* sy);
bool KeyShadowSource(float mx, float my, float feet, float sx, float sy, float max_h, float* src_x, float* src_y,
                     float* h);
// Contact shadow (our replacement for the BAM blob, shadow shader mode 1): darkening at (dx, dy) area px
// from a creature's feet, ellipse radii rx, ry: k at the centre, flat to r = 0.3, smooth to 0 at the rim.
float ContactShadow(float dx, float dy, float rx, float ry, float k);

// Hand glow overrides (assets/vfx/vfxglow.tsv, shipped from data/vfxglow.tsv): BAMs matching `pattern`
// ('?' = any char) glow or not, and are softened (screen blend) or not, whatever the scan says.
// Line: pattern glow soften   (0/1 each; '#' comments)
struct GlowOverride {
  char pattern[9] = {};
  bool glow = true, soften = true;
};
std::vector<GlowOverride> ParseGlowOverrides(const std::string& text);
const GlowOverride* FindGlowOverride(const std::vector<GlowOverride>& table, const char* resref);

// Hand light table (assets/vfx/vfxlight.tsv, shipped from data/vfxlight.tsv): any world draw whose BAM
// matches `pattern` ('?' = any one char, upper case, <= 8) carries a light: Ignus is always on fire.
// Line: pattern r g b radius z kind [strength]   (kind fire|magic|steady|pool; '#' comments)
// pattern "anim:N" = a creature light instead: every creature of animation id N (RESDATA.INI section)
// carries it at its own drawn feet, z px up: fixed to the creature, not to the drawn frame.
// Creature light flags after the strength: "noshadow" (the creature is the light: it casts no contact or
// key shadow and no creature blocks its light), "glow=PATTERN" (its BAMs keep their brightness: emissive).
struct HandLight {
  char pattern[9] = {};
  char glow[9] = {};    // creature lights: BAM pattern drawn emissive ("" = none)
  bool noshadow = false;
  int anim = -1;     // >= 0: creature light for this animation id (pattern unused)
  BakedLight light;  // x/y unused; z = height above the frame bottom / feet, radius, colour, kind, strength
};
std::vector<HandLight> ParseHandLights(const std::string& text);
bool MatchPattern(const char* pattern, const char* resref);
const HandLight* FindHandLight(const std::vector<HandLight>& table, const char* resref);
// The light for a matched draw (frame rect in area px): centred, ground at the frame bottom.
BakedLight HandLightAt(const HandLight& h, const GlowDraw& d, size_t index, double t_ms, float gain);
// Creature light for animation id `anim`: exact id first, then its low byte (EE ids past 255); null = none.
const HandLight* FindCreatureLight(const std::vector<HandLight>& table, int anim);
// The light of a creature standing at (x, y) area px: owned, flicker phase from the animation id (stable
// from frame to frame, whatever else is drawn).
BakedLight CreatureLightAt(const HandLight& h, float x, float y, double t_ms, float gain);
// Sprite occlusion of an owned light (GLSL occlusion(), u_owner): a sprite pixel whose feet line is within
// 8 px of the owner's feet and whose ray sample is within 40 px of the owner's x is the owner itself, and
// does not block the light it carries.
inline bool OwnerSprite(float owner_x, float owner_feet, float sample_x, float sprite_feet) {
  return (sprite_feet - owner_feet < 8.0f && owner_feet - sprite_feet < 8.0f) &&
         (sample_x - owner_x < 40.0f && owner_x - sample_x < 40.0f);
}

// Colour glow effects on creatures (opcodes 8 solid, 9 pulse, 52 bright tint, 61 fade; param1 =
// 0xBBGGRR00; 353 tint screen, param1 = 0x00BBGGRR, the buffs' fade in/out): a light of that colour on
// the creature. age_ms = time since the effect was first seen.
// Returns false when the light has faded out.
// Size of a buff light. Effects held by equipped items (timing 2: the glow pulse on 39 magic weapons,
// e.g. Annah's punch daggers) are the weapon glowing, not the body: one common weapon size, small and
// weak, at hand height. Everything else lights around the whole creature.
struct BuffLightShape {
  float radius, z, gain;
};
inline BuffLightShape BuffLightFor(uint32_t timing, float weapon_radius, float weapon_gain) {
  if (timing == 2) return {weapon_radius, 30.0f, weapon_gain};
  return {170.0f, 45.0f, 1.0f};
}
bool ColorEffectLight(int opcode, uint32_t param1, uint32_t param2, double age_ms, double t_ms, float gain,
                      Rgb* out);

// --- Day / night -------------------------------------------------------------------------------------
// The engine darkens a day/night area with one global tint (CInfinity +0x2EC, time of day: white by day,
// SetDusk/SetDawn ramp it, the night colour at night). The composite divides that tint back out of the
// scene (albedo) and lights it with our own time-of-day ambient, so lights act on the real colours.
struct DayNight {
  float night_level = 0.45f;   // night ambient / day ambient
  float night_light = 1.5f;    // dynamic lights at full night (x)
  float dusk_warmth = 0.35f;   // warm tint at dusk and dawn
  float lightmap = 0.6f;       // lightmap light pools at night
  float night_luma = 0.43f;    // luma of the engine's full-night tint (n = 1 there)
};
// 0 = day (tint white) .. 1 = full night, from the engine's time-of-day tint (0x00BBGGRR).
float Nightness(uint32_t tod_tint, float night_luma);
// The ambient at nightness n: day ambient -> moonlight (cool, night_level), warm between (dusk/dawn).
Rgb TimeOfDayAmbient(const Rgb& day, float n, const DayNight& d);
// Light gain at nightness n: 1 by day, night_light at full night.
float NightLightGain(float n, const DayNight& d);
// Scene pixel with the engine tint taken out (composite): c / max(tint, 0.2), capped at 1.
Rgb UndoTint(const Rgb& c, const Rgb& tint);
// Where a spell effect is drawn (its capture fx): EffectCover is 0 below 0.01, 1 from 0.12 of the capture's
// brightest channel (the composite keeps those pixels unlit).
float EffectCover(const Rgb& fx);
// The unlit cover of a pixel: EffectCover of the capture, or its alpha, written only by a self-lit creature
// (Ignus, hand light with glow=): all of him keeps his own (tint-free) colour at night, the dark parts too.
inline float FxCover(const Rgb& fx, float self_lit_alpha) {
  const float e = EffectCover(fx);
  return e > self_lit_alpha ? e : self_lit_alpha;
}
// fx_soften (normal-alpha effect draws in the screen blend) only by day: nightness below 0.5.
inline bool SoftenAt(float nightness) { return nightness < 0.5f; }
// The engine tints a cell's palette with the global (time-of-day) tint only when its draw flags have 0x10000
// (CVidPalette::GetTint lin 690700: 0x10000 global tint, 0x20000 the cell's own tint; creatures 0x30000).
// Most spell BAMs (A010SPIN, A009NECR, S083*: flags 0x40/0x41) are drawn untinted: dividing the night tint
// (0xC86464 = r g 0.39, b 0.78) out of those pixels raised red and green 2.5x: a white Spiritual Hammer went
// yellow, dark layers became visible copies. An FX-surface blit is untinted when every cell in it is.
inline bool UntintedCell(uint32_t cell_flags) { return (cell_flags & 0x10000u) == 0; }
// How an untinted draw marks the effect mask: 1 = colour and alpha (normal blend; FX blits are premultiplied,
// glow pixels have alpha 0), 2 = colour only (additive blend: black adds nothing, so only the light counts),
// 0 = not at all (multiply: a darkening layer).
// A softened draw (fx_soften: normal blend turned into the screen blend) keeps kind 1: its dark parts are part
// of the effect too (Elysium's Fires' ring has dark spokes; marked by colour only they were relit, darker).
inline int FxMaskKind(uint32_t blit_flags, bool softened) {
  (void)softened;
  if (blit_flags & 0x20000000u) return 0;
  return (blit_flags & 0x208u) ? 2 : 1;
}
// Untinted cover of a pixel from the effect mask (the colour untinted draws added, their alpha; sqrt: with a
// SRC_ALPHA blend one layer writes a^2): the share of the pixel the engine drew without its tint.
float UntintedCover(const Rgb& add, float alpha_sq);
// The composite's albedo: the engine tint taken out except by the untinted cover k (kept as drawn).
Rgb UndoTintFx(const Rgb& c, const Rgb& tint, float k);
Rgb TintRgb(uint32_t bbggrr);
// Night light of the area lightmap (LM, painted light pools): what a LM texel holds above the area's
// mean LM luma, scaled by k (0 where it is at or below the mean).
Rgb LightmapPool(const Rgb& lm, float mean_luma, float k);

// --- Phase 5: atmosphere v1 ------------------------------------------------------------------------
// Vertical depth cue: surfaces high above the floor drift toward the haze colour (VISUALS_PLAN §5.4).
struct Atmosphere {
  float haze = 0.06f, desat = 0.08f, contrast = 0.05f, h0 = 200.0f, h1 = 1024.0f;
};
float AtmosphereT(const Atmosphere& a, float h);  // smoothstep(h0, h1, h)
Rgb ApplyAtmosphere(const Atmosphere& a, const Rgb& c, float h, const Rgb& haze_colour, float strength);
// Haze colour: the key-light ambient hue at the ambient's luma; without a key line the ambient itself.
Rgb HazeColour(const AreaScene& s, const Rgb& ambient);

// Debug modes (PSTF_LightDebug): what the composite does.
enum class DebugMode : int {
  Lit = 0,        // normal
  LightOnly = 1,  // the light buffer as colour
  RoundTrip = 2,  // copy the scene and draw it back unchanged (pixel-identical: phase-0 test)
  Falloff = 3,    // GemRB debug: red = vertical surface, green = falloff, blue = normal x
  Off = 4,        // no pass at all (the hooks just call through)
  Emissive = 5,   // the emissive mask (red) and the bloom (green) of the glow draws
  Sprites = 6,    // creatures violet, coverage-only (engine shadow blobs) green, buffer-only red, key shadows yellow
  EmisRaw = 7,    // the emissive capture as drawn (colour), over the scene dimmed to a quarter
  Untinted = 8,   // the untinted effect cover (red) and the unlit effect cover (green) over the dimmed scene
};
const char* DebugName(DebugMode m);

}  // namespace pstf::lighting
