// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter for the lighting module: our own GL work inside the engine's world pass.
// Evidence for every address: src/engine/symbols.h "lighting". Game thread only.
//
// The composite callback runs once per frame, right before the first of CFloatMessage::Render,
// CInfinity::RenderTransitions, CInfinity::RenderFog or CInfinity::PostRender inside CGameArea::Render:
// after every tile and object, before fog of war, overhead text and markers. The engine's draw batch
// is flushed first and every GL state we may touch is saved and restored around the callback, so the
// engine's own state cache stays true.
#pragma once
#include <cstdint>
#include <string>

namespace pstf::engine {

// GL entry points we use (opengl32 exports + wglGetProcAddress). Loaded on the game thread with the
// engine's context current. Null members = not available.
struct Gl {
  // 1.1
  void(__stdcall* GetIntegerv)(unsigned, int*);
  void(__stdcall* GetFloatv)(unsigned, float*);
  unsigned char(__stdcall* IsEnabled)(unsigned);
  void(__stdcall* Enable)(unsigned);
  void(__stdcall* Disable)(unsigned);
  void(__stdcall* BindTexture)(unsigned, unsigned);
  void(__stdcall* GenTextures)(int, unsigned*);
  void(__stdcall* DeleteTextures)(int, const unsigned*);
  void(__stdcall* TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
  void(__stdcall* TexParameteri)(unsigned, unsigned, int);
  void(__stdcall* CopyTexSubImage2D)(unsigned, int, int, int, int, int, int, int);
  void(__stdcall* Viewport)(int, int, int, int);
  void(__stdcall* BlendFunc)(unsigned, unsigned);
  void(__stdcall* ClearColor)(float, float, float, float);
  void(__stdcall* Clear)(unsigned);
  void(__stdcall* Begin)(unsigned);
  void(__stdcall* End)();
  void(__stdcall* Vertex2f)(float, float);
  void(__stdcall* ReadPixels)(int, int, int, int, unsigned, unsigned, void*);
  void(__stdcall* PixelStorei)(unsigned, int);
  void(__stdcall* ColorMask)(unsigned char, unsigned char, unsigned char, unsigned char);
  const unsigned char*(__stdcall* GetString)(unsigned);
  unsigned(__stdcall* GetError)();
  // 2.0+ / 3.0 (wglGetProcAddress)
  void(__stdcall* ActiveTexture)(unsigned);
  void(__stdcall* UseProgram)(unsigned);
  unsigned(__stdcall* CreateShader)(unsigned);
  void(__stdcall* ShaderSource)(unsigned, int, const char* const*, const int*);
  void(__stdcall* CompileShader)(unsigned);
  void(__stdcall* GetShaderiv)(unsigned, unsigned, int*);
  void(__stdcall* GetShaderInfoLog)(unsigned, int, int*, char*);
  void(__stdcall* DeleteShader)(unsigned);
  unsigned(__stdcall* CreateProgram)();
  void(__stdcall* AttachShader)(unsigned, unsigned);
  void(__stdcall* LinkProgram)(unsigned);
  void(__stdcall* GetProgramiv)(unsigned, unsigned, int*);
  void(__stdcall* GetProgramInfoLog)(unsigned, int, int*, char*);
  int(__stdcall* GetUniformLocation)(unsigned, const char*);
  void(__stdcall* Uniform1i)(int, int);
  void(__stdcall* Uniform1f)(int, float);
  void(__stdcall* Uniform2f)(int, float, float);
  void(__stdcall* Uniform3f)(int, float, float, float);
  void(__stdcall* Uniform4f)(int, float, float, float, float);
  void(__stdcall* GenFramebuffers)(int, unsigned*);
  void(__stdcall* BindFramebuffer)(unsigned, unsigned);
  void(__stdcall* FramebufferTexture2D)(unsigned, unsigned, unsigned, unsigned, int);
  unsigned(__stdcall* CheckFramebufferStatus)(unsigned);
  void(__stdcall* BlendEquationSeparate)(unsigned, unsigned);
  void(__stdcall* BlendFuncSeparate)(unsigned, unsigned, unsigned, unsigned);
};
// Loads once; "" on success, else what is missing. Needs a current GL context.
std::string LoadGl(Gl* gl);

// Every piece of GL state the lighting passes change.
struct GlState {
  int program, active_tex, tex[8], draw_fbo, read_fbo, viewport[4];
  int blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a, blend_eq_rgb, blend_eq_a;
  int unpack_align, pack_align, unpack_row;
  float clear[4];
  int color_mask[4];
  unsigned char blend, scissor, depth, cull, stencil, alpha_test, tex2d, dither;
};
void SaveGlState(const Gl& gl, GlState* s);
void RestoreGlState(const Gl& gl, const GlState& s);

bool GlWorldAvailable();  // manifest bytes behind the reads/calls below match in memory
void EngineFlushBatch();  // DrawFlush (sub_63A370): draws everything the engine has queued
void WorldMatrix(float st[4]);  // uST of the scaled world pass (VPDRAW.GLSL), matrix slot 1
// Program handle of engine shader slot (0 = vpDraw/fpDraw ... 8 = fpSEAM); 0 = shaders off/not made.
unsigned EngineProgram(int slot);
// The current CGameArea's WED resref (8 chars max, upper case) and ARE area type flags.
std::string AreaWed(uint64_t area);
uint16_t AreaTypeFlags(uint64_t area);
// CGameArea::GetTintColor at an area point (the LM tint a creature standing there gets), as 0..1 luma;
// -1 when the symbol does not match. Game thread.
float AreaTintLuma(uint64_t area, int x, int y);

using CompositeFn = void (*)();
// Installs the four call-site hooks (once). The callback runs under SEH; on a fault it is turned off.
bool InstallWorldComposite(CompositeFn fn);
// Arms the callback for the frame being drawn (call from a world pre-draw hook).
void ArmWorldComposite();

// Glow draws (phase 4): every additive BAM v1 draw of the world pass before the composite, in draw
// space (area = draw + WorldPassOffset). The cell fields are set when the draw came through
// CVidCell::Render3d / FXRender3d (not FXBltToBack).
struct EngineGlowDraw {
  int x, y, w, h;  // for an FX-surface cell: that cell's frame rect (anchor - frame centre), else the blit rect
  uint32_t flags;
  char resref[9];  // "" = unknown cell
  uint16_t cycle, frame;
  int anchor_x, anchor_y;  // the cell's draw point (FX-surface cells; creatures: the feet), else bottom centre
  uint8_t from_fx;         // 1 = one cell of an FX-surface blit (rect from the cell's frame header)
  uint8_t pure;            // the blit held only emitting cells (no creature): drawn per pixel, softened
};
bool InstallGlowGather();
// Which world draws emit (module decides from the cell's resref/sequence/frame and the blend flags; the
// resref is "" when unknown). Emitting draws are gathered and, with the capture callbacks, drawn a second
// time with the normal blend into the module's emissive buffer: begin binds it (false = skip), end
// restores the engine's target. Game thread, inside the world pass.
// Returns 0 = nothing, 1 = emissive (gathered + captured), 2 = light only (gathered, not captured);
// | 4 = soften: a normal-alpha draw of this effect is drawn with the screen blend (black = see-through).
// FX-surface blits (CInfinity::FXRender cells then one FXBltToBack) are judged per cell: when every cell
// in the surface emits, the blit is an effect only and is captured per pixel (and softened); otherwise
// (a creature with attached effects) only the emitting cells are gathered, each with its own frame rect.
using EmitFn = int (*)(const char* resref, uint16_t cycle, uint16_t frame, uint32_t flags);
using CaptureBeginFn = bool (*)();
using CaptureEndFn = void (*)();
void SetEmissiveCallbacks(EmitFn emit, CaptureBeginFn begin, CaptureEndFn end);
// Draws gathered since the last ArmWorldComposite (cleared there). Game thread only.
const EngineGlowDraw* GlowDraws(int* n);
int GlowDrawsDropped();  // over the per-frame cap (256)
// Sprite buffer (lighting phase 2): creature draws of the world pass. A creature is one FX-surface blit
// with one cell the emit callback rejects and `want(resref)` accepts (a BAM with a depth file). Each is
// recorded (draw space) and its blit is drawn a second time with the normal blend between cov_begin and
// cov_end (the module's coverage buffer: alpha = the creature's pixels after wall clipping).
struct EngineSpriteDraw {
  char resref[9];
  uint16_t cycle, frame;
  int anchor_x, anchor_y;  // the feet (FXRender3d draw point), draw space
  int x, y, w, h;          // the frame rect from the cell's header (w = 0: header not loaded), draw space
  uint8_t mirror;          // blit flag 0x10 (FXBltToBack: x flipped inside the surface)
};
using SpriteWantFn = bool (*)(const char* resref);
void SetSpriteCallbacks(SpriteWantFn want, CaptureBeginFn cov_begin, CaptureEndFn cov_end);
const EngineSpriteDraw* SpriteDraws(int* n);  // since the last ArmWorldComposite
// Pure: the feet and frame rect of an FX-surface cell drawn at (cell_x, cell_y) inside a surface whose
// source rect is [f0, f2) and that is blitted at x (FXBltToBack: x = pos - ref; flag 0x10 mirrors).
inline void FxCellRect(int x, int f0, int f2, int cell_x, int w, int cx, bool mirror, int* anchor_x, int* left) {
  if (!mirror) {
    *anchor_x = x + cell_x;
    *left = *anchor_x - cx;
  } else {  // FXBltToBack flips the surface rect: surface column u lands at x + f0 + f2 - u
    *anchor_x = x + f0 + f2 - cell_x;
    *left = *anchor_x - (w - cx);
  }
}
// Probe: FX-surface blits this session (all / pure effect / softened draws) and cells seen in them.
void FxSurfaceStats(uint64_t* blits, uint64_t* pure, uint64_t* softened, uint64_t* cells);
// Probe: RenderTexture calls (all / in the armed world pass), Render3d calls, OR of world-pass flags.
// Probe: log each distinct draw (Render3d '3', FXRender3d 'X', RenderTexture with cell 'R' / from the FX
// surface 'F') of cells whose resref starts with `prefix` ("" = off).
void TraceDraws(const char* prefix);
void GlowStats(uint64_t* rt_calls, uint64_t* rt_world, uint64_t* render3d, uint32_t* flags_or);

}  // namespace pstf::engine
