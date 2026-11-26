// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/statusfx/statusfx_module.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/inflate.h"
#include "core/log.h"
#include "engine/combat.h"
#include "engine/frame.h"
#include "engine/gl_world.h"
#include "engine/lua.h"
#include "engine/render.h"
#include "engine/sprites.h"
#include "modules/dev/lua_bridge.h"
#include "modules/lighting/lighting_logic.h"
#include "modules/statusfx/statusfx_logic.h"

namespace pstf::statusfx {
namespace {

constexpr unsigned kQuads = 0x0007, kBlend = 0x0BE2, kVertexShader = 0x8B31, kFragmentShader = 0x8B30,
                   kCompileStatus = 0x8B81, kLinkStatus = 0x8B82, kSrcAlpha = 0x0302, kOneMinusSrcAlpha = 0x0303,
                   kOne = 1, kFuncAdd = 0x8006, kScissor = 0x0C11, kDepth = 0x0B71, kCull = 0x0B44,
                   kStencil = 0x0B90, kAlphaTest = 0x0BC0, kTexture2D = 0x0DE1, kTexture0 = 0x84C0,
                   kRgba = 0x1908, kUnsignedByte = 0x1401, kMinFilter = 0x2801, kMagFilter = 0x2800,
                   kLinear = 0x2601, kLinearMipLinear = 0x2703, kWrapS = 0x2802, kWrapT = 0x2803,
                   kClampToEdge = 0x812F, kUnpackAlignment = 0x0CF5, kUnpackRowLength = 0x0CF2, kMaxLevel = 0x813D;

System g_sys;
Watcher g_watch;
GlowCutter g_cut;
bool g_resist_on = true;
std::wstring g_resist_sound;  // pst_forever\sounds\spell_resist.wav, "" = silent
engine::Gl g_gl = {};
void(__stdcall* g_gen_mipmap)(unsigned) = nullptr;
bool g_gl_tried = false;
unsigned g_prog = 0, g_tex = 0;
int u_target = -1, u_tex = -1;
std::string g_why = "not drawn yet";
uint64_t g_area = 0;
ULONGLONG g_last_tick = 0, g_last_scan = 0;
std::vector<int32_t> g_owners;  // creatures with particles or instances (draw list)
// probes
uint64_t g_quads = 0, g_frames = 0, g_cut_glows = 0, g_blanked = 0, g_resists = 0, g_starts = 0;
std::string g_last = "-";

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
const char* kFs = R"(#version 130
uniform sampler2D u_tex;
in vec2 v_uv;
in vec4 v_col;
void main() {
  vec4 t = texture(u_tex, v_uv);
  gl_FragColor = vec4(t.rgb * v_col.rgb, t.a * v_col.a);
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
    Log("statusfx: shader failed: %s", buf);
    g_gl.DeleteShader(s);
    return 0;
  }
  return s;
}

// data/statusfx_atlas.bin (tools/make_statusfx_art.py): "PFA1", u16 w, h, cell, columns, zlib RGBA8.
bool LoadAtlas(std::vector<uint8_t>* rgba, int* w, int* h) {
  const std::string path = std::string(DataDir()) + "\\assets\\statusfx\\statusfx_atlas.bin";
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    g_why = "atlas missing: " + path;
    return false;
  }
  std::vector<uint8_t> in;
  uint8_t buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) in.insert(in.end(), buf, buf + n);
  fclose(f);
  auto u16 = [&](size_t o) { return int(in[o] | (in[o + 1] << 8)); };
  if (in.size() < 12 || in[0] != 'P' || in[1] != 'F' || in[2] != 'A' || in[3] != '1') {
    g_why = "atlas header";
    return false;
  }
  *w = u16(4), *h = u16(6);
  if (u16(8) != kAtlasCell || u16(10) != kAtlasCols || *w != kAtlasCell * kAtlasCols || *h != kAtlasCell * kAtlasRows) {
    g_why = "atlas layout differs from statusfx_logic.h";
    return false;
  }
  const size_t want = size_t(*w) * size_t(*h) * 4;
  if (!ZlibInflate(in.data() + 12, in.size() - 12, rgba, want) || rgba->size() != want) {
    g_why = "atlas data";
    return false;
  }
  return true;
}

bool SetUpGl() {
  if (g_prog) return true;
  if (g_gl_tried) return false;
  g_gl_tried = true;
  const std::string err = engine::LoadGl(&g_gl);
  if (!err.empty()) {
    g_why = "GL " + err;
    Log("statusfx: drawing off, %s", g_why.c_str());
    return false;
  }
  if (!engine::EngineProgram(0)) {
    g_why = "the engine runs without shaders";
    Log("statusfx: drawing off, %s", g_why.c_str());
    return false;
  }
  if (HMODULE gl = GetModuleHandleW(L"opengl32.dll"))
    if (auto wgl = reinterpret_cast<PROC(WINAPI*)(LPCSTR)>(GetProcAddress(gl, "wglGetProcAddress")))
      g_gen_mipmap = reinterpret_cast<void(__stdcall*)(unsigned)>(wgl("glGenerateMipmap"));
  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
  if (!LoadAtlas(&rgba, &w, &h)) {
    Log("statusfx: drawing off, %s", g_why.c_str());
    return false;
  }
  const unsigned v = Compile(kVertexShader, kVs), f = Compile(kFragmentShader, kFs);
  if (!v || !f) {
    g_why = "shader compile";
    return false;
  }
  const unsigned p = g_gl.CreateProgram();
  g_gl.AttachShader(p, v);
  g_gl.AttachShader(p, f);
  g_gl.LinkProgram(p);
  g_gl.DeleteShader(v);
  g_gl.DeleteShader(f);
  int ok = 0;
  g_gl.GetProgramiv(p, kLinkStatus, &ok);
  if (!ok) {
    g_why = "program link";
    Log("statusfx: drawing off, %s", g_why.c_str());
    return false;
  }
  u_target = g_gl.GetUniformLocation(p, "u_target");
  u_tex = g_gl.GetUniformLocation(p, "u_tex");
  engine::GlState saved;
  engine::SaveGlState(g_gl, &saved);
  g_gl.ActiveTexture(kTexture0);
  g_gl.GenTextures(1, &g_tex);
  g_gl.BindTexture(kTexture2D, g_tex);
  g_gl.PixelStorei(kUnpackAlignment, 4);
  g_gl.PixelStorei(kUnpackRowLength, 0);
  g_gl.TexImage2D(kTexture2D, 0, kRgba, w, h, 0, kRgba, kUnsignedByte, rgba.data());
  g_gl.TexParameteri(kTexture2D, kWrapS, kClampToEdge);
  g_gl.TexParameteri(kTexture2D, kWrapT, kClampToEdge);
  g_gl.TexParameteri(kTexture2D, kMagFilter, kLinear);
  if (g_gen_mipmap) {
    g_gl.TexParameteri(kTexture2D, kMaxLevel, 4);  // 128 px cells -> 8 px: no bleeding between cells
    g_gen_mipmap(kTexture2D);
    g_gl.TexParameteri(kTexture2D, kMinFilter, kLinearMipLinear);
  } else {
    g_gl.TexParameteri(kTexture2D, kMinFilter, kLinear);
  }
  engine::RestoreGlState(g_gl, saved);
  g_prog = p;
  g_why = "ok";
  Log("statusfx: GL ready (atlas %dx%d, mipmaps %s)", w, h, g_gen_mipmap ? "on" : "off");
  return true;
}

// --- per frame ---------------------------------------------------------------------------------------
void Remember(int32_t id) {
  if (std::find(g_owners.begin(), g_owners.end(), id) == g_owners.end()) g_owners.push_back(id);
}

void OnResist(int32_t target) {
  ++g_resists;
  if (!g_resist_on) return;
  g_sys.Start(Fx::Mantle, target);
  Remember(target);
  if (!g_resist_sound.empty()) PlaySoundW(g_resist_sound.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
  ++g_starts;
  g_last = "mantle";
  Log("statusfx: %X resisted a spell: mantle", static_cast<unsigned>(target));
}

// States -> effects, and the engine's colour glows cut (10 times a second).
void Scan(double now) {
  for (const engine::SpriteInfo& sp : engine::AllSprites()) {
    if (sp.area != g_area) continue;
    const int64_t st = engine::SpriteState(sp.id);
    if (st < 0) continue;
    const size_t before = g_sys.inst.size();
    g_watch.Step(sp.id, static_cast<uint32_t>(st), now, &g_sys);
    if (g_sys.inst.size() != before) {
      g_starts += g_sys.inst.size() - before;
      g_last = FxName(g_sys.inst.back().fx);
      Remember(sp.id);
      Log("statusfx: %X state %X: %s", static_cast<unsigned>(sp.id), static_cast<unsigned>(st), g_last.c_str());
    }
    std::vector<EffectInfo> effs;
    for (const engine::EffectEntry& e : engine::EffectEntries(sp.id)) {
      if (!IsGlowOpcode(e.opcode) && !ReplacedStateOpcode(e.opcode, g_cut.on) && e.opcode != 369 && e.opcode != 74)
        continue;
      EffectInfo i;
      i.key = e.key;
      i.opcode = e.opcode;
      i.type = e.type;
      i.equipped = e.equipped;
      i.parent = e.parent;
      i.res = e.res;
      effs.push_back(i);
    }
    const CutPlan plan = g_cut.Plan(sp.id, effs, now);
    for (uint64_t k : plan.expire)
      if (engine::ExpireEffectNow(sp.id, k)) ++g_cut_glows;
    for (uint64_t k : plan.blank)
      if (engine::SetEffectResource(sp.id, k, kBlankBam)) {
        ++g_blanked;
        Log("statusfx: %X: blinding overlay blanked", static_cast<unsigned>(sp.id));
      }
  }
}

void PreDraw() {
  engine::ArmWorldComposite();
  const uint64_t area = engine::WorldPassArea();
  const ULONGLONG now = GetTickCount64();
  float dt = g_last_tick ? float(std::min<ULONGLONG>(now - g_last_tick, 100)) / 1000.0f : 0.0f;
  g_last_tick = now;
  if (area != g_area) {  // another area: nothing carries over
    g_sys.Clear();
    g_watch.Clear();
    g_owners.clear();
    g_area = area;
    dt = 0;
  }
  if (!area) return;
  if (now - g_last_scan >= 100) {
    g_last_scan = now;
    Scan(double(now) / 1000.0);
  }
  g_sys.Update(dt);
  g_owners.erase(std::remove_if(g_owners.begin(), g_owners.end(),
                                [](int32_t id) {
                                  if (g_sys.Count(id)) return false;
                                  for (const Instance& in : g_sys.inst)
                                    if (in.owner == id) return false;
                                  return true;
                                }),
                 g_owners.end());
}

void Draw() {
  if (g_sys.p.empty()) return;
  const uint64_t area = engine::WorldPassArea();
  if (!area) return;
  std::vector<Quad> quads;
  for (int32_t id : g_owners) {
    engine::SpriteInfo sp;
    if (!engine::GetSprite(id, &sp) || sp.area != area) continue;
    Anchor a;
    int x = sp.x, y = sp.y;
    engine::InterpDrawPos(id, &x, &y);
    a.x = float(x);
    a.y = float(y);
    const int h = engine::SpriteTextHeight(id);
    a.head_px = h > 0 ? float(h) : 70.0f;
    g_sys.Draw(id, a, &quads);
  }
  if (quads.empty()) return;
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
  for (unsigned cap : {kScissor, kDepth, kCull, kStencil, kAlphaTest}) gl.Disable(cap);
  gl.ColorMask(1, 1, 1, 0);
  gl.UseProgram(g_prog);
  gl.Uniform2f(u_target, float(vw), float(vh));
  gl.ActiveTexture(kTexture0);
  gl.BindTexture(kTexture2D, g_tex);
  gl.Uniform1i(u_tex, 0);
  gl.Enable(kBlend);
  gl.BlendEquationSeparate(kFuncAdd, kFuncAdd);
  const float cw = 1.0f / kAtlasCols, ch = 1.0f / kAtlasRows;
  for (int pass = 0; pass < 2; ++pass) {
    const bool add = pass == 0;
    if (add) gl.BlendFuncSeparate(kSrcAlpha, kOne, kSrcAlpha, kOne);
    else gl.BlendFuncSeparate(kSrcAlpha, kOneMinusSrcAlpha, kSrcAlpha, kOneMinusSrcAlpha);
    gl.Begin(kQuads);
    for (const Quad& q : quads) {
      if (q.additive != add) continue;
      const float u0 = (q.cell % kAtlasCols) * cw, v0 = (q.cell / kAtlasCols) * ch;
      const float c = std::cos(q.rot), s = std::sin(q.rot), hw = q.w * 0.5f, hh = q.h * 0.5f;
      const float cx[4] = {-hw, hw, hw, -hw}, cy[4] = {-hh, -hh, hh, hh};
      const float tu[4] = {u0, u0 + cw, u0 + cw, u0}, tv[4] = {v0, v0, v0 + ch, v0 + ch};
      gl.Color4f(q.r, q.g, q.b, q.a);
      for (int i = 0; i < 4; ++i) {
        const float ax = q.x + cx[i] * c - cy[i] * s, ay = q.y + cx[i] * s + cy[i] * c;
        float px, py;
        lighting::ToPhys(vm, ax, ay, &px, &py);
        gl.TexCoord2f(tu[i], tv[i]);
        gl.Vertex2f(px, py);
      }
      ++g_quads;
    }
    gl.End();
  }
  ++g_frames;
  gl.Color4f(1, 1, 1, 1);
  engine::RestoreGlState(gl, saved);
}

// --- Lua probes --------------------------------------------------------------------------------------
// PSTF_StatusFx() -> effects running, particles, quads drawn, frames drawn, glows cut, overlays blanked,
// spells resisted, effects started, last effect name, GL status.
int __cdecl LuaState(engine::LuaState* L) {
  static uint64_t last_sig = ~0ull, calls = 0;  // log when something but the draw counters changed
  const uint64_t sig = g_sys.inst.size() * 1000003ull + g_cut_glows * 1009ull + g_blanked * 101ull + g_resists * 7ull + g_starts;
  if (sig != last_sig || ++calls % 200 == 1)
  Log("lua: PSTF_StatusFx -> inst %zu particles %zu quads %llu frames %llu cut %llu blanked %llu resists %llu "
      "starts %llu last %s gl %s",
      g_sys.inst.size(), g_sys.p.size(), static_cast<unsigned long long>(g_quads),
      static_cast<unsigned long long>(g_frames), static_cast<unsigned long long>(g_cut_glows),
      static_cast<unsigned long long>(g_blanked), static_cast<unsigned long long>(g_resists),
      static_cast<unsigned long long>(g_starts), g_last.c_str(), g_why.c_str());
  last_sig = sig;
  engine::LuaPushNumber(L, double(g_sys.inst.size()));
  engine::LuaPushNumber(L, double(g_sys.p.size()));
  engine::LuaPushNumber(L, double(g_quads));
  engine::LuaPushNumber(L, double(g_frames));
  engine::LuaPushNumber(L, double(g_cut_glows));
  engine::LuaPushNumber(L, double(g_blanked));
  engine::LuaPushNumber(L, double(g_resists));
  engine::LuaPushNumber(L, double(g_starts));
  engine::LuaPushString(L, g_last.c_str());
  engine::LuaPushString(L, g_why.c_str());
  return 10;
}

// PSTF_StatusFxOn(id) -> "fx,fx" running on that creature (loops still emitting), particles of it, the
// state bits the watcher saw, the overhead text height px.
int __cdecl LuaOn(engine::LuaState* L) {
  const int32_t id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  std::string names;
  for (const Instance& in : g_sys.inst)
    if (in.owner == id && !in.stopping) names += std::string(names.empty() ? "" : ",") + FxName(in.fx);
  const int n = g_sys.Count(id);
  const int h = engine::SpriteTextHeight(id);
  Log("lua: PSTF_StatusFxOn(%X) -> %s particles %d bits %X height %d", static_cast<unsigned>(id),
      names.empty() ? "-" : names.c_str(), n, g_watch.Bits(id), h);
  engine::LuaPushString(L, names.c_str());
  engine::LuaPushNumber(L, n);
  engine::LuaPushNumber(L, double(g_watch.Bits(id)));
  engine::LuaPushNumber(L, h);
  return 4;
}

// PSTF_StatusFxPlay(id, name) -> 1: plays an effect on a creature (screenshots; "stop:<name>" stops a loop).
int __cdecl LuaPlay(engine::LuaState* L) {
  const int32_t id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  std::string name = engine::LuaStringArg(L, 2);
  const bool stop = name.rfind("stop:", 0) == 0;
  if (stop) name = name.substr(5);
  Fx fx;
  engine::SpriteInfo sp;
  const bool ok = FxFromName(name, &fx) && engine::GetSprite(id, &sp);
  if (ok) {
    if (stop) g_sys.Stop(fx, id);
    else g_sys.Start(fx, id);
    Remember(id);
    g_last = name;
  }
  Log("lua: PSTF_StatusFxPlay(%X, %s%s) -> %d", static_cast<unsigned>(id), stop ? "stop:" : "", name.c_str(), ok ? 1 : 0);
  engine::LuaPushNumber(L, ok ? 1 : 0);
  return 1;
}

// Test probe: PSTF_StatusFxApply(id, opcode, param1, param2, seconds) -> 1: one effect from no source, no
// save, timing 0 (lasts `seconds`), the engine's way (engine::ApplyEffect).
int __cdecl LuaApply(engine::LuaState* L) {
  const int32_t id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  engine::EffectSpec e;
  e.opcode = static_cast<uint16_t>(engine::LuaNumberArg(L, 2, 0));
  e.param1 = static_cast<uint32_t>(engine::LuaNumberArg(L, 3, 0));
  e.param2 = static_cast<uint32_t>(engine::LuaNumberArg(L, 4, 0));
  e.timing = 0;
  e.duration = static_cast<uint32_t>(engine::LuaNumberArg(L, 5, 10));
  const bool ok = engine::ApplyEffect(id, -1, e);
  Log("lua: PSTF_StatusFxApply(%X, op %u, %u, %X, %u s) -> %d", static_cast<unsigned>(id), e.opcode, e.param1,
      e.param2, e.duration, ok ? 1 : 0);
  engine::LuaPushNumber(L, ok ? 1 : 0);
  return 1;
}

}  // namespace

void InstallStatusFx(const IniMap& ini) {
  if (!IniBool(ini, "statusfx.enabled", false)) {
    Log("statusfx: disabled in ini");
    return;
  }
  Toggles on;
  on.fear = IniBool(ini, "statusfx.fear", true);
  on.stun = IniBool(ini, "statusfx.stun", true);
  on.blind = IniBool(ini, "statusfx.blind", true);
  on.poison = IniBool(ini, "statusfx.poison", true);
  on.silence = IniBool(ini, "statusfx.silence", true);
  g_watch.on = on;
  g_cut.on = on;
  g_watch.repeat = float(std::max(0, std::min(600, IniInt(ini, "statusfx.repeat", 6))));
  g_cut.cut_after = float(std::max(0, std::min(600, IniInt(ini, "statusfx.glow_seconds", 2))));
  g_sys.scale = float(std::max(25, std::min(400, IniInt(ini, "statusfx.size", 100)))) / 100.0f;
  g_sys.rng.s = uint32_t(GetTickCount64()) | 1u;
  g_resist_on = IniBool(ini, "statusfx.resist", true);
  if (IniBool(ini, "statusfx.resist_sound", true)) {
    const std::string d = std::string(DataDir()) + "\\sounds\\spell_resist.wav";
    g_resist_sound.assign(d.begin(), d.end());
    if (GetFileAttributesW(g_resist_sound.c_str()) == INVALID_FILE_ATTRIBUTES) {
      Log("statusfx: %s missing: the mantle plays silent", d.c_str());
      g_resist_sound.clear();
    }
  }
  if (g_resist_on) {
    if (engine::InstallRollHooks()) engine::SetResistListener(&OnResist);
    else Log("statusfx: CheckSave hook failed: no spell mantle");
  }
  const bool draw = engine::InstallWorldPreDraw(&PreDraw) && engine::InstallWorldComposite(&Draw, 65);
  if (!draw) Log("statusfx: draw hooks failed (effects are computed but not drawn)");
  dev::AddLuaFunction("PSTF_StatusFx", &LuaState);
  dev::AddLuaFunction("PSTF_StatusFxOn", &LuaOn);
  dev::AddLuaFunction("PSTF_StatusFxPlay", &LuaPlay);
  dev::AddLuaFunction("PSTF_StatusFxApply", &LuaApply);
  Log("statusfx: installed (fear %d stun %d blind %d poison %d silence %d resist %d, glows %.0f s, repeat %.0f s, "
      "size %.2f)",
      on.fear, on.stun, on.blind, on.poison, on.silence, g_resist_on ? 1 : 0, g_cut.cut_after, g_watch.repeat,
      g_sys.scale);
}

}  // namespace pstf::statusfx
