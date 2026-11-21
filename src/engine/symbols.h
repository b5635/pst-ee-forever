// SPDX-License-Identifier: GPL-2.0-or-later
// Symbol manifest for Torment.exe GOG 3.2.2.0 (x64, base 0x140000000, no ASLR).
// Every engine address the DLL touches lives here, with the bytes we expect at it.
// pstf_verify checks them against the on-disk exe; the core checks them in memory
// before any module patches or calls anything. On a mismatch the module stays off.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pstf::sym {

constexpr uint64_t kImageBase = 0x140000000ull;
constexpr const char* kExeSha256 =
    "99ae15860cfdbcdaad0f48e005eee21444046b21de848c1deab9367e8246a3c5";

struct Symbol {
  const char* name;
  uint32_t rva;
  const uint8_t* bytes;  // expected bytes at rva
  size_t len;
  const char* evidence;  // Linux function + Windows EA
};

// --- exe entry point (loader patch site) -------------------------------------
// mainCRTStartup: sub rsp,28h; call __security_init_cookie; add rsp,28h; jmp ...
inline constexpr uint8_t kEntryBytes[] = {0x48, 0x83, 0xEC, 0x28, 0xE8, 0x97, 0x03,
                                          0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xE9};
constexpr uint32_t kEntryRva = 0x580AA0;  // PE AddressOfEntryPoint

// --- engine functions we call ------------------------------------------------
// CTlkTable::Fetch(uint strref, STR_RES& out, int noStrrefPrefix)
//   lin 673C70, win 1404A0840 (prologue unique at 15 bytes)
inline constexpr uint8_t kTlkFetchBytes[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
                                             0x55, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40};
// CSound::SetChannel(int channel, CGameArea* area)  lin 669E10, win 140495460
inline constexpr uint8_t kSetChannelBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                               0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
                                               0x48, 0x8B, 0x79, 0x18};
// CSound::SetFireForget(int)  lin 669B50, win 140495610
inline constexpr uint8_t kSetFireForgetBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
                                                  0xEC, 0x20, 0x48, 0x8B, 0x59, 0x18, 0x8B, 0xFA};
// CSound::Play(int)  lin 66C620; win 140494BD0 is the wrapper "mov rcx,[rcx+18h]; jmp imp_Play"
// (ported name CGameAnimationType::PickSound is wrong; confirmed by CMessagePlaySoundRef::Run
//  win 140236F93 and the body at 140494BF0 matching Linux Play: mixer check, active area, resref).
inline constexpr uint8_t kPlayBytes[] = {0x48, 0x8B, 0x49, 0x18, 0xE9, 0x17, 0x00, 0x00, 0x00};
// CSound::GetPlayTime(void) -> ms   lin 669BD0, win 1404949D0
inline constexpr uint8_t kGetPlayTimeBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48,
                                                0x8B, 0x59, 0x18, 0x48, 0x8B, 0x4B};

// --- call sites we redirect ----------------------------------------------------
// CGameTrigger::OnActionButton (lin 26A680, win 140218DD0), info-point branch:
//   1402196BE cmp [r14+57Ch], -1 ; 1402196FC call CTlkTable::Fetch ; then DisplayText + CGameText.
// The engine fetches STR_RES (text + CSound) but never plays the sound.
// (1402193DB is the travel-region Fetch; left alone.)
inline constexpr uint8_t kExamineFetchCallBytes[] = {0xE8, 0x3F, 0x71, 0x28, 0x00};

constexpr uint32_t kRvaTlkFetch = 0x4A0840;
constexpr uint32_t kRvaSetChannel = 0x495460;
constexpr uint32_t kRvaSetFireForget = 0x495610;
constexpr uint32_t kRvaPlay = 0x494BD0;
constexpr uint32_t kRvaGetPlayTime = 0x4949D0;
constexpr uint32_t kRvaExamineFetchCall = 0x2196FC;


// --- Lua 5.2 API (engine's own state; console + .menu UI) --------------------------------------
// Confirmed against Linux bodies (lin 6A1A10, 6A2240, 6A1760, 6A1850, 6A1AC0, 6A0BF0, 69FE20).
// NOTE: the ported names lua_createtable (1400E49E0) and lua_setfield (1400E47D0) are WRONG
// (1400E49E0 strlen's a key = lua_setfield); not used.
inline constexpr uint8_t kLuaPushCClosureBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                                    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x49};
inline constexpr uint8_t kLuaSetGlobalBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                                 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48};
inline constexpr uint8_t kLuaPushNumberBytes[] = {0x48, 0x8B, 0x41, 0x10, 0xF2, 0x0F, 0x11, 0x08,
                                                  0xC7, 0x40, 0x08, 0x03, 0x00, 0x00, 0x00, 0x48};
inline constexpr uint8_t kLuaPushStringBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
                                                  0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9};
inline constexpr uint8_t kLuaPushBooleanBytes[] = {0x4C, 0x8B, 0x41, 0x10, 0x33, 0xC0, 0x85, 0xD2,
                                                   0x0F, 0x95, 0xC0, 0x41, 0x89, 0x00, 0x41, 0xC7};
inline constexpr uint8_t kLuaToNumberXBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B,
                                                 0x05, 0x3B, 0x51, 0x64, 0x00, 0x48, 0x33, 0xC4};
inline constexpr uint8_t kLuaGetTopBytes[] = {0x48, 0x8B, 0x41, 0x20, 0x48, 0x8B, 0x51, 0x10,
                                              0x48, 0x2B, 0x10, 0x48, 0x8D, 0x42, 0xF0, 0x48};
constexpr uint32_t kRvaLuaPushCClosure = 0x0E44C0;
constexpr uint32_t kRvaLuaSetGlobal = 0x0E4920;
constexpr uint32_t kRvaLuaPushNumber = 0x0E4290;
constexpr uint32_t kRvaLuaPushString = 0x0E4350;
constexpr uint32_t kRvaLuaPushBoolean = 0x0E45D0;
constexpr uint32_t kRvaLuaToNumberX = 0x0E3E90;
constexpr uint32_t kRvaLuaGetTop = 0x0E3B30;
// CLUAConsole::LuaInit (lin 27AAB0, win 140226220): after tolua_BaldurLUA_open and
// "C = CLUAConsole:new()", 14022627A `call luaL_loadfilex(g_lua, "luaAuto.cfg", 0)`
// (callee 1400E6CA0 unported; Linux luaL_loadfilex 6A5320). We register PSTF_* there.
inline constexpr uint8_t kLuaAutoLoadCallBytes[] = {0xE8, 0x21, 0x0A, 0xEC, 0xFF};
constexpr uint32_t kRvaLuaAutoLoadCall = 0x22627A;
constexpr uint64_t kVaLuaLoadFileX = 0x1400E6CA0ull;

// --- combat-round length -----------------------------------------------------------------------
// CInfGame::LoadOptions (lin 3679E0, win 1402E6680):
//   1402E775C cmp cs:ENGINE_MODE(1407CB718), 3 ; 1402E776A mov eax,33h ; cmovz ebp,eax (ebp=64h)
//   1402E7775 call BGGetPrivateProfileInt("Game Options","Round Length", ebp)
//   1402E777D mov cs:ROUND_LENGTH(14071F5FC), eax
inline constexpr uint8_t kRoundModeCmpBytes[] = {0x83, 0x3D, 0xB5, 0x3F, 0x4E, 0x00, 0x03};
inline constexpr uint8_t kRoundDefaultBytes[] = {0xB8, 0x33, 0x00, 0x00, 0x00, 0x0F, 0x44, 0xE8};
inline constexpr uint8_t kRoundStoreBytes[] = {0x89, 0x05, 0x79, 0x7E, 0x43, 0x00};
constexpr uint32_t kRvaRoundModeCmp = 0x2E775C;
constexpr uint32_t kRvaRoundDefault = 0x2E776A;
constexpr uint32_t kRvaRoundStore = 0x2E777D;
constexpr uint64_t kVaEngineMode = 0x1407CB718ull;   // CChitin::ENGINE_MODE (bss, 3 for PST:EE)
constexpr uint64_t kVaRoundLength = 0x14071F5FCull;  // CGameAnimationType::ROUND_LENGTH (.data = 100)

// --- world clock -------------------------------------------------------------------------------
// CScreenWorld::AsynchronousUpdate (lin 4308B0, win 140353B20) at 14035432E:
//   mov rax,[g_pBaldurChitin 14072C278]; xor edx,edx; mov rcx,[rax+10C8h] (m_pObjectGame);
//   add rcx,7820h (CInfGame::m_worldTime); call CTimerWorld::UpdateTime (1402B3490).
// Linux uses chitin+0x1118 for the game pointer: the CBaldurChitin layout DIFFERS (Windows -0x50).
// CTimerWorld: +0 u32 game time in ticks, +4 u8 running (UpdateTime advances only if set).
inline constexpr uint8_t kWorldTimeChainBytes[] = {
    0x48, 0x8B, 0x05, 0x43, 0x7F, 0x3D, 0x00, 0x33, 0xD2, 0x48, 0x8B, 0x88, 0xC8, 0x10,
    0x00, 0x00, 0x48, 0x81, 0xC1, 0x20, 0x78, 0x00, 0x00, 0xE8, 0x46, 0xF1, 0xF5, 0xFF};
constexpr uint32_t kRvaWorldTimeChain = 0x35432E;
constexpr uint64_t kVaBaldurChitin = 0x14072C278ull;
constexpr uint32_t kOffChitinGame = 0x10C8;
constexpr uint32_t kOffGameWorldTimer = 0x7820;
constexpr uint64_t kVaTimerUpdateTime = 0x1402B3490ull;

// --- more Lua: run a file later (dev autorun) ----------------------------------------------------
// lua_pcallk(L,nargs,nres,errfunc,ctx,k): lin 6A2B50, win 1400E4EF0 (LuaInit calls it at 140226264).
// lua_settop: lin 69FE40, win 1400E3B50 (LuaInit tail `jmp` at 1402262CB with edx=-2).
// lua_tolstring: lin 6A1050, win 1400E40E0 (index2addr, type 4 fast path, luaV_tostring).
// g_lua: win 14072C8C8 (LuaInit 140226226 `mov rcx,[g_lua]`).
inline constexpr uint8_t kLuaPCallKBytes[] = {0x40, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
                                              0xEC, 0x68, 0x48, 0x8B, 0x05, 0xD7, 0x40, 0x64};
inline constexpr uint8_t kLuaSetTopBytes[] = {0x85, 0xD2, 0x78, 0x34, 0x48, 0x8B, 0x41, 0x20,
                                              0x48, 0x63, 0xD2, 0x48, 0xFF, 0xC2, 0x48, 0xC1};
inline constexpr uint8_t kLuaToLStringBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                                 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49};
inline constexpr uint8_t kLuaLoadFileXBytes[] = {0x40, 0x55, 0x53, 0x41, 0x54, 0x41, 0x56, 0x41,
                                                 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xA0, 0xFE, 0xFF};
inline constexpr uint8_t kGLuaLoadBytes[] = {0x48, 0x8B, 0x0D, 0x9B, 0x66, 0x50, 0x00};
constexpr uint32_t kRvaLuaPCallK = 0x0E4EF0;
constexpr uint32_t kRvaLuaSetTop = 0x0E3B50;
constexpr uint32_t kRvaLuaToLString = 0x0E40E0;
constexpr uint32_t kRvaLuaLoadFileX = 0x0E6CA0;
constexpr uint32_t kRvaGLuaLoad = 0x226226;
constexpr uint64_t kVaGLua = 0x14072C8C8ull;

// --- frame tick ----------------------------------------------------------------------------------
// CBaldurChitin::SynchronousUpdate (lin 15BE60; win 140144250, ported name __acrt_uninitialize_tmpfile
// is wrong): `sub rsp,28h; call CChitin::SynchronousUpdate (1404A6120); add rsp,28h; jmp chWriteInifile`.
// Called once per main-loop iteration by CChitin::Update on the main (game) thread.
inline constexpr uint8_t kFrameTickCallBytes[] = {0xE8, 0xC7, 0x1E, 0x36, 0x00};
constexpr uint32_t kRvaFrameTickCall = 0x144254;
constexpr uint64_t kVaChitinSyncUpdate = 0x1404A6120ull;

// --- screens (engines) ---------------------------------------------------------------------------
// CChitin::pActiveEngine at chitin+0x260 (lin CChitin::SynchronousUpdate `this+76` qword;
//   win 1404A6135 `cmp qword [rcx+260h],0`). Screens hang off CBaldurChitin, Windows = Linux - 0x50:
//   start +0x1138 (tolua GetEngineStart win 14045F7BE; lin 0x1188), world +0x1148 (GetEngineWorld
//   win 14045F9CE; lin 0x1198), load +0x1110 (OnContinueGame win 1403459C5; lin 0x1160).
// CScreenStart::OnContinueGame(this) -> bool: lin 40F4C0, win 1403459A0: loads the save named by
//   Baldur.lua 'Game Options','Last Save PST' (GetContinueSaveGameName, "Last Save %s" + campaign).
inline constexpr uint8_t kActiveEngineCmpBytes[] = {0x48, 0x83, 0xB9, 0x60, 0x02, 0x00, 0x00, 0x00};
inline constexpr uint8_t kGetEngineStartBytes[] = {0x48, 0x8B, 0x97, 0x38, 0x11, 0x00, 0x00};
inline constexpr uint8_t kGetEngineWorldBytes[] = {0x48, 0x8B, 0x97, 0x48, 0x11, 0x00, 0x00};
inline constexpr uint8_t kContinueLoadScreenBytes[] = {0x48, 0x8B, 0x0D, 0xAC, 0x68, 0x3E, 0x00, 0x33,
                                                       0xD2, 0x48, 0x8B, 0x89, 0x10, 0x11, 0x00, 0x00};
inline constexpr uint8_t kOnContinueGameBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D,
                                                   0x54, 0x24, 0x38, 0xE8, 0x60, 0xFC, 0xFF, 0xFF};
constexpr uint32_t kRvaActiveEngineCmp = 0x4A6135;
constexpr uint32_t kRvaGetEngineStart = 0x45F7BE;
constexpr uint32_t kRvaGetEngineWorld = 0x45F9CE;
constexpr uint32_t kRvaContinueLoadScreen = 0x3459C5;
constexpr uint32_t kRvaOnContinueGame = 0x3459A0;
constexpr uint32_t kOffChitinActiveEngine = 0x260;
constexpr uint32_t kOffChitinStartScreen = 0x1138;
constexpr uint32_t kOffChitinWorldScreen = 0x1148;
constexpr uint32_t kOffChitinLoadScreen = 0x1110;

#define PSTF_SYM(n, rva, b, ev) Symbol{n, rva, b, sizeof(b), ev}
inline constexpr Symbol kAll[] = {
    PSTF_SYM("entry", kEntryRva, kEntryBytes, "PE AddressOfEntryPoint"),
    PSTF_SYM("CTlkTable::Fetch", kRvaTlkFetch, kTlkFetchBytes, "lin 673C70 / win 1404A0840"),
    PSTF_SYM("CSound::SetChannel", kRvaSetChannel, kSetChannelBytes, "lin 669E10 / win 140495460"),
    PSTF_SYM("CSound::SetFireForget", kRvaSetFireForget, kSetFireForgetBytes,
             "lin 669B50 / win 140495610"),
    PSTF_SYM("CSound::Play", kRvaPlay, kPlayBytes, "lin 66C620 / win 140494BD0"),
    PSTF_SYM("CSound::GetPlayTime", kRvaGetPlayTime, kGetPlayTimeBytes,
             "lin 669BD0 / win 1404949D0"),
    PSTF_SYM("OnActionButton.examine.Fetch", kRvaExamineFetchCall, kExamineFetchCallBytes,
             "lin 26A680+L533 / win 1402196FC"),
    PSTF_SYM("lua_pushcclosure", kRvaLuaPushCClosure, kLuaPushCClosureBytes, "lin 6A1A10 / win 1400E44C0"),
    PSTF_SYM("lua_setglobal", kRvaLuaSetGlobal, kLuaSetGlobalBytes, "lin 6A2240 / win 1400E4920"),
    PSTF_SYM("lua_pushnumber", kRvaLuaPushNumber, kLuaPushNumberBytes, "lin 6A1760 / win 1400E4290"),
    PSTF_SYM("lua_pushstring", kRvaLuaPushString, kLuaPushStringBytes, "lin 6A1850 / win 1400E4350"),
    PSTF_SYM("lua_pushboolean", kRvaLuaPushBoolean, kLuaPushBooleanBytes, "lin 6A1AC0 / win 1400E45D0"),
    PSTF_SYM("lua_tonumberx", kRvaLuaToNumberX, kLuaToNumberXBytes, "lin 6A0BF0 / win 1400E3E90"),
    PSTF_SYM("lua_gettop", kRvaLuaGetTop, kLuaGetTopBytes, "lin 69FE20 / win 1400E3B30"),
    PSTF_SYM("LuaInit.luaAuto.loadfilex", kRvaLuaAutoLoadCall, kLuaAutoLoadCallBytes,
             "lin 27AAB0 / win 14022627A"),
    PSTF_SYM("LoadOptions.round.cmp_mode", kRvaRoundModeCmp, kRoundModeCmpBytes, "lin 3679E0 / win 1402E775C"),
    PSTF_SYM("LoadOptions.round.default51", kRvaRoundDefault, kRoundDefaultBytes, "lin 3679E0 / win 1402E776A"),
    PSTF_SYM("LoadOptions.round.store", kRvaRoundStore, kRoundStoreBytes, "lin 3679E0 / win 1402E777D"),
    PSTF_SYM("AsyncUpdate.worldtime.chain", kRvaWorldTimeChain, kWorldTimeChainBytes,
             "lin 4308B0 / win 14035432E"),
    PSTF_SYM("lua_pcallk", kRvaLuaPCallK, kLuaPCallKBytes, "lin 6A2B50 / win 1400E4EF0"),
    PSTF_SYM("lua_settop", kRvaLuaSetTop, kLuaSetTopBytes, "lin 69FE40 / win 1400E3B50"),
    PSTF_SYM("lua_tolstring", kRvaLuaToLString, kLuaToLStringBytes, "lin 6A1050 / win 1400E40E0"),
    PSTF_SYM("luaL_loadfilex", kRvaLuaLoadFileX, kLuaLoadFileXBytes, "lin 6A5320 / win 1400E6CA0"),
    PSTF_SYM("LuaInit.g_lua", kRvaGLuaLoad, kGLuaLoadBytes, "lin 27AAB0 / win 140226226"),
    PSTF_SYM("BaldurChitin.SyncUpdate.call", kRvaFrameTickCall, kFrameTickCallBytes,
             "lin 15BE60 / win 140144254"),
    PSTF_SYM("Chitin.SyncUpdate.activeEngine", kRvaActiveEngineCmp, kActiveEngineCmpBytes,
             "lin 65C5C0 / win 1404A6135"),
    PSTF_SYM("tolua.GetEngineStart", kRvaGetEngineStart, kGetEngineStartBytes, "lin 5D35D0 / win 14045F7BE"),
    PSTF_SYM("tolua.GetEngineWorld", kRvaGetEngineWorld, kGetEngineWorldBytes, "win 14045F9CE"),
    PSTF_SYM("OnContinueGame.loadScreen", kRvaContinueLoadScreen, kContinueLoadScreenBytes,
             "lin 40F4C0 / win 1403459C5"),
    PSTF_SYM("CScreenStart::OnContinueGame", kRvaOnContinueGame, kOnContinueGameBytes,
             "lin 40F4C0 / win 1403459A0"),
};
#undef PSTF_SYM

}  // namespace pstf::sym
