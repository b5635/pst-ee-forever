// SPDX-License-Identifier: GPL-2.0-or-later
// Dev Lua bridge: registers PSTF_* functions in the engine's Lua state so the in-game console
// (Ctrl+Space with "Cheats" or "Debug Mode" on, see docs/TBC_RESEARCH.md §2) and luaAuto.cfg can
// query our probes. Hook: the luaL_loadfilex("luaAuto.cfg") call in CLUAConsole::LuaInit, which
// runs once on the game thread right after the engine's own bindings and `C` exist.
// Every call is also written to pst_forever.log, so an agent can read results without a screen.
#include "modules/dev/lua_bridge.h"

#include <cstdio>
#include <string>

#include "core/log.h"
#include "core/patch.h"
#include "engine/lua.h"
#include "engine/screens.h"
#include "engine/symbols.h"
#include "engine/world.h"
#include "modules/tbc/tbc_rules.h"
#include "modules/tbc/tbc_sim.h"

namespace pstf::dev {
namespace {
using engine::LuaState;
using LoadFileFn = int(__fastcall*)(LuaState*, const char*, const char*);
LoadFileFn g_loadfile = nullptr;

int Ret(LuaState* L, const char* fn, double v) {
  Log("lua: %s -> %g", fn, v);
  engine::LuaPushNumber(L, v);
  return 1;
}
int Ret(LuaState* L, const char* fn, const std::string& s) {
  Log("lua: %s -> %s", fn, s.c_str());
  engine::LuaPushString(L, s.c_str());
  return 1;
}

int __cdecl Version(LuaState* L) { return Ret(L, "PSTF_Version", std::string("pst_forever " __DATE__)); }
int __cdecl RoundLength(LuaState* L) {
  return Ret(L, "PSTF_RoundLength", static_cast<double>(engine::RoundLengthTicks()));
}
int __cdecl EngineMode(LuaState* L) { return Ret(L, "PSTF_EngineMode", engine::EngineMode()); }
int __cdecl WorldTime(LuaState* L) {
  uint32_t t = 0;
  bool running = false;
  const bool ok = engine::WorldTime(&t, &running);
  Log("lua: PSTF_WorldTime -> %u running=%d (game %s)", t, running ? 1 : 0, ok ? "yes" : "no");
  engine::LuaPushNumber(L, ok ? t : -1.0);
  engine::LuaPushBool(L, running);
  return 2;
}
int __cdecl Screen(LuaState* L) {
  return Ret(L, "PSTF_Screen", std::string(engine::ScreenName(engine::ActiveScreen())));
}
int __cdecl RoundsForSeconds(LuaState* L) {
  const double s = engine::LuaNumberArg(L, 1, 0);
  const uint32_t rounds =
      tbc::RoundsForSeconds(s > 0 ? static_cast<uint32_t>(s) : 0, engine::RoundLengthTicks());
  return Ret(L, "PSTF_RoundsForSeconds", rounds);
}
int __cdecl TbcSim(LuaState* L) {
  const auto seed = static_cast<uint32_t>(engine::LuaNumberArg(L, 1, 1));
  return Ret(L, "PSTF_TbcSim", tbc::RunSim(seed, engine::RoundLengthTicks()).Summary());
}
// In-process assertions on engine facts the TBC design depends on. "ok" or a list of failures.
int __cdecl SelfTest(LuaState* L) {
  std::string fails;
  auto check = [&](bool c, const char* what) {
    if (!c) fails += std::string(fails.empty() ? "" : "; ") + what;
  };
  check(engine::EngineMode() == 3, "ENGINE_MODE != 3");
  const uint32_t rl = engine::RoundLengthTicks();
  // luaAuto.cfg runs before CInfGame::LoadOptions: ROUND_LENGTH still holds its static 100 then
  // (seen in game 2026-11-21). From the console it must be 51 unless Baldur.lua overrides it.
  check(rl != tbc::kRoundTicksOtherEE, "ROUND_LENGTH=100: LoadOptions not run yet (luaAuto.cfg is too early)");
  check(rl == tbc::kRoundTicksPst || rl == tbc::kRoundTicksOtherEE,
        "ROUND_LENGTH not 51 (Baldur.lua 'Game Options','Round Length' override?)");
  uint32_t t = 0;
  bool running = false;
  check(engine::WorldTime(&t, &running), "no game object");
  check(tbc::RunSim(1, rl ? rl : tbc::kRoundTicksPst).winner == 0, "director sim");
  return Ret(L, "PSTF_SelfTest", fails.empty() ? std::string("ok") : fails);
}

int __fastcall LuaAutoLoad(LuaState* L, const char* file, const char* mode) {
  struct {
    const char* name;
    engine::LuaCFn fn;
  } const fns[] = {{"PSTF_Version", Version},         {"PSTF_RoundLength", RoundLength},
                   {"PSTF_EngineMode", EngineMode},   {"PSTF_WorldTime", WorldTime},
                   {"PSTF_RoundsForSeconds", RoundsForSeconds},
                   {"PSTF_TbcSim", TbcSim},           {"PSTF_SelfTest", SelfTest},
                   {"PSTF_Screen", Screen}};
  for (const auto& f : fns) engine::LuaRegister(L, f.name, f.fn);
  Log("dev: %zu PSTF_* Lua functions registered (round length %u, engine mode %d)",
      sizeof(fns) / sizeof(fns[0]), engine::RoundLengthTicks(), engine::EngineMode());
  return g_loadfile(L, file, mode);
}

}  // namespace

bool InstallLuaBridge() {
  if (!engine::LuaApiAvailable() || !engine::WorldApiAvailable()) {
    Log("dev: Lua/world functions differ from the manifest; Lua bridge off");
    return false;
  }
  for (const auto& s : sym::kAll) {
    if (s.rva != sym::kRvaLuaAutoLoadCall) continue;
    const uint64_t original = RedirectCall(s, reinterpret_cast<void*>(&LuaAutoLoad));
    if (original != sym::kVaLuaLoadFileX) {
      Log("dev: luaAuto call site not patched (target %llX); Lua bridge off",
          static_cast<unsigned long long>(original));
      return false;
    }
    g_loadfile = reinterpret_cast<LoadFileFn>(original);
    Log("dev: Lua bridge installed at %llX", static_cast<unsigned long long>(sym::kImageBase + s.rva));
    return true;
  }
  return false;
}

}  // namespace pstf::dev
