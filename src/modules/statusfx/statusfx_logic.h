// SPDX-License-Identifier: GPL-2.0-or-later
// Status-effect visuals instead of the engine's colour pulses: fear (floating burning skulls), stun (stars
// circling the head, a lightning burst), blindness (growing glints), poison (acid sparks spiralling up into a
// green burst), silence (the same in white) and a spell mantle when magic resistance stops a spell. Every
// effect is a small particle system whose timings, rates, speeds, sizes and colours follow the matching
// Neverwinter Nights effect models (numbers only; the art is ours, tools/make_statusfx_art.py). The other
// colour glows of timed spells (confusion, taunt, ...) run for a moment and stop.
// Presentation only: states, effects, saves and scripts stay the engine's. Pure logic (no engine).
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pstf::statusfx {

// Atlas cells (tools/make_statusfx_art.py, data/statusfx_atlas.bin).
enum Cell : uint8_t {
  kGlint = 0,   // 4 frames
  kDust = 4,
  kStreak = 5,
  kBurst = 6,
  kShard = 7,   // spell mantle pane
  kBolt = 8,    // 4 variants
  kRing = 12,   // 4 frames
  kSmoke = 16,  // 8 frames
  kFire = 24,   // 8 frames, own colour
  kSkull = 32,  // 2 variants, own colour, normal blend
  kGlow = 34,
  kWisp = 35    // 4 frames
};
constexpr int kAtlasCell = 128, kAtlasCols = 8, kAtlasRows = 5;

enum class Fx : uint8_t { Fear, Stun, StunLoop, Blind, Acid, Sonic, Mantle, Count };
const char* FxName(Fx f);
bool FxFromName(const std::string& name, Fx* out);  // any case
bool IsLoop(Fx f);       // runs until stopped (Fear, StunLoop)
float FxLength(Fx f);    // s an impact emits (loops: one cycle)

// STATE.IDS bits drawn by this module.
constexpr uint32_t kStatePanic = 0x4, kStateStunned = 0x8, kStateDead = 0x800, kStateSilenced = 0x1000,
                   kStatePoisoned = 0x4000, kStateBlind = 0x40000;

struct Rng {
  uint32_t s = 0x2545F491u;
  float Next();  // [0, 1)
  float Range(float lo, float hi) { return lo + (hi - lo) * Next(); }
};

// Metres from the owner's feet: x right, y away from the viewer, z up (NWN model space).
struct Particle {
  float x = 0, y = 0, z = 0;
  float vx = 0, vy = 0, vz = 0;
  float age = 0, life = 1;
  float size0 = 0.1f, size1 = 0.1f;  // metres
  float a0 = 1, a1 = 1;              // alpha from start to end
  float fade_in = 0;                 // s
  float hold = 0;                    // 0..1 of the life the alpha stays a0 before it moves to a1
  float r0 = 1, g0 = 1, b0 = 1, r1 = 1, g1 = 1, b1 = 1;
  float rot = 0, vrot = 0;           // radians, rad/s
  float spin = 0;                    // rad/s the position turns about the owner's vertical axis (inherit_local)
  float drag = 0;                    // 1/s
  uint8_t cell = 0, frames = 1;
  float fps = 0;                     // 0: frame0 for life
  uint8_t frame0 = 0;
  uint8_t mode = 0;                  // 0 sprite, 1 streak along the velocity, 2 floor ring, 3 mantle pane
  bool additive = true;
  float theta = 0, rad0 = 0, rad1 = 0, close_t = 0.3f;  // mode 3: angle about the owner, radius rad0 -> rad1
  float trail = 0, trail_acc = 0;    // fire puffs per second left behind (burning skulls)
  float trail_until = 0;             // s of its life it trails fire
  // Point-to-point (NWN p2p, gravity type): pulled at `grav` m/s^2 toward a target that turns about the
  // owner's vertical axis: target = rot_z(tphase + tspin * age) * (tx, ty) + (0, 0, tz).
  float grav = 0, tx = 0, ty = 0, tz = 0, tphase = 0, tspin = 0;
  int32_t owner = -1;
  float delay = 0;                   // s before it shows (mantle panes in turn)
};

struct Instance {
  Fx fx = Fx::Fear;
  int32_t owner = -1;
  float t = 0;          // s since start
  bool stopping = false;
  float acc[6] = {};    // emitter accumulators
  bool fired = false;   // one-shot parts done (detonations)
};

// Where a creature is drawn: feet (area px) and the engine's overhead text height (px above the feet).
struct Anchor {
  float x = 0, y = 0;
  float head_px = 70;
};

// One textured quad, area px (centre), rotation in radians (screen, clockwise), cell = atlas index.
struct Quad {
  float x, y, w, h, rot;
  uint8_t cell;
  float r, g, b, a;
  bool additive;
};

// The IE 3/4 view, ~35 degrees down: a height z rises z * cos, depth y rises y * sin.
constexpr float kCosE = 0.819f, kSinE = 0.574f;
constexpr float kSmallBoost = 1.8f;  // small particles' size over NWN's (System::Draw)
constexpr float kHeadM = 1.75f;  // the NWN head attach point of a human, metres above the feet
// Px per metre for a creature whose overhead text sits head_px above its feet (the top of its frame ~ 1.9 m).
float PxPerMetre(float head_px);

class System {
 public:
  float scale = 1.0f;  // ini size / 100
  size_t max_particles = 4000;
  Rng rng;
  std::vector<Instance> inst;
  std::vector<Particle> p;

  void Start(Fx fx, int32_t owner);
  void Stop(Fx fx, int32_t owner);           // a loop stops emitting; its particles finish
  bool Active(Fx fx, int32_t owner) const;   // emitting (not stopping)
  int Count(int32_t owner) const;            // particles of that owner
  void Update(float dt);
  void Clear() {
    inst.clear();
    p.clear();
  }
  void Forget(int32_t owner);  // creature gone or left the area
  // Quads of one owner: the additive ones first, then the normal-blend ones (skulls over their fire).
  void Draw(int32_t owner, const Anchor& a, std::vector<Quad>* out) const;

 private:
  Particle& Add(int32_t owner);
  void Emit(Instance& in, float t0, float t1);
};

// --- which state draws what ----------------------------------------------------------------------------
struct Toggles {
  bool fear = true, stun = true, blind = true, poison = true, silence = true;
};
// Starts and stops effects from each creature's STATE bits: a new state plays its impact (and its loop,
// held while the state lasts); one without a loop plays its impact again every `repeat` s (0 = once).
class Watcher {
 public:
  Toggles on;
  float repeat = 6.0f;
  void Step(int32_t id, uint32_t state, double now, System* sys);
  void Forget(int32_t id) { s_.erase(id); }
  void Clear() { s_.clear(); }
  uint32_t Bits(int32_t id) const;

 private:
  struct S {
    uint32_t bits = 0;
    double last[5] = {};
  };
  std::map<int32_t, S> s_;
};

// --- the engine's colour glows ---------------------------------------------------------------------------
struct EffectInfo {
  uint64_t key = 0;
  int opcode = -1;
  uint32_t type = 0;  // 4096 running timed, 6/7/8 delayed
  bool equipped = false;
  std::string parent, res;  // upper case
};
struct CutPlan {
  std::vector<uint64_t> expire;  // colour glows to end now
  std::vector<uint64_t> blank;   // pending op 369 overlays to play the blank BAM instead
};
inline bool IsGlowOpcode(int op) { return op == 8 || op == 9 || op == 51 || op == 52 || op == 61; }
// State opcodes whose glows (and overlays) this module replaces with its own visual.
bool ReplacedStateOpcode(int op, const Toggles& on);
constexpr const char* kBlankBam = "PFVBLANK";

// Timed colour glows (spells, potions, weapon hits; not an equipped item's own glow) end `cut_after` s after
// they started; those of a spell that also gives a state drawn here end at once. A pending sticky overlay
// (op 369) of a blinding spell plays nothing (our blind visual replaces it).
class GlowCutter {
 public:
  float cut_after = 2.0f;  // 0 = leave other glows alone
  Toggles on;
  CutPlan Plan(int32_t id, const std::vector<EffectInfo>& effs, double now);
  void Forget(int32_t id);
  size_t Tracked() const { return first_.size(); }

 private:
  std::map<std::pair<int32_t, uint64_t>, double> first_;
};

}  // namespace pstf::statusfx
