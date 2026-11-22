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

// PSTF_SmoothLockFrames(): frames the lock camera followed the shown position.
int __cdecl LuaLockFrames(engine::LuaState* L) {
  engine::LuaPushNumber(L, static_cast<double>(engine::InterpLockFrames()));
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

// PSTF_SmoothTrace(id, frames): trace rows in the log ("smooth trace: ...").
int __cdecl LuaTrace(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const int n = static_cast<int>(engine::LuaNumberArg(L, 2, 120));
  engine::InterpTrace(id, n);
  Log("lua: PSTF_SmoothTrace(%X, %d)", static_cast<unsigned>(id), n);
  return 0;
}
// PSTF_SmoothTraceResult() -> done, frames, walk frames, standing-still frames, wobble px, longest hold ms,
// EE tick ms (exact).
int __cdecl LuaTraceResult(engine::LuaState* L) {
  const engine::TraceResult r = engine::InterpTraceResult();
  if (r.done)  // polled every tick by tests: log only the answer
    Log("lua: PSTF_SmoothTraceResult -> frames %d walk %d still %d wobble %d hold %d ms", r.frames, r.walk,
        r.still, r.wobble, r.hold_ms);
  engine::LuaPushNumber(L, r.done ? 1 : 0);
  engine::LuaPushNumber(L, r.frames);
  engine::LuaPushNumber(L, r.walk);
  engine::LuaPushNumber(L, r.still);
  engine::LuaPushNumber(L, r.wobble);
  engine::LuaPushNumber(L, r.hold_ms);
  engine::LuaPushNumber(L, 1000.0 / engine::InterpEngineFps());
  return 7;
}
// PSTF_LockCamera(id): the engine's own scroll lock on creature id (-1 = off) -> 1 when set.
int __cdecl LuaLock(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const bool ok = engine::LockCamera(id);
  Log("lua: PSTF_LockCamera(%X) -> %d", static_cast<unsigned>(id), ok ? 1 : 0);
  engine::LuaPushNumber(L, ok ? 1 : 0);
  return 1;
}

}  // namespace

void InstallSmooth(const IniMap& ini) {
  if (!IniBool(ini, "smooth.interpolation", false)) {
    Log("smooth: interpolation disabled in ini");
    return;
  }
  // 0 = the display's refresh rate; otherwise a fixed rate (the adapter keeps it >= the tick rate).
  int fps = IniInt(ini, "smooth.frame_rate", 0);
  if (fps < 0) fps = 0;
  if (fps > 240) fps = 240;
  if (!engine::InstallFrameInterpolation(static_cast<uint32_t>(fps), IniBool(ini, "smooth.lock_camera", true)))
    return;
  dev::AddLuaFunction("PSTF_SmoothState", &LuaState);
  dev::AddLuaFunction("PSTF_SmoothFrames", &LuaFrames);
  dev::AddLuaFunction("PSTF_SmoothTicks", &LuaTicks);
  dev::AddLuaFunction("PSTF_SmoothTickMs", &LuaTickMs);
  dev::AddLuaFunction("PSTF_SmoothFps", &LuaEngineFps);
  dev::AddLuaFunction("PSTF_SmoothTrace", &LuaTrace);
  dev::AddLuaFunction("PSTF_SmoothTraceResult", &LuaTraceResult);
  dev::AddLuaFunction("PSTF_SmoothLockFrames", &LuaLockFrames);
  dev::AddLuaFunction("PSTF_LockCamera", &LuaLock);
}

}  // namespace pstf::smooth
