// SPDX-License-Identifier: GPL-2.0-or-later
// Core entry: called once by the version.dll loader on the main thread, before the CRT starts.
#include <windows.h>

#include "core/config.h"
#include "core/crashlog.h"
#include "core/log.h"
#include "core/version_guard.h"
#include "modules/dev/autoload.h"
#include "modules/smooth/smooth_module.h"
#include "modules/tbc/tbc_module.h"
#include "modules/dev/lua_bridge.h"
#include "modules/voice/examine_voice.h"

extern "C" __declspec(dllexport) int __cdecl PSTF_Init() {
  static bool done = false;
  if (done) return 0;
  done = true;
  pstf::LogOpen();
  pstf::Log("pst_forever " __DATE__ " " __TIME__);
  pstf::InstallCrashLog();
  if (!pstf::IsSupportedExe()) return 1;
  const auto& ini = pstf::Config();
  if (pstf::IniBool(ini, "voice.examine", false)) {
    pstf::voice::InstallExamineVoice();
  } else {
    pstf::Log("voice: examine voice disabled in ini");
  }
  // The tbc HUD and End Turn button call PSTF_* functions, so tbc needs the Lua bridge too.
  if (pstf::IniBool(ini, "dev.lua", false) || pstf::IniBool(ini, "tbc.enabled", false) ||
      pstf::IniBool(ini, "smooth.interpolation", false)) {
    pstf::dev::InstallLuaBridge();
  } else {
    pstf::Log("dev: Lua bridge disabled in ini");
  }
  pstf::dev::InstallAutoload(ini);
  pstf::smooth::InstallSmooth(ini);
  pstf::tbc::InstallTbc(ini);
  return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(inst);
  return TRUE;
}
