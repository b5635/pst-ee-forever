// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/shot.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace pstf {
int g_last_read_fbo = 0;
namespace {
constexpr unsigned kGlViewport = 0x0BA2, kGlFront = 0x0404, kGlReadBuffer = 0x0C02;
constexpr unsigned kGlRgb = 0x1907, kGlUnsignedByte = 0x1401, kGlPackAlignment = 0x0D05;
using GetIntegervFn = void(WINAPI*)(unsigned, int*);
using ReadBufferFn = void(WINAPI*)(unsigned);
using ReadPixelsFn = void(WINAPI*)(int, int, int, int, unsigned, unsigned, void*);
using PixelStoreiFn = void(WINAPI*)(unsigned, int);
using GetCurrentContextFn = HGLRC(WINAPI*)();
}  // namespace

int GameClientWidth() {
  HMODULE gl = GetModuleHandleW(L"opengl32.dll");
  if (!gl) return 0;
  using GetDcFn = HDC(WINAPI*)();
  auto dc = reinterpret_cast<GetDcFn>(GetProcAddress(gl, "wglGetCurrentDC"));
  HWND wnd = dc && dc() ? WindowFromDC(dc()) : nullptr;
  RECT rc{};
  return wnd && GetClientRect(wnd, &rc) ? rc.right - rc.left : 0;
}

std::string SaveScreenshot(const std::string& path) {
  HMODULE gl = GetModuleHandleW(L"opengl32.dll");
  if (!gl) return "opengl32 not loaded";
  auto ctx = reinterpret_cast<GetCurrentContextFn>(GetProcAddress(gl, "wglGetCurrentContext"));
  auto geti = reinterpret_cast<GetIntegervFn>(GetProcAddress(gl, "glGetIntegerv"));
  auto rbuf = reinterpret_cast<ReadBufferFn>(GetProcAddress(gl, "glReadBuffer"));
  auto read = reinterpret_cast<ReadPixelsFn>(GetProcAddress(gl, "glReadPixels"));
  auto store = reinterpret_cast<PixelStoreiFn>(GetProcAddress(gl, "glPixelStorei"));
  if (!ctx || !geti || !rbuf || !read || !store) return "GL entry points missing";
  if (!ctx()) return "no current GL context on this thread";
  int vp[4] = {};
  geti(kGlViewport, vp);
  const int w = vp[2], h = vp[3];
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return "bad viewport";
  // The front buffer belongs to the default framebuffer: read from it even if an FBO is bound for reading
  // (restored after). The binding found is reported (a non-zero one means some pass leaked it).
  using BindFbFn = void(WINAPI*)(unsigned, unsigned);
  using WglGet = PROC(WINAPI*)(LPCSTR);
  auto wgl = reinterpret_cast<WglGet>(GetProcAddress(gl, "wglGetProcAddress"));
  auto bindfb = wgl ? reinterpret_cast<BindFbFn>(wgl("glBindFramebuffer")) : nullptr;
  int read_fbo = 0;
  geti(0x8CAA /*GL_READ_FRAMEBUFFER_BINDING*/, &read_fbo);
  g_last_read_fbo = read_fbo;
  if (bindfb && read_fbo) bindfb(0x8CA8 /*GL_READ_FRAMEBUFFER*/, 0);
  int old_read = 0;
  geti(kGlReadBuffer, &old_read);
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3);
  store(kGlPackAlignment, 1);
  rbuf(kGlFront);
  read(vp[0], vp[1], w, h, kGlRgb, kGlUnsignedByte, px.data());
  rbuf(static_cast<unsigned>(old_read));
  if (bindfb && read_fbo) bindfb(0x8CA8, static_cast<unsigned>(read_fbo));

  // Half size (2x2 box), bottom-up rows as BMP wants (GL rows are bottom-up too).
  const int ow = w / 2, oh = h / 2, stride = (ow * 3 + 3) & ~3;
  std::vector<uint8_t> out(static_cast<size_t>(stride) * oh);
  for (int y = 0; y < oh; ++y)
    for (int x = 0; x < ow; ++x)
      for (int c = 0; c < 3; ++c) {
        auto at = [&](int yy, int xx) { return px[(static_cast<size_t>(yy) * w + xx) * 3 + c]; };
        const int sum = at(2 * y, 2 * x) + at(2 * y, 2 * x + 1) + at(2 * y + 1, 2 * x) + at(2 * y + 1, 2 * x + 1);
        out[static_cast<size_t>(y) * stride + x * 3 + (2 - c)] = static_cast<uint8_t>(sum / 4);  // BGR
      }
  BITMAPFILEHEADER fh = {};
  BITMAPINFOHEADER ih = {};
  ih.biSize = sizeof ih;
  ih.biWidth = ow;
  ih.biHeight = oh;
  ih.biPlanes = 1;
  ih.biBitCount = 24;
  ih.biSizeImage = static_cast<DWORD>(out.size());
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof fh + sizeof ih;
  fh.bfSize = fh.bfOffBits + ih.biSizeImage;
  FILE* f = nullptr;
  if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return "cannot write " + path;
  fwrite(&fh, sizeof fh, 1, f);
  fwrite(&ih, sizeof ih, 1, f);
  fwrite(out.data(), 1, out.size(), f);
  fclose(f);
  return "";
}

}  // namespace pstf
