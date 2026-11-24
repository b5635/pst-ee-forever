// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/soft_draw.h"

#include <cmath>
#include <string>

#include "core/log.h"
#include "engine/gl_world.h"
#include "engine/render.h"
#include "modules/lighting/lighting_logic.h"

namespace pstf::engine {
namespace {

constexpr unsigned kQuads = 0x0007, kBlend = 0x0BE2, kVertexShader = 0x8B31, kFragmentShader = 0x8B30,
                   kCompileStatus = 0x8B81, kLinkStatus = 0x8B82, kSrcAlpha = 0x0302, kOneMinusSrcAlpha = 0x0303,
                   kOne = 1, kFuncAdd = 0x8006, kScissor = 0x0C11, kDepth = 0x0B71, kCull = 0x0B44,
                   kStencil = 0x0B90, kAlphaTest = 0x0BC0;

Gl g_gl = {};
bool g_loaded = false, g_off = false;
unsigned g_prog = 0;
int g_u_target = -1;

const char* kVs = R"(#version 130
uniform vec2 u_target;
out float v_t;
out vec4 v_col;
void main() {
  gl_Position = vec4(gl_Vertex.xy / u_target * 2.0 - 1.0, 0.0, 1.0);
  v_t = gl_MultiTexCoord0.x;
  v_col = gl_Color;
}
)";
// Across the strip t runs -1..1: a smooth bump, no hard edge.
const char* kFs = R"(#version 130
in float v_t;
in vec4 v_col;
void main() {
  float a = 1.0 - v_t * v_t;
  gl_FragColor = vec4(v_col.rgb, v_col.a * a * a);
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
    Log("soft_draw: shader failed: %s", buf);
    return 0;
  }
  return s;
}

bool SetUp() {
  if (g_prog) return true;
  if (g_off) return false;
  g_off = true;
  if (!g_loaded) {
    const std::string err = LoadGl(&g_gl);
    if (!err.empty()) {
      Log("soft_draw: off, GL %s", err.c_str());
      return false;
    }
    g_loaded = true;
  }
  if (!EngineProgram(0)) return false;  // the engine runs without shaders
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
    Log("soft_draw: program link failed");
    return false;
  }
  g_prog = p;
  g_u_target = g_gl.GetUniformLocation(p, "u_target");
  g_off = false;
  return true;
}

}  // namespace

bool DrawSoftSegments(const SoftSeg* s, int n, bool add) {
  if (n <= 0) return true;
  if (!WorldPassArea()) return false;
  EngineFlushBatch();
  if (!SetUp()) return false;
  auto& gl = g_gl;
  GlState saved;
  SaveGlState(gl, &saved);
  const int vw = saved.viewport[2], vh = saved.viewport[3];
  float st[4], ox, oy;
  WorldMatrix(st);
  WorldPassOffset(&ox, &oy);
  if (vw <= 0 || vh <= 0 || st[0] == 0 || st[1] == 0) {
    RestoreGlState(gl, saved);
    return false;
  }
  const lighting::ViewMap vm = lighting::MakeViewMap(vw, vh, st, ox, oy);
  const float px_per_area = 1.0f / std::fabs(vm.ax);
  for (unsigned cap : {kScissor, kDepth, kCull, kStencil, kAlphaTest}) gl.Disable(cap);
  gl.ColorMask(1, 1, 1, 0);
  gl.UseProgram(g_prog);
  gl.Uniform2f(g_u_target, float(vw), float(vh));
  gl.Enable(kBlend);
  gl.BlendEquationSeparate(kFuncAdd, kFuncAdd);
  if (add) gl.BlendFuncSeparate(kSrcAlpha, kOne, kSrcAlpha, kOne);
  else gl.BlendFuncSeparate(kSrcAlpha, kOneMinusSrcAlpha, kSrcAlpha, kOneMinusSrcAlpha);
  gl.Begin(kQuads);
  for (int i = 0; i < n; ++i) {
    const SoftSeg& q = s[i];
    if (q.alpha <= 0.003f) continue;
    float ax, ay, bx, by;
    lighting::ToPhys(vm, q.x1, q.y1, &ax, &ay);
    lighting::ToPhys(vm, q.x2, q.y2, &bx, &by);
    float dx = bx - ax, dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) continue;
    const float hw = q.half_width * px_per_area;
    const float nx = -dy / len * hw, ny = dx / len * hw;
    // the strip runs a little past both ends so joined segments overlap without a gap
    const float ex = dx / len * hw * 0.5f, ey = dy / len * hw * 0.5f;
    gl.Color4f(float(q.bgr & 0xFF) / 255.0f, float((q.bgr >> 8) & 0xFF) / 255.0f, float((q.bgr >> 16) & 0xFF) / 255.0f,
               q.alpha);
    gl.TexCoord2f(-1, 0);
    gl.Vertex2f(ax - ex + nx, ay - ey + ny);
    gl.TexCoord2f(-1, 0);
    gl.Vertex2f(bx + ex + nx, by + ey + ny);
    gl.TexCoord2f(1, 0);
    gl.Vertex2f(bx + ex - nx, by + ey - ny);
    gl.TexCoord2f(1, 0);
    gl.Vertex2f(ax - ex - nx, ay - ey - ny);
  }
  gl.End();
  gl.Color4f(1, 1, 1, 1);
  RestoreGlState(gl, saved);
  return true;
}

}  // namespace pstf::engine
