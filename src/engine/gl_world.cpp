// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/gl_world.h"
#include "engine/render.h"

#include <windows.h>

#include <cctype>
#include <cstring>

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

// --- glow gather -------------------------------------------------------------------------------------
constexpr int kMaxGlow = 256;
EngineGlowDraw g_glow[kMaxGlow];
int g_glow_n = 0, g_glow_dropped = 0;
uint64_t g_cell = 0;  // CVidCell of the Render3d / FXRender3d being run (0 outside)
// Effects draw in two steps: CInfinity::FXRender(cell) -> CVidCell::FXRender3d into the FX surface, then
// CInfinity::FXBltFrom -> CVidMode::FXBltToBack -> RenderTexture without a cell (lin 3A86D0, 681150).
// The last FX cell is the one that surface holds.
uint64_t g_fx_cell = 0;

using RenderTextureFn = uint64_t(__fastcall*)(int, int, const int*, uint64_t, const int*, const int*, uint32_t);
using Cell10Fn = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t);
RenderTextureFn g_render_texture = nullptr;
Cell10Fn g_render3d = nullptr, g_fx_render3d = nullptr;

// Cells drawn into the FX surface since the last cell-less RenderTexture (the blit of that surface).
struct FxCell {
  char res[9];
  uint16_t cyc, frm;
  int x, y;          // draw point in the surface (FXRender3d x, y)
  int w, h, cx, cy;  // frame header (0 when unreadable)
  uint32_t flags;
};
constexpr int kMaxFxCells = 32;
FxCell g_fx_cells[kMaxFxCells];
int g_fx_n = 0;
uint64_t g_fx_blits = 0, g_fx_pure = 0, g_fx_soft = 0, g_fx_cells_seen = 0;

void ReadFrameHeader(uint64_t cell, FxCell* f) {
  const uint64_t hdr = R<uint64_t>(cell + sym::kOffCellFrameHdr);
  if (!hdr || !PlausiblePtr(hdr)) return;
  f->w = R<uint16_t>(hdr);
  f->h = R<uint16_t>(hdr + 2);
  f->cx = R<int16_t>(hdr + 4);
  f->cy = R<int16_t>(hdr + 6);
}

EngineGlowDraw* NewGlow() {
  if (g_glow_n >= kMaxGlow) {
    ++g_glow_dropped;
    return nullptr;
  }
  EngineGlowDraw& d = g_glow[g_glow_n++];
  d = {};
  return &d;
}

void Gather(int x, int y, const int* frame, uint32_t flags) {
  if (!g_armed || !WorldPassArea() || !frame) return;
  EngineGlowDraw* dp = NewGlow();
  if (!dp) return;
  EngineGlowDraw& d = *dp;
  d.x = x + frame[0];
  d.y = y + frame[1];
  d.w = frame[2] - frame[0];
  d.h = frame[3] - frame[1];
  d.anchor_x = d.x + d.w / 2;
  d.anchor_y = d.y + d.h;
  d.flags = flags;
  d.resref[0] = 0;
  d.cycle = d.frame = 0;
  const uint64_t cell = g_cell ? g_cell : g_fx_cell;
  if (cell && PlausiblePtr(cell)) {
    for (int i = 0; i < 8; ++i) d.resref[i] = R<char>(cell + sym::kOffCellResRef + i);
    d.resref[8] = 0;
    d.cycle = R<uint16_t>(cell + sym::kOffCellSeq);
    d.frame = R<uint16_t>(cell + sym::kOffCellFrame);
  }
}

EmitFn g_emit_fn = nullptr;
CaptureBeginFn g_cap_begin = nullptr;
CaptureEndFn g_cap_end = nullptr;

// Probe counters (GlowStats): every RenderTexture call, those inside the armed world pass, OR of their flags.
uint64_t g_rt_calls = 0, g_rt_world = 0, g_r3d_calls = 0;
uint32_t g_rt_flags_or = 0;

// Draw trace (probe TraceDraws): logs each distinct (path, resref, flags) of cells whose resref starts with
// the prefix, up to 96 lines.
char g_trace[9] = {};
int g_trace_lines = 0;
struct TraceKey {
  char path, res[9];
  uint32_t flags;
};
TraceKey g_trace_seen[96];

void ReadRes(uint64_t cell, char out[9]) {
  out[0] = 0;
  if (!cell || !PlausiblePtr(cell)) return;
  for (int i = 0; i < 8; ++i) out[i] = R<char>(cell + sym::kOffCellResRef + i);
  out[8] = 0;
}

void Trace(char path, uint64_t cell, uint32_t flags, int w, int h) {
  if (!g_trace[0] || g_trace_lines >= 96) return;
  char res[9];
  ReadRes(cell, res);
  if (strcmp(g_trace, "*") != 0 && _strnicmp(res, g_trace, strlen(g_trace)) != 0) return;
  for (int i = 0; i < g_trace_lines; ++i)
    if (g_trace_seen[i].path == path && g_trace_seen[i].flags == flags && !strcmp(g_trace_seen[i].res, res)) return;
  TraceKey& k = g_trace_seen[g_trace_lines++];
  k.path = path;
  k.flags = flags;
  memcpy(k.res, res, 9);
  Log("lighting: trace %c %-8s seq %u frame %u flags %08X %dx%d world %d", path, res,
      cell ? R<uint16_t>(cell + sym::kOffCellSeq) : 0, cell ? R<uint16_t>(cell + sym::kOffCellFrame) : 0, flags, w,
      h, (g_armed && WorldPassArea()) ? 1 : 0);
}

void GatherFxCell(int x, int y, const FxCell& c, uint32_t flags, bool pure) {
  EngineGlowDraw* d = NewGlow();
  if (!d) return;
  d->anchor_x = x + c.x;
  d->anchor_y = y + c.y;
  d->x = d->anchor_x - c.cx;
  d->y = d->anchor_y - c.cy;
  d->w = c.w;
  d->h = c.h;
  d->flags = flags | c.flags;
  memcpy(d->resref, c.res, 9);
  d->cycle = c.cyc;
  d->frame = c.frm;
  d->from_fx = 1;
  d->pure = pure ? 1 : 0;
}

uint64_t __fastcall RenderTextureDetour(int x, int y, const int* frame, uint64_t size, const int* clip,
                                        const int* clip2, uint32_t flags) {
  ++g_rt_calls;
  const bool world = g_armed && WorldPassArea();
  if (world) {
    ++g_rt_world;
    g_rt_flags_or |= flags;
  }
  if (g_trace[0] && frame)
    Trace(g_cell ? 'R' : 'F', g_cell ? g_cell : g_fx_cell, flags, frame[2] - frame[0], frame[3] - frame[1]);
  int emit = 0;
  bool pure = false;
  const bool fx_blit = !g_cell && g_fx_n > 0;
  if (world && frame && g_emit_fn) {
    if (fx_blit) {
      // Judge each cell of the FX surface: pure effect blits are drawn and captured per pixel.
      int emit_cells = 0, pixel_cells = 0, soft_cells = 0, e[kMaxFxCells];
      for (int i = 0; i < g_fx_n; ++i) {
        const FxCell& c = g_fx_cells[i];
        e[i] = c.res[0] ? g_emit_fn(c.res, c.cyc, c.frm, flags) : 0;
        if (e[i] & 3) ++emit_cells;
        if (e[i] & 1) ++pixel_cells;
        if (e[i] & 4) ++soft_cells;
      }
      pure = pixel_cells == g_fx_n;  // light-only cells (Ignus' hand light) never make a pixel capture
      ++g_fx_blits;
      g_fx_cells_seen += uint64_t(g_fx_n);
      if (pure) ++g_fx_pure;
      for (int i = 0; i < g_fx_n; ++i)
        if (e[i] & 3) {
          FxCell c = g_fx_cells[i];
          if (c.w <= 0) {  // no header: the blit rect, anchored at its bottom centre
            c.w = frame[2] - frame[0];
            c.h = frame[3] - frame[1];
            c.cx = c.x - frame[0];
            c.cy = c.y - frame[1];
          }
          GatherFxCell(x, y, c, flags, pure);
        }
      if (pure) {
        emit = 1;
        if (soft_cells == g_fx_n) emit |= 4;
      } else if (emit_cells) {
        emit = 2;  // a creature composite: lights per emitting cell, no pixels
      }
    } else {
      const uint64_t cell = g_cell ? g_cell : g_fx_cell;
      char res[9];
      ReadRes(cell, res);
      const uint16_t cyc = res[0] ? R<uint16_t>(cell + sym::kOffCellSeq) : 0;
      const uint16_t frm = res[0] ? R<uint16_t>(cell + sym::kOffCellFrame) : 0;
      emit = g_emit_fn(res, cyc, frm, flags);
      if (emit) Gather(x, y, frame, flags);
    }
  }
  if (fx_blit) g_fx_n = 0;  // the surface has been blitted; the next FXRender3d starts a new one
  const bool additive = (flags & 0x208u) && !(flags & 0x20000000u);
  const bool multiply = (flags & 0x20000000u) != 0;
  uint32_t draw_flags = flags;
  // Soften: a normal-alpha effect draw (own cell, or a pure-effect FX blit) uses the screen blend
  // (engine flag 8: 1-DST, ONE), so its dark parts let the scene through instead of painting over it.
  if ((emit & 4) && !additive && !multiply && (g_cell || pure)) {
    draw_flags |= 8u;
    ++g_fx_soft;
  }
  const uint64_t r = g_render_texture(x, y, frame, size, clip, clip2, draw_flags);
  // Emissive capture: the same quad again, normal blend (premultiplied over black), into the module's
  // buffer. The engine's batch is flushed around it so each copy lands in its own target.
  // Normal-blend blits of the FX surface that hold a creature (CInfinity::FXRender per object then one
  // FXBltToBack) are composites: re-drawing one would capture the creature too. Those give lights per
  // emitting cell (gathered above) but no pixels; additive blits, pure-effect blits and draws with
  // their own cell (Render3d) are effect pixels only.
  if ((emit & 1) && g_cap_begin && (additive || g_cell || pure)) {
    EngineFlushBatch();
    if (g_cap_begin()) {
      g_render_texture(x, y, frame, size, clip, clip2, flags & ~(0x208u | 0x20000000u));
      EngineFlushBatch();
      g_cap_end();
    }
  }
  return r;
}

// Render3d has 9 arguments, FXRender3d 7: forwarding 10 register/stack slots covers both (the extra
// slots are read from the caller's frame and ignored by the callee).
uint64_t __fastcall Render3dDetour(uint64_t c, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                                   uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10) {
  const uint64_t keep = g_cell;
  g_cell = c;
  ++g_r3d_calls;
  if (g_trace[0]) Trace('3', c, uint32_t(a8), 0, 0);  // Render3d(x, y, rect, clip, poly, n, flags, alpha)
  const uint64_t r = g_render3d(c, a2, a3, a4, a5, a6, a7, a8, a9, a10);
  g_cell = keep;
  return r;
}
uint64_t __fastcall FxRender3dDetour(uint64_t c, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                                     uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10) {
  const uint64_t keep = g_cell;
  g_cell = c;
  if (g_trace[0]) Trace('X', c, uint32_t(a6), 0, 0);  // FXRender3d(x, y, rect, clip, flags, alpha)
  FxCell* pushed = nullptr;
  if (g_armed && g_fx_n < kMaxFxCells && PlausiblePtr(c)) {
    FxCell& f = g_fx_cells[g_fx_n++];
    pushed = &f;
    ReadRes(c, f.res);
    f.cyc = R<uint16_t>(c + sym::kOffCellSeq);
    f.frm = R<uint16_t>(c + sym::kOffCellFrame);
    f.x = int(a2);
    f.y = int(a3);
    f.flags = uint32_t(a6);
    f.w = f.h = f.cx = f.cy = 0;
    ReadFrameHeader(c, &f);
  }
  const uint64_t r = g_fx_render3d(c, a2, a3, a4, a5, a6, a7, a8, a9, a10);
  // The frame header may be loaded lazily inside FXRender3d (vtable +64 when +0x128 is null): retry after.
  if (pushed && pushed->w == 0) ReadFrameHeader(c, pushed);
  g_cell = keep;
  g_fx_cell = c;
  return r;
}
}  // namespace

bool InstallGlowGather() {
  if (g_render_texture) return true;
  const sym::Symbol* rt = Find(sym::kRvaRenderTexture);
  const sym::Symbol* r3 = Find(sym::kRvaRender3d);
  const sym::Symbol* fx = Find(sym::kRvaFxRender3d);
  const sym::Symbol* bl = Find(sym::kRvaRenderTextureBlend);
  const sym::Symbol* fh = Find(sym::kRvaCellFrameHdrLoad);
  if (!rt || !r3 || !fx || !bl || !fh || !CheckSymbol(*rt) || !CheckSymbol(*r3) || !CheckSymbol(*fx) ||
      !CheckSymbol(*bl) || !CheckSymbol(*fh))
    return false;
  // Cell hooks first: RenderTexture only reads what they set.
  g_render3d = reinterpret_cast<Cell10Fn>(InlineHook(*r3, sym::kRender3dCopy, reinterpret_cast<void*>(&Render3dDetour)));
  g_fx_render3d =
      reinterpret_cast<Cell10Fn>(InlineHook(*fx, sym::kFxRender3dCopy, reinterpret_cast<void*>(&FxRender3dDetour)));
  if (!g_render3d || !g_fx_render3d) {
    Log("lighting: cell hooks failed (Render3d %d, FXRender3d %d)", g_render3d ? 1 : 0, g_fx_render3d ? 1 : 0);
    return false;
  }
  g_render_texture = reinterpret_cast<RenderTextureFn>(
      InlineHook(*rt, sym::kRenderTextureCopy, reinterpret_cast<void*>(&RenderTextureDetour)));
  return g_render_texture != nullptr;
}

const EngineGlowDraw* GlowDraws(int* n) {
  *n = g_glow_n;
  return g_glow;
}

int GlowDrawsDropped() { return g_glow_dropped; }

void FxSurfaceStats(uint64_t* blits, uint64_t* pure, uint64_t* softened, uint64_t* cells) {
  *blits = g_fx_blits;
  *pure = g_fx_pure;
  *softened = g_fx_soft;
  *cells = g_fx_cells_seen;
}

void SetEmissiveCallbacks(EmitFn emit, CaptureBeginFn begin, CaptureEndFn end) {
  g_emit_fn = emit;
  g_cap_begin = begin;
  g_cap_end = end;
}

void TraceDraws(const char* prefix) {
  size_t n = 0;
  for (; prefix && prefix[n] && n < 8; ++n) g_trace[n] = prefix[n];
  g_trace[n] = 0;
  g_trace_lines = 0;
  Log("lighting: tracing draws of '%s'", g_trace);
}

void GlowStats(uint64_t* rt_calls, uint64_t* rt_world, uint64_t* render3d, uint32_t* flags_or) {
  *rt_calls = g_rt_calls;
  *rt_world = g_rt_world;
  *render3d = g_r3d_calls;
  *flags_or = g_rt_flags_or;
}

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
  for (int i = 0; i < 6; ++i) {
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
  for (int i = 5; i >= 0; --i) {
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

void ArmWorldComposite() {
  g_armed = true;
  g_glow_n = 0;
  g_glow_dropped = 0;
  g_fx_cell = 0;
  g_fx_n = 0;
}

}  // namespace pstf::engine
