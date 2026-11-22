// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/smooth/smooth_module.h"

#include <cstdio>

#include "core/ini.h"
#include "core/log.h"
#include "engine/frame.h"
#include "engine/lua.h"
#include "modules/dev/lua_bridge.h"

namespace pstf::smooth {
namespace {

// PSTF_SmoothFrames() / PSTF_SmoothTicks() / PSTF_SmoothTickMs() / PSTF_SmoothFps(): counters since
// install, EE's tick interval and EE's fps global. The in-game test compares frames with ticks.
int __cdecl LuaFrames(engine::LuaState* L) {
  engine::LuaPushNumber(L, static_cast<double>(engine::InterpFrames()));
  return 1;
}
int __cdecl LuaTicks(engine::LuaState* L) {
  engine::LuaPushNumber(L, static_cast<double>(engine::InterpTicks()));
  return 1;
}
int __cdecl LuaTickMs(engine::LuaState* L) {
  engine::LuaPushNumber(L, engine::InterpTickMs());
  return 1;
}
int __cdecl LuaEngineFps(engine::LuaState* L) {
  engine::LuaPushNumber(L, engine::InterpEngineFps());
  return 1;
}

// PSTF_SmoothState() -> "active=1 frames=.. ticks=.. tick_ms=.. fps=.. alpha=0.34".
int __cdecl LuaState(engine::LuaState* L) {
  char b[160];
  std::snprintf(b, sizeof b, "active=%d frames=%llu ticks=%llu tick_ms=%u fps=%d alpha=%.2f",
                engine::InterpActive() ? 1 : 0, static_cast<unsigned long long>(engine::InterpFrames()),
                static_cast<unsigned long long>(engine::InterpTicks()), engine::InterpTickMs(),
                engine::InterpEngineFps(), engine::InterpAlpha());
  Log("lua: PSTF_SmoothState -> %s", b);
  engine::LuaPushString(L, b);
  return 1;
}

}  // namespace

void InstallSmooth(const IniMap& ini) {
  if (!IniBool(ini, "smooth.interpolation", false)) {
    Log("smooth: interpolation disabled in ini");
    return;
  }
  uint32_t fps = static_cast<uint32_t>(IniInt(ini, "smooth.frame_rate", 60));
  if (fps < 30) fps = 30;
  if (fps > 240) fps = 240;
  if (!engine::InstallFrameInterpolation(fps)) return;
  dev::AddLuaFunction("PSTF_SmoothState", &LuaState);
  dev::AddLuaFunction("PSTF_SmoothFrames", &LuaFrames);
  dev::AddLuaFunction("PSTF_SmoothTicks", &LuaTicks);
  dev::AddLuaFunction("PSTF_SmoothTickMs", &LuaTickMs);
  dev::AddLuaFunction("PSTF_SmoothFps", &LuaEngineFps);
}

}  // namespace pstf::smooth
