// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/gl_world.h"

#include <windows.h>

#include <cctype>

#include "core/crashlog.h"
#include "core/log.h"
#include "core/patch.h"
#include "engine/sprite_ptr.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {

// GL enums (glcorearb.h / gl.h).
constexpr unsigned kCurrentProgram = 0x8B8D, kActiveTexture = 0x84E0, kTexture0 = 0x84C0,
                   kTexBinding2D = 0x8069, kDrawFbo = 0x8CA6, kReadFbo = 0x8CAA, kDrawFramebuffer = 0x8CA9,
                   kReadFramebuffer = 0x8CA8, kViewport = 0x0BA2, kBlendSrcRgb = 0x80C9, kBlendDstRgb = 0x80C8,
                   kBlendSrcA = 0x80CB, kBlendDstA = 0x80CA, kBlendEqRgb = 0x8009, kBlendEqA = 0x883D,
                   kUnpackAlign = 0x0CF5, kPackAlign = 0x0D05, kUnpackRow = 0x0CF2, kClearColor = 0x0C22,
                   kColorWritemask = 0x0C23, kBlend = 0x0BE2, kScissor = 0x0C11, kDepth = 0x0B71, kCull = 0x0B44,
                   kStencil = 0x0B90, kAlphaTest = 0x0BC0, kTex2D = 0x0DE1, kDither = 0x0BD0;

template <class F>
bool Get(F* out, HMODULE gl, const char* name) {
  void* p = reinterpret_cast<void*>(GetProcAddress(gl, name));
  if (!p) {
    using WglGet = PROC(WINAPI*)(LPCSTR);
    static auto wgl = reinterpret_cast<WglGet>(GetProcAddress(gl, "wglGetProcAddress"));
    if (wgl) {
      p = reinterpret_cast<void*>(wgl(name));
      const auto v = reinterpret_cast<intptr_t>(p);
      if (v >= -1 && v <= 3) p = nullptr;  // wglGetProcAddress failure values
    }
  }
  *out = reinterpret_cast<F>(p);
  return p != nullptr;
}

template <class T>
T R(uint64_t va) {
  return *reinterpret_cast<const volatile T*>(va);
}

using PassFn = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
struct Site {
  uint32_t rva;
  uint64_t expect;
  PassFn original;
};
Site g_sites[4] = {{sym::kRvaFloatMsgCall, sym::kVaFloatMsgRender, nullptr},
                   {sym::kRvaTransitionsCall, sym::kVaRenderTransitions, nullptr},
                   {sym::kRvaFogCall, sym::kVaRenderFog, nullptr},
                   {sym::kRvaPostRenderCall, sym::kVaPostRender, nullptr}};
CompositeFn g_fn = nullptr;
bool g_armed = false, g_faulted = false;

void CallComposite(uint64_t) { g_fn(); }

void RunComposite() {
  if (!g_armed || g_faulted || !g_fn) return;
  g_armed = false;
  uint32_t code = 0;
  if (!GuardedCall(&CallComposite, 0, &code)) {
    g_faulted = true;
    Log("lighting: composite faulted (%08X); lighting off for this session", code);
  }
}

// Each detour forwards up to four integer arguments unchanged (x64: rcx, rdx, r8, r9).
template <int I>
uint64_t __fastcall Detour(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  RunComposite();
  return g_sites[I].original(a, b, c, d);
}
void* const kDetours[4] = {reinterpret_cast<void*>(&Detour<0>), reinterpret_cast<void*>(&Detour<1>),
                           reinterpret_cast<void*>(&Detour<2>), reinterpret_cast<void*>(&Detour<3>)};

const sym::Symbol* Find(uint32_t rva) {
  for (const auto& s : sym::kAll)
    if (s.rva == rva) return &s;
  return nullptr;
}
}  // namespace

std::string LoadGl(Gl* g) {
  HMODULE gl = GetModuleHandleW(L"opengl32.dll");
  if (!gl) return "opengl32 not loaded";
  std::string miss;
#define L(member, name) \
  if (!Get(&g->member, gl, name)) miss += std::string(miss.empty() ? "" : " ") + name;
  L(GetIntegerv, "glGetIntegerv") L(GetFloatv, "glGetFloatv") L(IsEnabled, "glIsEnabled") L(Enable, "glEnable")
  L(Disable, "glDisable") L(BindTexture, "glBindTexture") L(GenTextures, "glGenTextures")
  L(DeleteTextures, "glDeleteTextures") L(TexImage2D, "glTexImage2D") L(TexParameteri, "glTexParameteri")
  L(CopyTexSubImage2D, "glCopyTexSubImage2D") L(Viewport, "glViewport") L(BlendFunc, "glBlendFunc")
  L(ClearColor, "glClearColor") L(Clear, "glClear") L(Begin, "glBegin") L(End, "glEnd") L(Vertex2f, "glVertex2f")
  L(ReadPixels, "glReadPixels") L(PixelStorei, "glPixelStorei") L(ColorMask, "glColorMask")
  L(GetString, "glGetString") L(GetError, "glGetError")
  L(ActiveTexture, "glActiveTexture") L(UseProgram, "glUseProgram") L(CreateShader, "glCreateShader")
  L(ShaderSource, "glShaderSource") L(CompileShader, "glCompileShader") L(GetShaderiv, "glGetShaderiv")
  L(GetShaderInfoLog, "glGetShaderInfoLog") L(DeleteShader, "glDeleteShader") L(CreateProgram, "glCreateProgram")
  L(AttachShader, "glAttachShader") L(LinkProgram, "glLinkProgram") L(GetProgramiv, "glGetProgramiv")
  L(GetProgramInfoLog, "glGetProgramInfoLog") L(GetUniformLocation, "glGetUniformLocation")
  L(Uniform1i, "glUniform1i") L(Uniform1f, "glUniform1f") L(Uniform2f, "glUniform2f") L(Uniform3f, "glUniform3f")
  L(Uniform4f, "glUniform4f") L(GenFramebuffers, "glGenFramebuffers") L(BindFramebuffer, "glBindFramebuffer")
  L(FramebufferTexture2D, "glFramebufferTexture2D") L(CheckFramebufferStatus, "glCheckFramebufferStatus")
  L(BlendEquationSeparate, "glBlendEquationSeparate") L(BlendFuncSeparate, "glBlendFuncSeparate")
#undef L
  return miss.empty() ? "" : "missing " + miss;
}

void SaveGlState(const Gl& gl, GlState* s) {
  gl.GetIntegerv(kCurrentProgram, &s->program);
  gl.GetIntegerv(kActiveTexture, &s->active_tex);
  for (int i = 0; i < 5; ++i) {
    gl.ActiveTexture(kTexture0 + i);
    gl.GetIntegerv(kTexBinding2D, &s->tex[i]);
  }
  gl.ActiveTexture(static_cast<unsigned>(s->active_tex));
  gl.GetIntegerv(kDrawFbo, &s->draw_fbo);
  gl.GetIntegerv(kReadFbo, &s->read_fbo);
  gl.GetIntegerv(kViewport, s->viewport);
  gl.GetIntegerv(kBlendSrcRgb, &s->blend_src_rgb);
  gl.GetIntegerv(kBlendDstRgb, &s->blend_dst_rgb);
  gl.GetIntegerv(kBlendSrcA, &s->blend_src_a);
  gl.GetIntegerv(kBlendDstA, &s->blend_dst_a);
  gl.GetIntegerv(kBlendEqRgb, &s->blend_eq_rgb);
  gl.GetIntegerv(kBlendEqA, &s->blend_eq_a);
  gl.GetIntegerv(kUnpackAlign, &s->unpack_align);
  gl.GetIntegerv(kPackAlign, &s->pack_align);
  gl.GetIntegerv(kUnpackRow, &s->unpack_row);
  gl.GetFloatv(kClearColor, s->clear);
  gl.GetIntegerv(kColorWritemask, s->color_mask);
  s->blend = gl.IsEnabled(kBlend);
  s->scissor = gl.IsEnabled(kScissor);
  s->depth = gl.IsEnabled(kDepth);
  s->cull = gl.IsEnabled(kCull);
  s->stencil = gl.IsEnabled(kStencil);
  s->alpha_test = gl.IsEnabled(kAlphaTest);
  s->tex2d = gl.IsEnabled(kTex2D);
  s->dither = gl.IsEnabled(kDither);
}

void RestoreGlState(const Gl& gl, const GlState& s) {
  auto set = [&](unsigned cap, unsigned char on) { on ? gl.Enable(cap) : gl.Disable(cap); };
  gl.UseProgram(static_cast<unsigned>(s.program));
  for (int i = 4; i >= 0; --i) {
    gl.ActiveTexture(kTexture0 + i);
    gl.BindTexture(kTex2D, static_cast<unsigned>(s.tex[i]));
  }
  gl.ActiveTexture(static_cast<unsigned>(s.active_tex));
  gl.BindFramebuffer(kDrawFramebuffer, static_cast<unsigned>(s.draw_fbo));
  gl.BindFramebuffer(kReadFramebuffer, static_cast<unsigned>(s.read_fbo));
  gl.Viewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
  gl.BlendFuncSeparate(s.blend_src_rgb, s.blend_dst_rgb, s.blend_src_a, s.blend_dst_a);
  gl.BlendEquationSeparate(s.blend_eq_rgb, s.blend_eq_a);
  gl.PixelStorei(kUnpackAlign, s.unpack_align);
  gl.PixelStorei(kPackAlign, s.pack_align);
  gl.PixelStorei(kUnpackRow, s.unpack_row);
  gl.ClearColor(s.clear[0], s.clear[1], s.clear[2], s.clear[3]);
  gl.ColorMask(s.color_mask[0] != 0, s.color_mask[1] != 0, s.color_mask[2] != 0, s.color_mask[3] != 0);
  set(kBlend, s.blend);
  set(kScissor, s.scissor);
  set(kDepth, s.depth);
  set(kCull, s.cull);
  set(kStencil, s.stencil);
  set(kAlphaTest, s.alpha_test);
  set(kTex2D, s.tex2d);
  set(kDither, s.dither);
}

bool GlWorldAvailable() {
  static int ok = -1;
  if (ok < 0) {
    int n = 0, want = 0;
    for (const auto& s : sym::kAll) {
      const uint32_t r = s.rva;
      if (r == sym::kRvaDrawFlush || r == sym::kRvaBeginScaledFlushCall || r == sym::kRvaEndScaledFlushCall ||
          r == sym::kRvaWorldStStore || r == sym::kRvaProgramTableLea || r == sym::kRvaAreaHeaderCopy ||
          r == sym::kRvaAreaTypeRead) {
        ++want;
        n += CheckSymbol(s) ? 1 : 0;
      }
    }
    ok = (want == 7 && n == 7) ? 1 : 0;
  }
  return ok == 1;
}

void EngineFlushBatch() { reinterpret_cast<void(__fastcall*)()>(Va(sym::kRvaDrawFlush))(); }

void WorldMatrix(float st[4]) {
  for (int i = 0; i < 4; ++i) st[i] = R<float>(sym::kVaWorldSt + 4ull * i);
}

unsigned EngineProgram(int slot) {
  if (slot < 0 || slot > 8) return 0;
  return R<uint32_t>(sym::kVaProgramTable + uint64_t(sym::kProgramEntrySize) * slot);
}

std::string AreaWed(uint64_t area) {
  if (!PlausiblePtr(area)) return "";
  std::string s;
  for (int i = 0; i < 8; ++i) {
    const char c = R<char>(area + sym::kOffAreaWed + i);
    if (!c) break;
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return "";
    s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

uint16_t AreaTypeFlags(uint64_t area) { return PlausiblePtr(area) ? R<uint16_t>(area + sym::kOffAreaType) : 0; }

bool InstallWorldComposite(CompositeFn fn) {
  if (g_fn) return true;
  if (!GlWorldAvailable()) return false;
  const sym::Symbol* s[4];
  for (int i = 0; i < 4; ++i) {
    s[i] = Find(g_sites[i].rva);
    if (!s[i] || !CheckSymbol(*s[i])) return false;
  }
  g_fn = fn;
  for (int i = 0; i < 4; ++i) {
    const uint64_t orig = RedirectCall(*s[i], kDetours[i]);
    if (orig != g_sites[i].expect) {
      Log("lighting: call site %X not patched (target %llX)", g_sites[i].rva, static_cast<unsigned long long>(orig));
      if (orig) g_sites[i].original = reinterpret_cast<PassFn>(orig);  // keep the patched site working
      else return false;
    }
    g_sites[i].original = reinterpret_cast<PassFn>(orig);
  }
  return true;
}

void ArmWorldComposite() { g_armed = true; }

}  // namespace pstf::engine
