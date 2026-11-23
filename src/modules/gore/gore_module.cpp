// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/gore/gore_module.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/log.h"
#include "engine/blood.h"
#include "engine/gl_world.h"
#include "engine/lua.h"
#include "engine/render.h"
#include "engine/sprites.h"
#include "modules/dev/lua_bridge.h"
#include "modules/gore/gore_logic.h"
#include "modules/lighting/lighting_logic.h"

namespace pstf::gore {
namespace {

constexpr unsigned kQuads = 0x0007, kBlend = 0x0BE2, kVertexShader = 0x8B31, kFragmentShader = 0x8B30,
                   kCompileStatus = 0x8B81, kLinkStatus = 0x8B82, kSrcAlpha = 0x0302, kOneMinusSrcAlpha = 0x0303,
                   kOne = 1, kFuncAdd = 0x8006, kScissor = 0x0C11, kDepth = 0x0B71, kCull = 0x0B44,
                   kStencil = 0x0B90, kAlphaTest = 0x0BC0;

struct Settings {
  bool custom_blood = true, crits = true;
  float particle_scale = 1.5f;
} g_set;

MaterialTable g_table;
Particles g_ps;
engine::Gl g_gl = {};
bool g_gl_loaded = false, g_gl_off = false;
unsigned g_prog = 0;
int g_u_target = -1, g_u_disc = -1;
uint64_t g_area = 0;
ULONGLONG g_last_tick = 0;
// probes
uint64_t g_events = 0, g_swallowed = 0, g_hits = 0, g_crits = 0, g_draws = 0;
std::string g_last_material = "-";

const char* kVs = R"(#version 130
uniform vec2 u_target;
out vec2 v_uv;
out vec4 v_col;
void main() {
  gl_Position = vec4(gl_Vertex.xy / u_target * 2.0 - 1.0, 0.0, 1.0);
  v_uv = gl_MultiTexCoord0.xy;
  v_col = gl_Color;
}
)";
// Soft shapes (GemRB SoftShape): blob = (1 - d^2)^2, disc = hard edge with a 30 % soft rim.
const char* kFs = R"(#version 130
in vec2 v_uv;
in vec4 v_col;
uniform int u_disc;
void main() {
  float d2 = dot(v_uv, v_uv);
  if (d2 >= 1.0) discard;
  float a = u_disc == 1 ? 1.0 - smoothstep(0.7, 1.0, sqrt(d2)) : (1.0 - d2) * (1.0 - d2);
  gl_FragColor = vec4(v_col.rgb, v_col.a * a);
}
)";

unsigned Compile(unsigned type, const char* src) {
  const unsigned s = g_gl.CreateShader(type);
  g_gl.ShaderSource(s, 1, &src, nullptr);
  g_gl.CompileShader(s);
  int ok = 0;
  g_gl.GetShaderiv(s, kCompileStatus, &ok);
  if (!ok) {
    char buf[1024] = {};
    g_gl.GetShaderInfoLog(s, sizeof buf - 1, nullptr, buf);
    Log("gore: shader failed: %s", buf);
    return 0;
  }
  return s;
}

bool SetUpGl() {
  if (g_prog) return true;
  if (g_gl_off) return false;
  g_gl_off = true;
  if (!g_gl_loaded) {
    const std::string err = engine::LoadGl(&g_gl);
    if (!err.empty()) {
      Log("gore: drawing off, GL %s", err.c_str());
      return false;
    }
    g_gl_loaded = true;
  }
  if (!engine::EngineProgram(0)) {
    Log("gore: drawing off, the engine runs without shaders");
    return false;
  }
  const unsigned v = Compile(kVertexShader, kVs), f = Compile(kFragmentShader, kFs);
  if (!v || !f) return false;
  const unsigned p = g_gl.CreateProgram();
  g_gl.AttachShader(p, v);
  g_gl.AttachShader(p, f);
  g_gl.LinkProgram(p);
  g_gl.DeleteShader(v);
  g_gl.DeleteShader(f);
  int ok = 0;
  g_gl.GetProgramiv(p, kLinkStatus, &ok);
  if (!ok) {
    Log("gore: program link failed");
    return false;
  }
  g_prog = p;
  g_u_target = g_gl.GetUniformLocation(p, "u_target");
  g_u_disc = g_gl.GetUniformLocation(p, "u_disc");
  g_gl_off = false;
  Log("gore: GL ready, program %u", p);
  return true;
}

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

float Height(Material m) {  // where on the body the hit lands (px above the feet); Morte floats low
  return m == Material::Bone ? 38.0f : 45.0f;
}

void Spawn(int x, int y, int anim, int direction, int severity, bool crit) {
  HitEvent e;
  e.x = float(x);
  e.y = float(y);
  e.mat = g_table.Of(anim < 0 ? 0 : anim);
  e.height = Height(e.mat);
  e.severity = severity;
  e.crit = crit;
  e.dir = DirectionAngle(direction);
  g_ps.Hit(e);
  ++g_hits;
  if (crit) ++g_crits;
  g_last_material = MaterialName(e.mat);
}

// engine/blood.h: every AddBlood. Gushes and the exploding death become our material effect (the engine's
// BLOOD* animation is swallowed); critical-hit animations stay and get an exaggerated spray on top.
bool OnBlood(const engine::BloodEvent& e) {
  ++g_events;
  const int sev = GushSeverity(e.type);
  if (sev >= 0 && g_set.custom_blood) {
    Spawn(e.x, e.y, e.anim_id, e.direction, sev, false);
    ++g_swallowed;
    static int logged = 0;
    if (logged++ < 20)
      Log("gore: hit %X anim %d (%s) type %d dir %d at %d,%d", static_cast<unsigned>(e.sprite_id), e.anim_id,
          g_last_material.c_str(), e.type, e.direction, e.x, e.y);
    return true;
  }
  if (IsCriticalAnim(e.type) && g_set.crits) Spawn(e.x, e.y, e.anim_id, e.direction, 3, true);
  return false;
}

// --- drawing ------------------------------------------------------------------------------------------
enum class Pass { Floor, Air, Glow };

void Draw(Pass pass) {
  if (g_ps.p.empty() && (pass != Pass::Glow || g_ps.flashes.empty())) return;
  const uint64_t area = engine::WorldPassArea();
  if (!area) return;
  engine::EngineFlushBatch();
  if (!SetUpGl()) return;
  auto& gl = g_gl;
  engine::GlState saved;
  engine::SaveGlState(gl, &saved);
  const int vw = saved.viewport[2], vh = saved.viewport[3];
  float st[4], ox, oy;
  engine::WorldMatrix(st);
  engine::WorldPassOffset(&ox, &oy);
  if (vw <= 0 || vh <= 0 || st[0] == 0 || st[1] == 0) {
    engine::RestoreGlState(gl, saved);
    return;
  }
  const lighting::ViewMap vm = lighting::MakeViewMap(vw, vh, st, ox, oy);
  const float px_per_area = 1.0f / std::fabs(vm.ax);
  for (unsigned cap : {kScissor, kDepth, kCull, kStencil, kAlphaTest}) gl.Disable(cap);
  gl.ColorMask(1, 1, 1, 0);
  gl.UseProgram(g_prog);
  gl.Uniform2f(g_u_target, float(vw), float(vh));
  gl.Enable(kBlend);
  gl.BlendEquationSeparate(kFuncAdd, kFuncAdd);
  const bool glow = pass == Pass::Glow;
  if (glow) gl.BlendFuncSeparate(kSrcAlpha, kOne, kSrcAlpha, kOne);
  else gl.BlendFuncSeparate(kSrcAlpha, kOneMinusSrcAlpha, kSrcAlpha, kOneMinusSrcAlpha);
  const float k = g_set.particle_scale;
  for (int disc = 0; disc <= 1; ++disc) {
    gl.Uniform1i(g_u_disc, disc);
    gl.Begin(kQuads);
    for (const Particle& q : g_ps.p) {
      const bool decal = IsDecal(q.kind);
      if ((pass == Pass::Floor) != decal) continue;
      const Shape s = ShapeOf(q);
      if (s.disc != (disc == 1) || s.additive != glow || s.a <= 0.003f) continue;
      float cx, cy;
      lighting::ToPhys(vm, s.x, s.y, &cx, &cy);
      const float rx = s.rx * k * px_per_area, ry = s.ry * k * px_per_area;
      if (cx + rx < 0 || cy + ry < 0 || cx - rx > float(vw) || cy - ry > float(vh)) continue;
      gl.Color4f(s.r, s.g, s.b, std::min(1.0f, s.a));
      gl.TexCoord2f(-1, -1);
      gl.Vertex2f(cx - rx, cy - ry);
      gl.TexCoord2f(1, -1);
      gl.Vertex2f(cx + rx, cy - ry);
      gl.TexCoord2f(1, 1);
      gl.Vertex2f(cx + rx, cy + ry);
      gl.TexCoord2f(-1, 1);
      gl.Vertex2f(cx - rx, cy + ry);
      ++g_draws;
    }
    // spark impact flashes: a short additive glow at the point of impact
    if (glow && disc == 0)
      for (const Flash& f : g_ps.flashes) {
        const float t = f.age / f.life, a = (1.0f - t) * (1.0f - t) * 0.6f;
        float cx, cy;
        lighting::ToPhys(vm, f.x, f.y - f.z, &cx, &cy);
        const float r = f.radius * 0.45f * px_per_area;
        gl.Color4f(f.r, f.g, f.b, a);
        gl.TexCoord2f(-1, -1);
        gl.Vertex2f(cx - r, cy - r * 0.7f);
        gl.TexCoord2f(1, -1);
        gl.Vertex2f(cx + r, cy - r * 0.7f);
        gl.TexCoord2f(1, 1);
        gl.Vertex2f(cx + r, cy + r * 0.7f);
        gl.TexCoord2f(-1, 1);
        gl.Vertex2f(cx - r, cy + r * 0.7f);
      }
    gl.End();
  }
  gl.Color4f(1, 1, 1, 1);
  engine::RestoreGlState(gl, saved);
}

void DrawFloor() { Draw(Pass::Floor); }
void DrawAir() { Draw(Pass::Air); }
void DrawGlow() { Draw(Pass::Glow); }

// Once per drawn frame, before the world pass: the presentation clock (capped at 100 ms per frame).
void PreDraw() {
  engine::ArmWorldComposite();
  const uint64_t area = engine::WorldPassArea();
  const ULONGLONG now = GetTickCount64();
  float dt = g_last_tick ? float(std::min<ULONGLONG>(now - g_last_tick, 100)) / 1000.0f : 0.0f;
  g_last_tick = now;
  if (area != g_area) {  // another area: nothing carries over
    g_ps.Clear();
    g_area = area;
    dt = 0;
  }
  g_ps.Simulate(dt);
}

// --- Lua probes --------------------------------------------------------------------------------------
// PSTF_GoreState() -> particles, decals, airborne, engine blood events, swallowed, hits drawn, crits,
// last material name.
int __cdecl LuaState(engine::LuaState* L) {
  int decals = 0, air = 0;
  for (const Particle& q : g_ps.p) (IsDecal(q.kind) ? decals : air)++;
  static uint64_t last_events = ~0ull, last_hits = ~0ull, calls = 0;
  if (g_events != last_events || g_hits != last_hits || ++calls % 200 == 1)
  Log("lua: PSTF_GoreState -> particles %zu decals %d air %d events %llu swallowed %llu hits %llu crits %llu "
      "draws %llu last %s",
      g_ps.p.size(), decals, air, static_cast<unsigned long long>(g_events),
      static_cast<unsigned long long>(g_swallowed), static_cast<unsigned long long>(g_hits),
      static_cast<unsigned long long>(g_crits), static_cast<unsigned long long>(g_draws), g_last_material.c_str());
  last_events = g_events;
  last_hits = g_hits;
  engine::LuaPushNumber(L, double(g_ps.p.size()));
  engine::LuaPushNumber(L, decals);
  engine::LuaPushNumber(L, air);
  engine::LuaPushNumber(L, double(g_events));
  engine::LuaPushNumber(L, double(g_swallowed));
  engine::LuaPushNumber(L, double(g_hits));
  engine::LuaPushNumber(L, double(g_crits));
  engine::LuaPushString(L, g_last_material.c_str());
  return 8;
}

// PSTF_GoreHit(id, severity[, crit[, anim]]) -> material name: a hit on a creature as AddBlood would make it
// (for tests and screenshots; anim overrides the creature's animation id).
int __cdecl LuaHit(engine::LuaState* L) {
  const int32_t id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const int sev = static_cast<int>(engine::LuaNumberArg(L, 2, 2));
  const bool crit = engine::LuaNumberArg(L, 3, 0) != 0;
  int anim = static_cast<int>(engine::LuaNumberArg(L, 4, -1));
  engine::SpriteInfo sp;
  if (!engine::GetSprite(id, &sp)) {
    engine::LuaPushString(L, "");
    return 1;
  }
  if (anim < 0) anim = engine::SpriteAnimId(id);
  Spawn(sp.x, sp.y, anim, int(g_hits * 5) & 15, sev, crit);
  Log("lua: PSTF_GoreHit(%X, %d, %d) anim %d -> %s", static_cast<unsigned>(id), sev, crit ? 1 : 0, anim,
      g_last_material.c_str());
  engine::LuaPushString(L, g_last_material.c_str());
  return 1;
}

}  // namespace

void InstallGore(const IniMap& ini) {
  if (!IniBool(ini, "gore.enabled", false)) {
    Log("gore: disabled in ini");
    return;
  }
  g_set.custom_blood = IniBool(ini, "gore.custom_blood", true);
  g_set.crits = IniBool(ini, "gore.crits", true);
  g_set.particle_scale = float(std::max(50, std::min(400, IniInt(ini, "gore.particle_scale", 150)))) / 100.0f;
  g_ps.linger = float(std::max(2, std::min(600, IniInt(ini, "gore.linger", 30))));
  g_ps.max_particles = size_t(std::max(100, std::min(5000, IniInt(ini, "gore.max_particles", 1500))));
  g_ps.max_decals = g_ps.max_particles * 2 / 5;
  g_ps.rng.s = uint32_t(GetTickCount64()) | 1u;
  const std::string dir = std::string(DataDir()) + "\\assets\\gore\\";
  std::string text;
  if (!ReadFile(dir + "hitmat.2da", &text) || !ParseHitmat(text, &g_table)) Log("gore: hitmat.2da missing");
  if (ReadFile(dir + "hitarmor.tsv", &text)) ParseArmorList(text, &g_table);
  Log("gore: %zu animation armor values, %zu ANIM rows, %zu ARMOR rows", g_table.armor_of_anim.size(),
      g_table.anim_rows.size(), g_table.armor_rows.size());
  if (!engine::InstallBloodHook(&OnBlood)) {
    Log("gore: off, CGameSprite::AddBlood does not match");
    return;
  }
  const bool draw = engine::InstallWorldPreDraw(&PreDraw) && engine::InstallWorldFloor(&DrawFloor) &&
                    engine::InstallWorldComposite(&DrawAir, 40) && engine::InstallWorldComposite(&DrawGlow, 60);
  if (!draw) Log("gore: draw hooks failed (effects are computed but not drawn)");
  dev::AddLuaFunction("PSTF_GoreState", &LuaState);
  dev::AddLuaFunction("PSTF_GoreHit", &LuaHit);
  Log("gore: installed (custom blood %d, crits %d, linger %.0f s, max %zu particles, scale %.2f)",
      g_set.custom_blood ? 1 : 0, g_set.crits ? 1 : 0, g_ps.linger, g_ps.max_particles, g_set.particle_scale);
}

}  // namespace pstf::gore
