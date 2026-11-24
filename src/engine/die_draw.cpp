// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/die_draw.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "core/inflate.h"
#include "core/log.h"
#include "engine/gl_world.h"

namespace pstf::engine {
namespace {

using namespace rolls::die3d;

constexpr unsigned kTriangles = 0x0004, kBlend = 0x0BE2, kVertexShader = 0x8B31, kFragmentShader = 0x8B30,
                   kCompileStatus = 0x8B81, kLinkStatus = 0x8B82, kSrcAlpha = 0x0302, kOneMinusSrcAlpha = 0x0303,
                   kFuncAdd = 0x8006, kScissor = 0x0C11, kDepth = 0x0B71, kCull = 0x0B44, kStencil = 0x0B90,
                   kAlphaTest = 0x0BC0, kTexture2D = 0x0DE1, kTexture0 = 0x84C0, kRgba = 0x1908,
                   kUnsignedByte = 0x1401, kMinFilter = 0x2801, kMagFilter = 0x2800, kLinear = 0x2601,
                   kLinearMipLinear = 0x2703, kWrapS = 0x2802, kWrapT = 0x2803, kClampToEdge = 0x812F,
                   kUnpackAlignment = 0x0CF5, kUnpackRowLength = 0x0CF2;

Gl g_gl = {};
void(__stdcall* g_gen_mipmap)(unsigned) = nullptr;
bool g_tried = false, g_ready = false;
unsigned g_prog = 0, g_tex = 0;
int u_tex = -1, u_e0 = -1, u_e1 = -1, u_e2 = -1, u_face = -1, u_w = -1, u_target = -1;
int g_atlas_w = 0, g_atlas_h = 0;
std::wstring g_atlas_path;
int g_drawn = 0, g_failed = 0;
bool g_probe_armed = false, g_probe_done = false;
uint8_t g_probe_rgb[3] = {};
std::string g_why = "not drawn yet";

// Vertices in viewport px (y down); every edge is a signed distance in window px (gl_FragCoord), > 0 inside.
const char* kVs = R"(#version 130
uniform vec2 u_target;
out vec2 v_uv;
void main() {
  gl_Position = vec4(gl_Vertex.x / u_target.x * 2.0 - 1.0, 1.0 - gl_Vertex.y / u_target.y * 2.0, 0.0, 1.0);
  v_uv = gl_MultiTexCoord0.xy;
}
)";
const char* kFs = R"(#version 130
uniform sampler2D u_tex;
uniform vec3 u_e0, u_e1, u_e2;  // edge lines: dot(n, p) + c, n unit and inward
uniform vec4 u_face;            // lit face colour, number shade
uniform float u_w;              // edge line half width, px
in vec2 v_uv;
void main() {
  vec2 p = gl_FragCoord.xy;
  float d0 = dot(u_e0.xy, p) + u_e0.z, d1 = dot(u_e1.xy, p) + u_e1.z, d2 = dot(u_e2.xy, p) + u_e2.z;
  float inside = clamp(0.5 + min(d0, min(d1, d2)), 0.0, 1.0);
  // each face paints its own half of an edge line, inside itself: nothing outside the die's outline
  float line = max(clamp(u_w + 0.5 - d0, 0.0, 1.0), max(clamp(u_w + 0.5 - d1, 0.0, 1.0), clamp(u_w + 0.5 - d2, 0.0, 1.0)));
  vec2 t = texture(u_tex, v_uv).rg;
  vec3 txt = mix(vec3(8.0, 30.0, 10.0) / 255.0, vec3(226.0, 255.0, 214.0) / 255.0, t.r);
  float ta = t.r + t.g * (1.0 - t.r);
  vec3 col = mix(u_face.rgb, txt, ta * u_face.a * inside);
  col = mix(col, vec3(120.0, 236.0, 112.0) / 255.0, line);
  gl_FragColor = vec4(col, inside);
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
    Log("die3d: shader failed: %s", buf);
    g_gl.DeleteShader(s);
    return 0;
  }
  return s;
}

bool LoadAtlas(std::vector<uint8_t>* rgba) {
  FILE* f = _wfopen(g_atlas_path.c_str(), L"rb");
  if (!f) {
    g_why = "atlas missing";
    return false;
  }
  std::vector<uint8_t> in;
  uint8_t buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) in.insert(in.end(), buf, buf + n);
  fclose(f);
  if (in.size() < 12 || in[0] != 'P' || in[1] != 'F' || in[2] != 'D' || in[3] != 'N') {
    g_why = "atlas header";
    return false;
  }
  auto u16 = [&](int o) { return int(in[o] | (in[o + 1] << 8)); };
  g_atlas_w = u16(4), g_atlas_h = u16(6);
  if (u16(8) != kCell || u16(10) != kCols || g_atlas_w != kCell * kCols || g_atlas_h != kCell * kRows) {
    g_why = "atlas layout differs from die3d.h";
    return false;
  }
  std::vector<uint8_t> plain;
  const size_t want = size_t(g_atlas_w) * g_atlas_h * 2;
  if (!ZlibInflate(in.data() + 12, in.size() - 12, &plain, want) || plain.size() != want) {
    g_why = "atlas data";
    return false;
  }
  rgba->resize(want * 2);
  for (size_t i = 0; i < want / 2; ++i) {
    (*rgba)[i * 4] = plain[i * 2];
    (*rgba)[i * 4 + 1] = plain[i * 2 + 1];
    (*rgba)[i * 4 + 2] = 0;
    (*rgba)[i * 4 + 3] = 255;
  }
  return true;
}

bool SetUp() {
  if (g_ready) return true;
  if (g_tried) return false;
  g_tried = true;
  if (!GlWorldAvailable()) {
    g_why = "GL symbols differ";
    return false;
  }
  const std::string err = LoadGl(&g_gl);
  if (!err.empty()) {
    g_why = "GL " + err;
    return false;
  }
  if (!EngineProgram(0)) {
    g_why = "engine runs without shaders";
    return false;
  }
  if (HMODULE gl = GetModuleHandleW(L"opengl32.dll"))
    if (auto wgl = reinterpret_cast<PROC(WINAPI*)(LPCSTR)>(GetProcAddress(gl, "wglGetProcAddress")))
      g_gen_mipmap = reinterpret_cast<void(__stdcall*)(unsigned)>(wgl("glGenerateMipmap"));
  std::vector<uint8_t> rgba;
  if (!LoadAtlas(&rgba)) return false;
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
    return false;
  }
  g_prog = p;
  u_tex = g_gl.GetUniformLocation(p, "u_tex");
  u_e0 = g_gl.GetUniformLocation(p, "u_e0");
  u_e1 = g_gl.GetUniformLocation(p, "u_e1");
  u_e2 = g_gl.GetUniformLocation(p, "u_e2");
  u_face = g_gl.GetUniformLocation(p, "u_face");
  u_w = g_gl.GetUniformLocation(p, "u_w");
  u_target = g_gl.GetUniformLocation(p, "u_target");
  GlState saved;
  SaveGlState(g_gl, &saved);
  g_gl.ActiveTexture(kTexture0);
  g_gl.GenTextures(1, &g_tex);
  g_gl.BindTexture(kTexture2D, g_tex);
  g_gl.PixelStorei(kUnpackAlignment, 4);
  g_gl.PixelStorei(kUnpackRowLength, 0);
  g_gl.TexImage2D(kTexture2D, 0, kRgba, g_atlas_w, g_atlas_h, 0, kRgba, kUnsignedByte, rgba.data());
  g_gl.TexParameteri(kTexture2D, kWrapS, kClampToEdge);
  g_gl.TexParameteri(kTexture2D, kWrapT, kClampToEdge);
  g_gl.TexParameteri(kTexture2D, kMagFilter, kLinear);
  if (g_gen_mipmap) {
    g_gen_mipmap(kTexture2D);
    g_gl.TexParameteri(kTexture2D, kMinFilter, kLinearMipLinear);
  } else {
    g_gl.TexParameteri(kTexture2D, kMinFilter, kLinear);
  }
  RestoreGlState(g_gl, saved);
  g_ready = true;
  Log("die3d: GL die ready (atlas %dx%d, mipmaps %s)", g_atlas_w, g_atlas_h, g_gen_mipmap ? "on" : "off");
  return true;
}

}  // namespace

void SetDieAtlasPath(const std::wstring& path) { g_atlas_path = path; }

void ArmDieProbe() {
  g_probe_armed = true;
  g_probe_done = false;
}
bool DieProbePixel(uint8_t rgb[3]) {
  if (!g_probe_done) return false;
  rgb[0] = g_probe_rgb[0], rgb[1] = g_probe_rgb[1], rgb[2] = g_probe_rgb[2];
  return true;
}

void DieDrawStats(int* drawn, int* failed, std::string* why) {
  *drawn = g_drawn;
  *failed = g_failed;
  *why = g_why;
}

bool DrawDie(const Frame& fr, float cx, float cy, float box, float ui_w, float ui_h) {
  if (!SetUp() || ui_w <= 0 || ui_h <= 0) {
    if (g_failed++ == 0) Log("die3d: not drawn (%s): the BAM die is shown", g_why.c_str());
    return false;
  }
  EngineFlushBatch();  // what the menu drew so far goes under the die
  auto& gl = g_gl;
  GlState saved;
  SaveGlState(gl, &saved);
  const int vx = saved.viewport[0], vy = saved.viewport[1], vw = saved.viewport[2], vh = saved.viewport[3];
  if (vw <= 0 || vh <= 0) {
    RestoreGlState(gl, saved);
    ++g_failed;
    g_why = "no viewport";
    return false;
  }
  const double sx = vw / ui_w, sy = vh / ui_h;  // UI -> viewport px
  const double px_scale = 0.5 * (sx + sy);
  const double ccx = cx * sx, ccy = cy * sy;
  const double radius = 52.0 / 150.0 * box * px_scale * fr.scale;
  const double half_w = 1.0 * box / 150.0 * px_scale;

  for (unsigned cap : {kScissor, kDepth, kCull, kStencil, kAlphaTest}) gl.Disable(cap);
  gl.ColorMask(1, 1, 1, 0);
  gl.UseProgram(g_prog);
  gl.Uniform2f(u_target, float(vw), float(vh));
  gl.Uniform1i(u_tex, 0);
  gl.Uniform1f(u_w, float(half_w));
  gl.ActiveTexture(kTexture0);
  gl.BindTexture(kTexture2D, g_tex);
  gl.Enable(kBlend);
  gl.BlendEquationSeparate(kFuncAdd, kFuncAdd);
  gl.BlendFuncSeparate(kSrcAlpha, kOneMinusSrcAlpha, kSrcAlpha, kOneMinusSrcAlpha);

  const Mesh& m = D20();
  V3 view[12];
  double scr[12][2];
  for (int i = 0; i < 12; ++i) {
    view[i] = Mul(fr.pose, m.v[i]);
    scr[i][0] = ccx + view[i].x * radius;
    scr[i][1] = ccy - view[i].y * radius;
  }
  int order[20];
  double depth[20];
  for (int i = 0; i < 20; ++i) {
    order[i] = i;
    depth[i] = view[m.f[i][0]].z + view[m.f[i][1]].z + view[m.f[i][2]].z;
  }
  for (int a = 1; a < 20; ++a)
    for (int b = a; b > 0 && depth[order[b]] < depth[order[b - 1]]; --b) std::swap(order[b], order[b - 1]);
  const V3 light = Norm(V3{-0.45, 0.6, 0.66});
  const double T = kCell, AW = g_atlas_w, AH = g_atlas_h;
  for (int oi = 0; oi < 20; ++oi) {
    const int f = order[oi];
    const int a = m.f[f][0], b = m.f[f][1], c = m.f[f][2];
    const V3 nrm = Norm(Cross(view[b] - view[a], view[c] - view[a]));
    if (nrm.z <= 0.02) continue;
    const double lam = std::fmax(0.0, Dot(nrm, light)), spec = std::pow(lam, 18);
    float col[3];
    const double base[3] = {22, 92, 34}, hi[3] = {150, 255, 150};
    for (int k = 0; k < 3; ++k)
      col[k] = float(std::fmin(255.0, base[k] * (0.45 + 0.75 * lam) + hi[k] * spec * 0.55) / 255.0);
    gl.Uniform4f(u_face, col[0], col[1], col[2], float(0.55 + 0.45 * nrm.z));
    // apex, base right, base left of the number cell (keep the text unmirrored, as the BAM does)
    int vtx[3] = {a, b, c};
    const double area = (scr[b][0] - scr[a][0]) * (scr[c][1] - scr[a][1]) - (scr[b][1] - scr[a][1]) * (scr[c][0] - scr[a][0]);
    if (area <= 0) std::swap(vtx[1], vtx[2]);
    const bool front = f == fr.front_face;
    const int cell = AtlasCell(front ? fr.front_text : m.num[f], front);
    const double ox = (cell % kCols) * T, oy = (cell / kCols) * T;
    const double uv[3][2] = {{(ox + T / 2) / AW, oy / AH}, {(ox + T) / AW, (oy + T * 0.866) / AH}, {ox / AW, (oy + T * 0.866) / AH}};
    // edge lines in window px (gl_FragCoord: origin bottom left of the window)
    double wx[3], wy[3];
    for (int k = 0; k < 3; ++k) {
      wx[k] = vx + scr[vtx[k]][0];
      wy[k] = vy + (vh - scr[vtx[k]][1]);
    }
    const double gx = (wx[0] + wx[1] + wx[2]) / 3, gy = (wy[0] + wy[1] + wy[2]) / 3;
    double e[3][3];
    double inr = 1e9;
    for (int k = 0; k < 3; ++k) {
      const int p = k, q = (k + 1) % 3;
      double nx = -(wy[q] - wy[p]), ny = wx[q] - wx[p];
      const double len = std::sqrt(nx * nx + ny * ny);
      if (len < 1e-6) nx = 0, ny = 0;
      else nx /= len, ny /= len;
      double cc = -(nx * wx[p] + ny * wy[p]);
      if (nx * gx + ny * gy + cc < 0) nx = -nx, ny = -ny, cc = -cc;
      e[k][0] = nx, e[k][1] = ny, e[k][2] = cc;
      inr = std::fmin(inr, nx * gx + ny * gy + cc);
    }
    if (inr < 0.05) continue;  // edge-on sliver
    gl.Uniform3f(u_e0, float(e[0][0]), float(e[0][1]), float(e[0][2]));
    gl.Uniform3f(u_e1, float(e[1][0]), float(e[1][1]), float(e[1][2]));
    gl.Uniform3f(u_e2, float(e[2][0]), float(e[2][1]), float(e[2][2]));
    // grow the triangle about its centroid by the 1 px of edge smoothing
    const double k = std::fmin(6.0, (inr + 1.5) / inr);
    const double sgx = (scr[vtx[0]][0] + scr[vtx[1]][0] + scr[vtx[2]][0]) / 3;
    const double sgy = (scr[vtx[0]][1] + scr[vtx[1]][1] + scr[vtx[2]][1]) / 3;
    const double cu = (uv[0][0] + uv[1][0] + uv[2][0]) / 3, cv = (uv[0][1] + uv[1][1] + uv[2][1]) / 3;
    gl.Begin(kTriangles);
    for (int q = 0; q < 3; ++q) {
      gl.TexCoord2f(float(cu + (uv[q][0] - cu) * k), float(cv + (uv[q][1] - cv) * k));
      gl.Vertex2f(float(sgx + (scr[vtx[q]][0] - sgx) * k), float(sgy + (scr[vtx[q]][1] - sgy) * k));
    }
    gl.End();
  }
  gl.Color4f(1, 1, 1, 1);
  if (g_probe_armed) {  // the die's centre pixel, read back right after the draw
    uint8_t px[4] = {};
    gl.PixelStorei(0x0D05, 1);  // GL_PACK_ALIGNMENT
    gl.ReadPixels(int(vx + ccx), int(vy + vh - ccy), 1, 1, kRgba, kUnsignedByte, px);
    g_probe_rgb[0] = px[0], g_probe_rgb[1] = px[1], g_probe_rgb[2] = px[2];
    g_probe_armed = false;
    g_probe_done = true;
  }
  RestoreGlState(gl, saved);
  ++g_drawn;
  g_why = "ok";
  return true;
}

}  // namespace pstf::engine
