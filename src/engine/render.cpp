// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/render.h"

#include "core/crashlog.h"
#include "core/log.h"
#include "core/patch.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {

template <class T>
T R(uint64_t va) {
  return *reinterpret_cast<const volatile T*>(va);
}

using AreaRenderFn = void(__fastcall*)(void* area, void* vid);
using DrawLine2dFn = void(__fastcall*)(void* vid, int x1, int y1, int x2, int y2, const int* clip, uint32_t color,
                                      float half_width, float start_trim, float end_trim, float dash, float gap,
                                      bool textured);
AreaRenderFn g_render = nullptr;
WorldDrawFn g_fn = nullptr;
bool g_faulted = false;

// Valid while the callback runs.
void* g_vid = nullptr;
uint64_t g_area = 0;

void CallFn(uint64_t) { g_fn(); }

void __fastcall AreaRender(void* area, void* vid) {
  g_render(area, vid);
  if (!g_fn || g_faulted) return;
  g_vid = vid;
  g_area = reinterpret_cast<uint64_t>(area);
  uint32_t code = 0;
  if (!GuardedCall(&CallFn, 0, &code)) {
    g_faulted = true;
    Log("render: world overlay faulted (%08X); overlay off for this session", code);
  }
  g_vid = nullptr;
  g_area = 0;
}

}  // namespace

bool InstallWorldDraw(WorldDrawFn fn) {
  if (g_render) {
    g_fn = fn;
    return true;
  }
  const sym::Symbol* site = nullptr;
  int ok = 0;
  for (const auto& s : sym::kAll) {
    if (s.rva == sym::kRvaAreaRenderCall) site = &s;
    if (s.rva == sym::kRvaDrawLine2d || s.rva == sym::kRvaRenderView || s.rva == sym::kRvaRenderActionsLine)
      ok += CheckSymbol(s) ? 1 : 0;
  }
  if (!site || ok != 3) return false;
  const uint64_t original = RedirectCall(*site, reinterpret_cast<void*>(&AreaRender));
  if (original != sym::kVaAreaRender) {
    Log("render: RenderZoomed call not patched (target %llX)", static_cast<unsigned long long>(original));
    return false;
  }
  g_render = reinterpret_cast<AreaRenderFn>(original);
  g_fn = fn;
  Log("render: world overlay installed at %llX", static_cast<unsigned long long>(sym::kImageBase + site->rva));
  return true;
}

void DrawWorldLine(int x1, int y1, int x2, int y2, uint32_t color, float half_width) {
  if (!g_vid || !g_area) return;
  const int* rect = reinterpret_cast<const int*>(g_area + sym::kOffAreaRect);  // left, top, right, bottom
  const int ox = R<int32_t>(g_area + sym::kOffAreaRenderView) - rect[0];
  const int oy = R<int32_t>(g_area + sym::kOffAreaRenderView + 4) - rect[1];
  reinterpret_cast<DrawLine2dFn>(Va(sym::kRvaDrawLine2d))(g_vid, x1 - ox, y1 - oy, x2 - ox, y2 - oy, rect, color,
                                                          half_width, 0.f, 0.f, -1.f, 0.f, false);
}

}  // namespace pstf::engine
