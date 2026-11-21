// SPDX-License-Identifier: GPL-2.0-or-later
// Dev autoload: load a save without input and run a test Lua file once the world is up.
// Turned on per launch by tools\launch.ps1 through environment variables, or in the ini:
//   PSTF_AUTOLOAD=1 / [dev] autoload=1   press "Continue" on the start screen (the save named in
//                                         Baldur.lua 'Game Options','Last Save PST')
//   PSTF_RUN_LUA=<file> / [dev] autorun=  run this Lua file in the engine state when the world is ready
// Every step is logged ("dev: autoload: ..."), so a script can wait on the log.
#include "modules/dev/autoload.h"

#include <cstdlib>
#include <string>

#include "core/log.h"
#include "core/tick.h"
#include "engine/lua.h"
#include "engine/screens.h"
#include "engine/world.h"
#include "modules/dev/autoload_logic.h"

namespace pstf::dev {
namespace {
Autoload g_auto;
std::string g_script;
engine::Screen g_last = engine::Screen::None;

Scr Map(engine::Screen s) {
  switch (s) {
    case engine::Screen::Start: return Scr::Start;
    case engine::Screen::Load: return Scr::Load;
    case engine::Screen::World: return Scr::World;
    case engine::Screen::None: return Scr::None;
    default: return Scr::Other;
  }
}

void OnTick(uint64_t frame) {
  const engine::Screen s = engine::ActiveScreen();
  if (s != g_last) {
    Log("dev: screen %s -> %s (frame %llu)", engine::ScreenName(g_last), engine::ScreenName(s),
        static_cast<unsigned long long>(frame));
    g_last = s;
  }
  switch (g_auto.Step(Map(s))) {
    case Act::Continue:
      if (engine::ContinueGame()) {
        Log("dev: autoload: Continue pressed (frame %llu)", static_cast<unsigned long long>(frame));
      } else {
        Log("dev: autoload: FAILED, no 'Last Save PST' save to continue");
        g_auto.ContinueFailed();
      }
      break;
    case Act::RunScript: {
      uint32_t t = 0;
      bool running = false;
      engine::WorldTime(&t, &running);
      Log("dev: autoload: world ready (frame %llu, world time %u, running %d)",
          static_cast<unsigned long long>(frame), t, running ? 1 : 0);
      if (!g_script.empty()) {
        const std::string err = engine::LuaDoFile(g_script.c_str());
        Log("dev: autorun %s: %s", g_script.c_str(), err.empty() ? "ok" : err.c_str());
      }
      Log("dev: autoload: done");
      break;
    }
    case Act::Fail:
      Log("dev: autoload: FAILED, timeout (screen %s)", engine::ScreenName(s));
      break;
    default:
      break;
  }
}

std::string Env(const char* name) {
  char* v = nullptr;
  size_t n = 0;
  std::string out;
  if (_dupenv_s(&v, &n, name) == 0 && v) out = v;
  free(v);
  return out;
}
}  // namespace

bool InstallAutoload(const IniMap& ini) {
  const std::string env = Env("PSTF_AUTOLOAD");
  const bool on = env.empty() ? IniBool(ini, "dev.autoload", false) : (env == "1");
  if (!on) {
    Log("dev: autoload off");
    return false;
  }
  g_script = Env("PSTF_RUN_LUA");
  if (g_script.empty()) {
    auto it = ini.find("dev.autorun");
    if (it != ini.end()) g_script = it->second;
  }
  if (!engine::ScreensAvailable() || !engine::WorldApiAvailable()) {
    Log("dev: autoload: screen/world functions differ from the manifest; off");
    return false;
  }
  if (!AddTickHandler(&OnTick)) return false;
  Log("dev: autoload on (script: %s)", g_script.empty() ? "none" : g_script.c_str());
  return true;
}

}  // namespace pstf::dev
