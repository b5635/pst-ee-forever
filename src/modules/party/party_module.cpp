// SPDX-License-Identifier: GPL-2.0-or-later
// Module: 8-member party (TNO + all seven companions). Ini [party] enabled=1, size=8; off = vanilla
// (nothing patched). The engine keeps its own rules: joining, leaving, scripts and saves run the
// engine's code with its party arrays moved and grown to 8 (engine/party.cpp, docs/PARTY8_PLAN.md).
#include "modules/party/party_module.h"

#include <string>

#include "core/log.h"
#include "engine/lua.h"
#include "engine/party.h"
#include "modules/dev/lua_bridge.h"

namespace pstf::party {
namespace {

// PSTF_Party() -> "max=8 count=N order=... fixed=..." (party ids by order and by portrait slot).
int __cdecl LuaParty(engine::LuaState* L) {
  const std::string s = engine::PartyDump();
  Log("lua: PSTF_Party -> %s", s.c_str());
  engine::LuaPushString(L, s.c_str());
  return 1;
}

// PSTF_PartyNames() -> "NAMELESS,MORTE,..." (script names in party order).
int __cdecl LuaPartyNames(engine::LuaState* L) {
  const std::string s = engine::PartyNames();
  Log("lua: PSTF_PartyNames -> %s", s.c_str());
  engine::LuaPushString(L, s.c_str());
  return 1;
}

// PSTF_Trigger(who, triggerId, param1[, objectName[, string1[, quiet]]]) -> 1/0 as the engine evaluates it (-1 unavailable).
int __cdecl LuaTrigger(engine::LuaState* L) {
  const auto who = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const auto id = static_cast<uint16_t>(engine::LuaNumberArg(L, 2, 0));
  const auto p1 = static_cast<int32_t>(engine::LuaNumberArg(L, 3, 0));
  const std::string name = engine::LuaStringArg(L, 4);
  const std::string str = engine::LuaStringArg(L, 5);
  const bool quiet = engine::LuaNumberArg(L, 6, 0) != 0;
  const int r = engine::EvalTrigger(who, id, p1, name.c_str(), str.c_str());
  if (!quiet)
    Log("lua: PSTF_Trigger(%X, 0x%X, %d, %s, %s) -> %d", static_cast<unsigned>(who), id, p1, name.c_str(), str.c_str(), r);
  engine::LuaPushNumber(L, r);
  return 1;
}

// PSTF_CanSave() -> 1/0, refusal strref (CInfGame::CanSaveGame as the quick save asks it).
int __cdecl LuaCanSave(engine::LuaState* L) {
  uint32_t reason = 0;
  const int r = engine::CanSaveGame(&reason);
  Log("lua: PSTF_CanSave -> %d reason %d", r, static_cast<int32_t>(reason));
  engine::LuaPushNumber(L, r);
  engine::LuaPushNumber(L, static_cast<int32_t>(reason));
  return 2;
}

// PSTF_Formation(formation[, angle]) -> "selected=N pts=x,y;..." (CAIGroup::GetOffsets for a party move).
int __cdecl LuaFormation(engine::LuaState* L) {
  const int f = static_cast<int>(engine::LuaNumberArg(L, 1, 0));
  const int a = static_cast<int>(engine::LuaNumberArg(L, 2, 0));
  const std::string s = engine::FormationPoints(f, a);
  Log("lua: PSTF_Formation(%d, %d) -> %s", f, a, s.c_str());
  engine::LuaPushString(L, s.c_str());
  return 1;
}

// PSTF_UnselectAll() -> 1/0 (CInfGame::UnselectAll).
int __cdecl LuaUnselectAll(engine::LuaState* L) {
  const bool ok = engine::UnselectAll();
  Log("lua: PSTF_UnselectAll -> %d", ok ? 1 : 0);
  engine::LuaPushNumber(L, ok ? 1 : 0);
  return 1;
}

// PSTF_ScreenCaches() -> "map count=N ids=.. mask=X inv sel=S piles=.. flags=.." (map markers and the
// inventory ground-pile cache, 8 slots each with the layout on).
int __cdecl LuaScreenCaches(engine::LuaState* L) {
  const std::string s = engine::ScreenCaches();
  Log("lua: PSTF_ScreenCaches -> %s", s.c_str());
  engine::LuaPushString(L, s.c_str());
  return 1;
}

}  // namespace

void InstallParty(const IniMap& ini) {
  if (!IniBool(ini, "party.enabled", false) || IniInt(ini, "party.size", 6) < 8) {
    Log("party: 8-member party disabled in ini");
    return;
  }
  if (!engine::InstallParty8()) return;
  dev::AddLuaFunction("PSTF_Party", &LuaParty);
  dev::AddLuaFunction("PSTF_PartyNames", &LuaPartyNames);
  dev::AddLuaFunction("PSTF_Trigger", &LuaTrigger);
  dev::AddLuaFunction("PSTF_CanSave", &LuaCanSave);
  dev::AddLuaFunction("PSTF_Formation", &LuaFormation);
  dev::AddLuaFunction("PSTF_UnselectAll", &LuaUnselectAll);
  dev::AddLuaFunction("PSTF_ScreenCaches", &LuaScreenCaches);
}

}  // namespace pstf::party
