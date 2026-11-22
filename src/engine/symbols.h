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

// --- creatures (turn-based combat) ---------------------------------------------------------------
// Object array (lin CGameObjectArray::GetDeny 362B80): 16-byte entries {u16 seq, pad, CGameObject* at +8},
// id = (index << 16) | seq. Windows (CInfGame::CanRestParty win 1402D28B0): m_pArray 14072D860
// (1402D2910 lea r8), m_maxArrayIndex i16 14072D854 (1402D2965), m_nextObjectId i16 14072D858 (1402D297B).
// Party: CInfGame +0xA200 i32 ids[6], +0xA218 i16 count (1402D294C, 1402D2917).
// Sprite (Windows offsets; the CGameSprite layout DRIFTS from Linux past ~0x1000):
//   +0x0C/+0x10 pos x,y and +0x18 CGameArea* (1402D29CF/1402D29AA); +0x38 EA (CGameSprite::Enemy
//   140408B14); +0x48 id (140408BED); +0x348 queued-action count (GetNextAction 140172D31);
//   +0x3F0 current action id, 0 = none (ClearAI 140404BC6..); alive = !(state & 0x800) where
//   state = *(u32*)(sprite + (*(i32*)(sprite+0x4BBC) ? 0xFE8 : 0x1C98)) (CGameSprite::Animate 1403FB520).
inline constexpr uint8_t kObjArrayLeaBytes[] = {0x4C, 0x8D, 0x05, 0x49, 0xAF, 0x45, 0x00};
inline constexpr uint8_t kObjMaxCmpBytes[] = {0x66, 0x39, 0x05, 0xE8, 0xAE, 0x45, 0x00};
inline constexpr uint8_t kObjNextCmpBytes[] = {0x66, 0x39, 0x0D, 0xD6, 0xAE, 0x45, 0x00};
inline constexpr uint8_t kPartyCountBytes[] = {0x41, 0x0F, 0xB7, 0x8C, 0x24, 0x18, 0xA2, 0x00, 0x00};
inline constexpr uint8_t kPartyIdsBytes[] = {0x41, 0x8B, 0x8C, 0x84, 0x00, 0xA2, 0x00, 0x00};
inline constexpr uint8_t kSpriteAreaBytes[] = {0x49, 0x8B, 0x46, 0x18};
inline constexpr uint8_t kSpritePosBytes[] = {0x4D, 0x8B, 0x66, 0x0C};
inline constexpr uint8_t kAnimateBytes[] = {0x83, 0xB9, 0xBC, 0x4B, 0x00, 0x00, 0x00, 0xB8, 0xE8, 0x0F, 0x00,
                                            0x00, 0xBA, 0x98, 0x1C, 0x00, 0x00, 0x0F, 0x44, 0xC2, 0x8B, 0x04,
                                            0x08, 0xC1, 0xE8, 0x0B, 0xF7, 0xD0, 0x83, 0xE0, 0x01, 0xC3};
inline constexpr uint8_t kEnemyEaBytes[] = {0x0F, 0xB6, 0x05, 0x57, 0x67, 0x21, 0x00, 0x48, 0x8B, 0xF9, 0x88, 0x41, 0x38};
inline constexpr uint8_t kEnemyIdBytes[] = {0x8B, 0x57, 0x48};
inline constexpr uint8_t kQueueCountBytes[] = {0x44, 0x39, 0xA1, 0x48, 0x03, 0x00, 0x00};
inline constexpr uint8_t kClearAICurActionBytes[] = {0x0F, 0xB7, 0x05, 0xD3, 0x75, 0x32, 0x00, 0x48, 0x8D, 0x8F, 0xF8,
                                                     0x03, 0x00, 0x00, 0x66, 0x89, 0x87, 0xF0, 0x03, 0x00, 0x00};
constexpr uint32_t kRvaObjArrayLea = 0x2D2910, kRvaObjMaxCmp = 0x2D2965, kRvaObjNextCmp = 0x2D297B;
constexpr uint32_t kRvaPartyCount = 0x2D2917, kRvaPartyIds = 0x2D294C;
constexpr uint32_t kRvaSpriteArea = 0x2D29AA, kRvaSpritePos = 0x2D29CF;
constexpr uint32_t kRvaAnimate = 0x3FB520, kRvaEnemyEa = 0x408B0A, kRvaEnemyId = 0x408BED;
constexpr uint32_t kRvaQueueCount = 0x172D31, kRvaClearAICurAction = 0x404BC6;
constexpr uint64_t kVaObjArray = 0x14072D860ull, kVaObjMax = 0x14072D854ull, kVaObjNext = 0x14072D858ull;
constexpr uint32_t kOffGamePartyIds = 0xA200, kOffGamePartyCount = 0xA218;
constexpr uint32_t kOffSpritePos = 0x0C, kOffSpriteArea = 0x18, kOffSpriteEa = 0x38, kOffSpriteId = 0x48;
constexpr uint32_t kOffSpriteQueueCount = 0x348, kOffSpriteCurAction = 0x3F0;
constexpr uint32_t kOffSpriteStatsSel = 0x4BBC, kOffSpriteStatsA = 0xFE8, kOffSpriteStatsB = 0x1C98;
constexpr uint32_t kStateDead = 0x800;
// Current HP i16 at sprite+0x574 (CGameSprite::FloatingHP "%d/%d": lin this+698*2, win 14043A900).
inline constexpr uint8_t kHpBytes[] = {0x44, 0x0F, 0xBF, 0x86, 0x74, 0x05, 0x00, 0x00};
constexpr uint32_t kRvaHp = 0x43A900, kOffSpriteHp = 0x574;

// CGameSprite vtable (win 140649328; lin _ZTV11CGameSprite E57298). Windows slot = Linux slot - 1
// around AIUpdate/ProcessAI (one MSVC deleting dtor vs two GCC dtors); slots 29..34 do NOT map 1:1.
//   slot 3 AIUpdate 1403B4330, slot 35 (+0x118, VA 140649440) ProcessAI 140421830 (lin slot 36).
constexpr uint64_t kVaSpriteVtable = 0x140649328ull;
inline constexpr uint8_t kSlotProcessAIBytes[] = {0x30, 0x18, 0x42, 0x40, 0x01, 0x00, 0x00, 0x00};
constexpr uint32_t kRvaSlotProcessAI = 0x649440;
inline constexpr uint8_t kProcessAIBytes[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x48,
                                              0x8D, 0x6C, 0x24, 0xF0, 0x48, 0x81, 0xEC, 0x10};
constexpr uint32_t kRvaProcessAI = 0x421830;
// CGameSprite::AIUpdate calls CGameSprite::AIUpdateWalk (lin 51FFA0, win 1403B6D20) at two sites; void.
inline constexpr uint8_t kWalkCall1Bytes[] = {0xE8, 0x0A, 0x0B, 0x00, 0x00};
inline constexpr uint8_t kWalkCall2Bytes[] = {0xE8, 0x9C, 0x05, 0x00, 0x00};
constexpr uint32_t kRvaWalkCall1 = 0x3B6211, kRvaWalkCall2 = 0x3B677F;
constexpr uint64_t kVaAIUpdateWalk = 0x1403B6D20ull;
// CGameSprite::ClearAI(uchar) lin 4A5320, win 140404B90 (vtable slot 31): ClearActions(0) via +0xC8,
//   ClearTriggers, SetCurrAction(NULL_ACTION 14072C1A0) via +0x120, resets +0x3F0.
inline constexpr uint8_t kClearAIBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
                                            0xEC, 0x20, 0x48, 0x8B, 0x01, 0x0F, 0xB6, 0xDA};
// CGameSprite::DropPath lin 505F80, win 1403C4280: frees the path at +0x4470 (lin +0x4458).
inline constexpr uint8_t kDropPathBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
                                             0xD9, 0x48, 0x8B, 0x89, 0x70, 0x44, 0x00, 0x00};
// CGameSprite::Enemy (action Enemy()) lin 4AEA80, win 140408B00: EA := ENEMY the engine's way.
inline constexpr uint8_t kEnemyBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
                                          0xEC, 0x40, 0x0F, 0xB6, 0x05, 0x57, 0x67, 0x21};
constexpr uint32_t kRvaClearAI = 0x404B90, kRvaDropPath = 0x3C4280, kRvaEnemy = 0x408B00;
// luaL_loadstring lin 6A58A0, win 1400E7380 (LuaInit calls it at 140226240 for "C = CLUAConsole:new()").
inline constexpr uint8_t kLuaLoadStringBytes[] = {0x48, 0x83, 0xEC, 0x58, 0x48, 0x8B, 0x05, 0x4D,
                                                  0x1C, 0x64, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89};
constexpr uint32_t kRvaLuaLoadString = 0x0E7380;

// --- turn-based HUD -------------------------------------------------------------------------
// uiLoadMenu (lin 581790 uiLoadMenu(CResText*), win 140459E00; the port named it lua_type and gave
// the name uiLoadMenu to its tokenizer 140456630). After CRes::Demand it copies the UI.MENU text
// (CString at CResText+0x58) with `call 1404842C0` (CString copy ctor) at 140459EC3, takes the char*,
// releases the copy and tokenizes the text. We redirect that copy to append our menus.
inline constexpr uint8_t kMenuCopyCallBytes[] = {0xE8, 0xF8, 0xA3, 0x02, 0x00};
constexpr uint32_t kRvaMenuCopyCall = 0x459EC3;
constexpr uint64_t kVaCStringCopy = 0x1404842C0ull;
// CString::CString(char const*) lin 69BED0, win 140484330 (afxPchNil, strlen, AllocBuffer, memmove).
inline constexpr uint8_t kCStringCtorBytes[] = {0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83,
                                                0xEC, 0x20, 0x48, 0x8B, 0x05, 0x6F, 0xCD, 0x29};
constexpr uint32_t kRvaCStringCtor = 0x484330;
// Bestiary index: CGameAnimationTypePlanescapeCharacter +0x1D0C (lin SetNewAnimation reads the
// animation INI key "bestiary"; Render/RenderLarge call SetBestiaryKnown(it)). Ctor win 14038042C:
// lea rax, vtbl (1406470B0) ... mov [rsi+1D0Ch], edi (-1).
inline constexpr uint8_t kAnimVtblLeaBytes[] = {0x48, 0x8D, 0x05, 0x7D, 0x6C, 0x2C, 0x00};
inline constexpr uint8_t kAnimBestiaryBytes[] = {0x89, 0xBE, 0x0C, 0x1D, 0x00, 0x00};
constexpr uint32_t kRvaAnimVtblLea = 0x38042C, kRvaAnimBestiary = 0x380487;
constexpr uint64_t kVaAnimPstVtable = 0x1406470B0ull;
constexpr uint32_t kOffAnimBestiary = 0x1D0C;
// Sprite -> animation type pointer: CGameAIBase::GetTargetShareType win 14017358E
// mov rcx,[rdi+3950h] (lin this+1831*8 = +0x3938) then vtable +192 GetPersonalSpace.
inline constexpr uint8_t kSpriteAnimBytes[] = {0x48, 0x8B, 0x8F, 0x50, 0x39, 0x00, 0x00};
constexpr uint32_t kRvaSpriteAnim = 0x17358E, kOffSpriteAnim = 0x3950;
// Max HP i16 in the derived stats (FloatingHP "%d/%d" win 14043A908: sel ? +0xFEC : +0x1C9C).
inline constexpr uint8_t kMaxHpBytes[] = {0xB9, 0x9C, 0x1C, 0x00, 0x00, 0xB8, 0xEC, 0x0F, 0x00, 0x00};
constexpr uint32_t kRvaMaxHp = 0x43A908, kOffSpriteMaxHpA = 0xFEC, kOffSpriteMaxHpB = 0x1C9C;
// Name strref (CGameSprite::GetNameRef lin 521CB0 this+342*4, win 1403C93F4 mov eax,[rcx+558h]).
inline constexpr uint8_t kNameRefBytes[] = {0x8B, 0x81, 0x58, 0x05, 0x00, 0x00, 0x83, 0xF8, 0xFF};
constexpr uint32_t kRvaNameRef = 0x3C93F4, kOffSpriteNameRef = 0x558;
// Walk path (CGameSprite::AIUpdateWalk lin 51FFA0, win 1403B6D20): int cells at +0x4470
// (1403B718E), next index i16 +0x44F0 vs count i16 +0x4478 (1403B71FA). Cell c: x = c % GRID_ACTUALX,
// y = GRID_ACTUALY - 1 - c / GRID_ACTUALX; point = (x*SX + SX/2, y*SY + SY/2) (1403B72E8..1403B732D).
inline constexpr uint8_t kPathPtrBytes[] = {0x4C, 0x8B, 0x8E, 0x70, 0x44, 0x00, 0x00};
inline constexpr uint8_t kPathIndexBytes[] = {0x0F, 0xB7, 0x8E, 0xF0, 0x44, 0x00, 0x00,
                                              0x66, 0x3B, 0x8E, 0x78, 0x44, 0x00, 0x00};
inline constexpr uint8_t kGridXBytes[] = {0x0F, 0xBF, 0x0D, 0x9D, 0x72, 0x27, 0x00};
inline constexpr uint8_t kGridYBytes[] = {0x44, 0x0F, 0xBF, 0x15, 0x99, 0x72, 0x27, 0x00};
inline constexpr uint8_t kGridSxBytes[] = {0x0F, 0xB6, 0x05, 0x82, 0x72, 0x27, 0x00};
constexpr uint32_t kRvaPathPtr = 0x3B718E, kRvaPathIndex = 0x3B71FA;
constexpr uint32_t kRvaGridX = 0x3B72E8, kRvaGridY = 0x3B72EF, kRvaGridSx = 0x3B72FF;
constexpr uint32_t kOffSpritePath = 0x4470, kOffSpritePathIndex = 0x44F0, kOffSpritePathCount = 0x4478;
constexpr uint64_t kVaGridActualX = 0x14062E58Cull, kVaGridActualY = 0x14062E590ull;
constexpr uint64_t kVaGridSquareX = 0x14062E588ull, kVaGridSquareY = 0x14062E589ull;
// World -> UI point, as CGameAreaNotes::UpdateButtonPositions (lin 1C1920, win 1401A5B94):
// area = game[+0xA180 + 8 * byte game[+0xA178]]; CInfinity at area+0x5D8;
// CInfinity::GetScreenCoordinates (win 140305AB0): screen = p - view(+0x60,+0x64) + rect(+0x78,+0x7C);
// ui = screen / zoom (float area+0xA14, divss at 1401A5BDD).
inline constexpr uint8_t kVisibleAreaBytes[] = {0x48, 0x8B, 0x88, 0xC8, 0x10, 0x00, 0x00, 0x0F, 0xB6, 0x81,
                                                0x78, 0xA1, 0x00, 0x00, 0x48, 0x8B, 0xBC, 0xC1, 0x80, 0xA1,
                                                0x00, 0x00, 0x48, 0x8D, 0x8F, 0xD8, 0x05, 0x00, 0x00};
inline constexpr uint8_t kScreenCoordsBytes[] = {0x44, 0x8B, 0x51, 0x78, 0x44, 0x2B, 0x51, 0x60,
                                                 0x44, 0x8B, 0x59, 0x7C, 0x45, 0x03, 0xD0, 0x49,
                                                 0xC1, 0xE8, 0x20, 0x44, 0x2B, 0x41, 0x64};
inline constexpr uint8_t kZoomBytes[] = {0xF3, 0x0F, 0x5E, 0x87, 0x14, 0x0A, 0x00, 0x00};
constexpr uint32_t kRvaVisibleArea = 0x1A5B94, kRvaScreenCoords = 0x305AB0, kRvaZoom = 0x1A5BDD;
constexpr uint32_t kOffGameVisibleIdx = 0xA178, kOffGameAreas = 0xA180, kOffAreaInfinity = 0x5D8;
constexpr uint32_t kOffAreaZoom = 0xA14, kOffInfView = 0x60, kOffInfRect = 0x78;

// Selection (turn-based: only the active party member is selected). CInfGame::OnPortraitLClick
// (lin 36E870, win 1402EA411): call UnselectAll; mov edx,[rbx+rbp*4+0A200h]; mov r8b,1; mov rcx,rbx;
// call SelectCharacter; then SelectToolbar. Selected flag: sprite +0x3914 (CGameSprite::Select win
// 1403E4CCD sets 1, Unselect 1403F0546 sets 0; lin this+3647*4 = +0x38FC).
inline constexpr uint8_t kPortraitSelectBytes[] = {0xE8, 0xAA, 0x19, 0x01, 0x00, 0x8B, 0x94, 0xAB, 0x00, 0xA2, 0x00, 0x00,
                                                   0x41, 0xB0, 0x01, 0x48, 0x8B, 0xCB, 0xE8, 0x08, 0x7E, 0x00, 0x00};
inline constexpr uint8_t kUnselectAllBytes[] = {0x40, 0x53, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC,
                                                0x30, 0x48, 0x8B, 0xD9, 0x48, 0x81, 0xC1, 0x90};
inline constexpr uint8_t kSelectCharBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
                                               0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55};
inline constexpr uint8_t kSelectToolbarBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x33, 0xC0,
                                                  0xC7, 0x81, 0x28, 0x9C, 0x00, 0x00, 0x64, 0x00};
inline constexpr uint8_t kSelectedFlagBytes[] = {0xC7, 0x81, 0x14, 0x39, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
constexpr uint32_t kRvaPortraitSelect = 0x2EA411, kRvaUnselectAll = 0x2FBDC0, kRvaSelectChar = 0x2F2230;
constexpr uint32_t kRvaSelectToolbar = 0x2F24D0, kRvaSelectedFlag = 0x3E4CCD, kOffSpriteSelected = 0x3914;

// Movement line, drawn by the engine in the world pass. CGameArea::RenderZoomed (lin 1BC300, win
// 14019F8A0) brackets `call CGameArea::Render` (14019F91C) with DrawBeginScaled/DrawEndScaled; we draw
// right after Render returns, in the same (zoom-scaled) space. Line: CVidMode::DrawLine2d (lin 6774B0,
// win 14049D870; called by CGameSprite::RenderActions at 1403DF80A): (vid, x1, y1, x2, y2, CRect* clip,
// uint color, float half_width, float start_trim, float end_trim, float dash(-1 solid), float gap,
// bool textured). Draw space = world - (view at area+0x698 - rect left at area+0x650) (RenderActions
// 1403DF4F3). CVidMode = [g_pBaldurChitin]+0x268 (RenderZoomed 14019F8DD).
inline constexpr uint8_t kAreaRenderCallBytes[] = {0xE8, 0xDF, 0xEB, 0xFF, 0xFF};
inline constexpr uint8_t kDrawLine2dBytes[] = {0x48, 0x8B, 0xC4, 0x44, 0x89, 0x48, 0x20, 0x44,
                                               0x89, 0x40, 0x18, 0x89, 0x50, 0x10, 0x55, 0x41};
inline constexpr uint8_t kRenderActionsLineBytes[] = {0xE8, 0x61, 0xE0, 0x0B, 0x00};
inline constexpr uint8_t kRenderViewBytes[] = {0x44, 0x8B, 0xA9, 0x98, 0x06, 0x00, 0x00, 0x44, 0x2B, 0xA9, 0x50,
                                               0x06, 0x00, 0x00, 0x44, 0x8B, 0xA1, 0x9C, 0x06, 0x00, 0x00};
inline constexpr uint8_t kVidModeBytes[] = {0x48, 0x8B, 0xB8, 0x68, 0x02, 0x00, 0x00};
constexpr uint32_t kRvaAreaRenderCall = 0x19F91C, kRvaDrawLine2d = 0x49D870, kRvaRenderActionsLine = 0x3DF80A;
constexpr uint32_t kRvaRenderView = 0x3DF4F3, kRvaVidMode = 0x19F8DD;
constexpr uint64_t kVaAreaRender = 0x14019E500ull;
constexpr uint32_t kOffChitinVidMode = 0x268, kOffAreaRenderView = 0x698, kOffAreaRect = 0x650;
// Walk cost of one search-map cell, the engine's way (CGameSprite::SetTarget lin 51E5F0, win
// 1403E9376..1403E93DE): personal space = (sprite+0x3958 & 4) ? byte sprite+0x3960 : anim vtbl+0xC0();
// terrain table = sprite vtbl+0x148(); CSearchBitmap at area+0xA38; GetCost(bitmap, CPoint* cell,
// terrain, ps, short* out, int 1) (lin 33F3D0, win 1402ADAB0); COST_IMPASSABLE byte 14062E58A (0xFF).
inline constexpr uint8_t kCostPsBytes[] = {0xF6, 0x87, 0x58, 0x39, 0x00, 0x00, 0x04, 0x74, 0x0A, 0x44,
                                           0x0F, 0xB6, 0xB7, 0x60, 0x39, 0x00, 0x00, 0xEB, 0x14, 0x48,
                                           0x8B, 0x8F, 0x50, 0x39, 0x00, 0x00, 0x48, 0x8B, 0x01, 0xFF,
                                           0x90, 0xC0, 0x00, 0x00, 0x00};
inline constexpr uint8_t kCostTerrainBytes[] = {0x48, 0x8B, 0x5F, 0x18, 0x48, 0x8B, 0xCF, 0x48, 0x8B,
                                                0x17, 0x48, 0x81, 0xC3, 0x38, 0x0A, 0x00, 0x00, 0xFF,
                                                0x92, 0x48, 0x01, 0x00, 0x00};
inline constexpr uint8_t kGetCostCallBytes[] = {0xE8, 0xD8, 0x46, 0xEC, 0xFF};
inline constexpr uint8_t kGetCostBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x57, 0x41,
                                            0x54, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC};
inline constexpr uint8_t kImpassableCmpBytes[] = {0x3A, 0x05, 0xAC, 0x51, 0x24, 0x00};
constexpr uint32_t kRvaCostPs = 0x3E9376, kRvaCostTerrain = 0x3E939D, kRvaGetCostCall = 0x3E93D3;
constexpr uint32_t kRvaGetCost = 0x2ADAB0, kRvaImpassableCmp = 0x3E93D8;
constexpr uint64_t kVaCostImpassable = 0x14062E58Aull;
constexpr uint32_t kOffSpritePsFlag = 0x3958, kOffSpritePs = 0x3960, kOffAreaSearch = 0xA38;
constexpr uint32_t kVtblAnimPersonalSpace = 0xC0, kVtblSpriteTerrain = 0x148;
// Cursor: CGameArea::OnMouseMove (lin 1B1410, win 14019D8A2) stores the mouse point at area+0x25C
// (CInfinity screen space); world = point - rect(+0x78) + view(+0x60) (CInfinity::GetWorldCoordinates).
inline constexpr uint8_t kMouseStoreBytes[] = {0x48, 0x8B, 0x0F, 0x45, 0x33, 0xE4, 0x48, 0x89, 0x8B, 0x5C, 0x02, 0x00, 0x00};
constexpr uint32_t kRvaMouseStore = 0x19D8A2, kOffAreaMouse = 0x25C;

// --- frame interpolation -------------------------------------------------------------------------
// EE advances the world exactly once per main-loop iteration (CTimerWorld::UpdateTime win 1402B3490 /
// lin 345D00: `++tick % 100000`, called once per iteration from CScreenWorld::AsynchronousUpdate at
// 14035432E) and caps the iteration to 1000/fps ms (CChitin::Update win 1404A6350: SDL_Delay at
// 1404A65A2). Draw rate therefore equals tick rate: walk motion lands as whole-pixel steps at the tick
// rate. We keep the tick rate (game speed stays exactly vanilla) and let the loop run faster, drawing
// each mover between where it was at the start of the current tick and where it is now.
// Loop hooks, CBaldurChitin vtable (win 140620308; lin _ZTV13CBaldurChitin E44D78):
//   slot 29 (+0xE8) CBaldurChitin::AsynchronousUpdate 14013FDF0 = the world update, called at
//     1404A6494 (`call qword [r10+0E8h]`, five zero args); slot 0 (+0x00) CBaldurChitin::SynchronousUpdate
//     140144250 = the draw pass (DrawClear, engine render slots, Flip), called at 1404A64D8.
inline constexpr uint8_t kSlotChitinAsyncBytes[] = {0xF0, 0xFD, 0x13, 0x40, 0x01, 0x00, 0x00, 0x00};
constexpr uint32_t kRvaSlotChitinAsync = 0x6203F0;
constexpr uint64_t kVaChitinAsyncUpdate = 0x14013FDF0ull;
inline constexpr uint8_t kDelayCallBytes[] = {0xE8, 0x29, 0xB5, 0x02, 0x00};  // -> SDL_Delay 1404D1AD0
constexpr uint32_t kRvaDelayCall = 0x4A65A2;
constexpr uint64_t kVaSDLDelay = 0x1404D1AD0ull;
// fps = EE's tick/frame rate (runtime value from Baldur.lua; CChitin::InitInstance 1404A667D clamps
// 10..90 else 40, CGameOptions::SetOption case 71 sets max(30, 10*(opt+3))). Only the read site is
// pinned: the global's value changes at runtime, so it is never checked.
inline constexpr uint8_t kFpsReadBytes[] = {0x8B, 0x05, 0x8A, 0x9A, 0x3B, 0x00};  // mov eax,[rip+dword_1407211FC]
constexpr uint32_t kRvaFpsRead = 0x36776C;                                        // CScreenWorld::TimerSynchronousUpdate
constexpr uint64_t kVaEngineFps = 0x1407211FCull;
// The world screen's draw pass also runs game logic (music fade, pending object lists):
// CScreenWorld::TimerSynchronousUpdate 140367740 calls CInfGame::SynchronousUpdate (1402F8C40) at
// 140367824. On a draw-only iteration that call must not happen.
inline constexpr uint8_t kGameSyncCallBytes[] = {0xE8, 0x17, 0x14, 0xF9, 0xFF};
constexpr uint32_t kRvaGameSyncCall = 0x367824;
constexpr uint64_t kVaGameSyncUpdate = 0x1402F8C40ull;
// Sub-tick walk position: the engine already carries the fraction a tick cannot spend, as Q10 fixed
// point per creature (CGameSprite::AIUpdateWalk lin 51FFA0 / win 1403B6D20):
//   step = (moveScale*dx << 10) / dist  -> sprite+0x4434/+0x4438 (writes 1403B7433 / 1403B7476;
//     moveScale = anim vtbl+0x88, see AnimPst::SetRunning 1403A4FA0)
//   acc += step                        -> sprite+0x442C/+0x4430 (reads 1403B76A4, add 1403B76B2)
//   pos = acc >> 10 (x); (acc_y*3)>>2>>10 (y: 12 px up/down per 16 px across) (1403B76E3, 1403B76F8)
//   the pos before the step is saved at sprite+0x4444/+0x444C (1403B76CE, 1403B76D5) and restored when
//   the step is blocked (1403B792E/1403B7935) — so acc is the creature's exact position while walking.
inline constexpr uint8_t kWalkStepXReadBytes[] = {0x48, 0x8B, 0x8E, 0x34, 0x44, 0x00, 0x00};  // mov rcx,[rsi+4434h]
inline constexpr uint8_t kWalkAccXReadBytes[] = {0x48, 0x8B, 0x9E, 0x2C, 0x44, 0x00, 0x00};   // mov rbx,[rsi+442Ch]
inline constexpr uint8_t kWalkAccYAddBytes[] = {0x01, 0x8E, 0x30, 0x44, 0x00, 0x00};          // add [rsi+4430h],ecx
inline constexpr uint8_t kWalkPosXBytes[] = {0x89, 0x7E, 0x0C};                              // mov [rsi+0Ch],edi
inline constexpr uint8_t kWalkPosYBytes[] = {0x44, 0x89, 0x5E, 0x10};                        // mov [rsi+10h],r11d
constexpr uint32_t kRvaWalkStepXRead = 0x3B769D, kRvaWalkAccXRead = 0x3B76A4, kRvaWalkAccYAdd = 0x3B76B2;
constexpr uint32_t kRvaWalkPosX = 0x3B76E3, kRvaWalkPosY = 0x3B76F8;
constexpr uint32_t kOffSpriteWalkAccX = 0x442C, kOffSpriteWalkAccY = 0x4430;
constexpr uint32_t kOffSpriteWalkStepX = 0x4434, kOffSpriteWalkStepY = 0x4438;
// Draw paths that read the object pos (+0x0C/+0x10) and can be wrapped around a temporary shift.
// Prologues are whole instructions up to the first RIP-relative one (InlineHook copy_len = len).
inline constexpr uint8_t kSpriteRenderBytes[] = {0x40, 0x55, 0x56, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24,
                                                 0x60, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xA0, 0x01, 0x00, 0x00};
constexpr uint32_t kRvaSpriteRender = 0x3DD820;  // CGameSprite::Render lin 562A10 (pos reads 0x3DD9EA/0x3DDA1F)
inline constexpr uint8_t kHealthBarRenderBytes[] = {0x4C, 0x8B, 0xDC, 0x55, 0x57, 0x49, 0x8D, 0xAB,
                                                    0x48, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xA8, 0x01, 0x00, 0x00};
constexpr uint32_t kRvaHealthBarRender = 0x3E1110;  // CGameSprite::RenderHealthBar (pos reads 0x3E1187/0x3E11C2)
inline constexpr uint8_t kMarkersRenderBytes[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48,
                                                  0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x41, 0x18};
constexpr uint32_t kRvaMarkersRender = 0x3E1A50;  // CGameSprite::RenderMarkers (pos reads 0x3E1A75/0x3E1AAA)
inline constexpr uint8_t kTemporalRenderBytes[] = {0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x57,
                                                   0x49, 0x8D, 0x6B, 0xA1, 0x48, 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00};
constexpr uint32_t kRvaTemporalRender = 0x3DF110;  // CGameTemporal::Render (projectiles draw through it; pos 0x3DF172/0x3DF15C)
// The engine's lock camera (the HUD's scroll-lock button; CScreenWorld+0xA58 = locked creature id, -1 off;
// set by CScreenWorld::LockScrollSelected lin 423090 / win 14035AA20). Once per WORLD TICK,
// CScreenWorld::AsynchronousUpdate (lin 4308B0 / win 140353B20, 140354EAF..140354FB1) sets
//   SetScrollDest(area+0x5D8, {pos.x - viewW/2, pos.y - viewH/2 - anim->vf[0x1C0]()})  call at 140354F8F
//   area+0x894 (scroll speed) = 0                                                         140354FB1
// and CGameArea::Render (win 14019E500: dest read 14019E5F2, speed read 14019E687) hands the dest to
// CInfinity::Scroll (win 1403084D0), which with speed 0 sets the view to it at once and clears it to -1.
// So the view jumps to the newest tick position while movers are drawn sliding behind it.
inline constexpr uint8_t kLockScrollDestCallBytes[] = {0xE8, 0xBC, 0x44, 0xFB, 0xFF};  // -> 140309450
inline constexpr uint8_t kLockIdReadBytes[] = {0x41, 0x8B, 0x8E, 0x58, 0x0A, 0x00, 0x00};  // mov ecx,[r14+0A58h]
inline constexpr uint8_t kLockSpeedStoreBytes[] = {0x66, 0x44, 0x89, 0xBA, 0x94, 0x08, 0x00, 0x00};
inline constexpr uint8_t kAreaDestReadBytes[] = {0x4D, 0x8B, 0x8E, 0x98, 0x08, 0x00, 0x00};  // mov r9,[r14+898h]
inline constexpr uint8_t kAreaSpeedReadBytes[] = {0x45, 0x0F, 0xB7, 0x86, 0x94, 0x08, 0x00, 0x00};
constexpr uint32_t kRvaLockScrollDestCall = 0x354F8F, kRvaLockIdRead = 0x354EAF, kRvaLockSpeedStore = 0x354FB1;
constexpr uint32_t kRvaAreaDestRead = 0x19E5F2, kRvaAreaSpeedRead = 0x19E687;
constexpr uint64_t kVaSetScrollDest = 0x140309450ull;
constexpr uint32_t kOffWorldScrollLock = 0xA58, kOffAreaScrollSpeed = 0x894;

#define PSTF_SYM(n, rva, b, ev) Symbol{n, rva, b, sizeof(b), ev}
// --- round clock for effects (turn-based: effects advance one round at their owner's turn) -------
// CGameSprite::ProcessEffectList (lin 4E21F0, win 1404231E0) runs the persistent effects (poison,
// regeneration, ...; HandlePersistantEffects inlined at 140425C36) only while the world timer runs:
//   140425C36 cmp byte [game+7824h],0 ; jz 140425CC3. We turn the cmp into a call to a stub that
// also answers "skip" (ZF=1) for combatants: their DoT/regen never advance on their own.
inline constexpr uint8_t kPersistGateBytes[] = {0x80, 0xB9, 0x24, 0x78, 0x00, 0x00, 0x00,
                                                0x0F, 0x84, 0x80, 0x00, 0x00, 0x00};
// CGameSprite::HandlePersistantEffects(int ticks) lin 4A46D0, win 140413BF0 (list sprite+0x4768, then
// CPersistantEffectListRegenerated at +0x18D8). CompressTime calls it with many ticks at once: each
// effect fires ticks/15 times (CPersistantEffectRegeneration::AIUpdate lin 2252D0 L41-97).
inline constexpr uint8_t kHandlePersistBytes[] = {0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                                  0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48};
// Natural regeneration (ProcessEffectList win 140423487): heal = (now - stamp) / rate, stamp at
// sprite+0x4B6C ("sub eax,[rsi+4B6Ch]"; "mov [rsi+4B6Ch],eax" at 140423522).
inline constexpr uint8_t kRegenStampBytes[] = {0x2B, 0x86, 0x6C, 0x4B, 0x00, 0x00};
// Effect lists checked by ProcessEffectList: timed +0x4718 (1404237D4), equipped +0x46C8 (1404237E4)
// (lin this+18176/18096: Windows +0x18). CPtrList: head +8, node {next, prev, data +0x10}.
inline constexpr uint8_t kEffListTimedBytes[] = {0x48, 0x8D, 0x8E, 0x18, 0x47, 0x00, 0x00};
inline constexpr uint8_t kEffListEquipBytes[] = {0x48, 0x8D, 0x8E, 0xC8, 0x46, 0x00, 0x00};
// CGameSprite::AIUpdate sets sprite+0x48BC = 1 before ProcessEffectList (1403B43D1): the per-second
// gate ("tick % 15 == id % 15 || [+0x48BC]", win 1404231E0 L134) lets it run at once.
inline constexpr uint8_t kForceEffectsBytes[] = {0xC7, 0x83, 0xBC, 0x48, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
// CGameEffect timing (ResolveEffect lin 1DFD20 / win 140209350): +0x24 duration type (4096 = absolute,
// 6/7/8 = delayed; both compared with the world time), +0x28 tick (140209436 / 14020944D).
inline constexpr uint8_t kEffTypeStoreBytes[] = {0xC7, 0x43, 0x24, 0x00, 0x10, 0x00, 0x00};
constexpr uint32_t kRvaPersistGate = 0x425C36, kRvaHandlePersist = 0x413BF0, kRvaRegenStamp = 0x423487;
constexpr uint32_t kRvaEffListTimed = 0x4237D4, kRvaEffListEquip = 0x4237E4, kRvaForceEffects = 0x3B43D1;
constexpr uint32_t kRvaEffTypeStore = 0x209436;
constexpr uint32_t kOffSpriteRegenStamp = 0x4B6C, kOffSpriteEffTimed = 0x4718, kOffSpriteEffEquip = 0x46C8;
constexpr uint32_t kOffSpriteForceEffects = 0x48BC, kOffEffType = 0x24, kOffEffTick = 0x28;
constexpr uint32_t kOffEffOpcode = 0x10, kOffEffSpriteSel = 0x4BBC;
// Held creatures still take effects at once: CGameSprite::ProcessEffectList(int) lin 4E21F0, win 1404231E0
// (its per-tick caller is ProcessAI, which held creatures skip). Its once-a-second gate compares the
// AI tick counter sprite+0x3A0 (`mov r8d,[rsi+3A0h]` 14042338C; lin this+232*4, ++ in ProcessAI L653).
inline constexpr uint8_t kProcessEffectListBytes[] = {0x40, 0x55, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x58,
                                                      0xFE, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xA8, 0x02};
inline constexpr uint8_t kAiTickReadBytes[] = {0x44, 0x8B, 0x86, 0xA0, 0x03, 0x00, 0x00};
constexpr uint32_t kRvaProcessEffectList = 0x4231E0, kRvaAiTickRead = 0x42338C, kOffSpriteAiTick = 0x3A0;
// SetInterrupt(FALSE) (ACTION 86, CGameSprite::ExecuteAction case 86, win 14040CDA9): bit 31 of sprite
// +0x560; scripts do not interrupt while it is set, ProcessAI clears it once no action is left (lin L511).
inline constexpr uint8_t kNoInterruptBytes[] = {0x8B, 0x8F, 0x60, 0x05, 0x00, 0x00};
constexpr uint32_t kRvaNoInterrupt = 0x40CDA9, kOffSpriteFlags = 0x560, kFlagNoInterrupt = 0x80000000u;

// --- combat presentation and pacing -------------------------------------------------------------
// Running: CGameAnimationTypePlanescapeCharacter::SetRunning(int run, int) lin 474370, win 1403A4FA0,
// vtable +0x1F0 (1406472A0); flag +0x1D10, move scale bytes +0x0A/+0x0B = (int) run scale double
// +0x1CF8 (1403A5022) or walk scale +0x1CF0. Other animations: move scale byte +0x0A (GetMoveScale).
inline constexpr uint8_t kSetRunningBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x89, 0x91,
                                               0x10, 0x1D, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x83};
inline constexpr uint8_t kRunScaleBytes[] = {0xF2, 0x0F, 0x10, 0x83, 0xF8, 0x1C, 0x00, 0x00};
inline constexpr uint8_t kSlotSetRunningBytes[] = {0xA0, 0x4F, 0x3A, 0x40, 0x01, 0x00, 0x00, 0x00};
constexpr uint32_t kRvaSetRunning = 0x3A4FA0, kRvaRunScale = 0x3A5022, kRvaSlotSetRunning = 0x6472A0;
constexpr uint32_t kVtblAnimSetRunning = 0x1F0, kOffAnimRunFlag = 0x1D10, kOffAnimRunScale = 0x1CF8;
constexpr uint32_t kOffAnimWalkScale = 0x1CF0, kOffAnimMoveScale = 0x0A;
// Battle music: CGameArea::AIUpdate (lin 1B8840 L1335-1368, win 14018E30B) counts area+0xFC4 down per
// tick; at 0 the battle song (state word +0xFC2 = 3) ends, above 0 it starts.
inline constexpr uint8_t kBattleCounterBytes[] = {0x83, 0xBF, 0xC4, 0x0F, 0x00, 0x00, 0x1E};
constexpr uint32_t kRvaBattleCounter = 0x18E30B, kOffAreaBattleCounter = 0xFC4;
// Attack roll: CGameSprite::Hit (lin 4C29F0, win 1404147B0) ends with FeedBack(this, 4, total, hit,
// roll, -1, ...) at 140415759 (lin L639). Swing calls Hit at 140433E8A/140433F90/14043422D.
inline constexpr uint8_t kHitFeedbackCallBytes[] = {0xE8, 0xC2, 0x93, 0xFF, 0xFF};
inline constexpr uint8_t kSwingHit1Bytes[] = {0xE8, 0x21, 0x09, 0xFE, 0xFF};
inline constexpr uint8_t kSwingHit2Bytes[] = {0xE8, 0x1B, 0x08, 0xFE, 0xFF};
inline constexpr uint8_t kSwingHit3Bytes[] = {0xE8, 0x7E, 0x05, 0xFE, 0xFF};
constexpr uint32_t kRvaHitFeedbackCall = 0x415759, kRvaSwingHit1 = 0x433E8A, kRvaSwingHit2 = 0x433F90;
constexpr uint32_t kRvaSwingHit3 = 0x43422D;
constexpr uint64_t kVaFeedBack = 0x14040EB20ull, kVaHit = 0x1404147B0ull;
// CGameSprite::FeedBack(uint type, int, int, int, uint, int, CString const&) lin 4BD4E0, win 14040EB20:
// prologue `mov [rsp+10h],rbx` (5 bytes, no RIP operand) for the debug log of every feedback type.
inline constexpr uint8_t kFeedBackBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54};
constexpr uint32_t kRvaFeedBack = 0x40EB20;
// THAC0 / AC words in the derived stats (Hit 140414888: sel ? +0xFF8 : +0x1CA8 THAC0, +0xFEE/+0x1C9E AC).
inline constexpr uint8_t kThac0AcBytes[] = {0xBA, 0x9E, 0x1C, 0x00, 0x00, 0xB8, 0xA8, 0x1C, 0x00, 0x00,
                                            0xB9, 0xF8, 0x0F, 0x00, 0x00};
constexpr uint32_t kRvaThac0Ac = 0x414888, kOffStatThac0 = 0x10, kOffStatAc = 0x06;
// Saving throws: CGameEffect::CheckSave(effect, sprite, u8& x6) lin 1FBED0, win 1401EC460; returns
// 0 = saved. Save flags effect +0x40 (bits 0..4), bonus +0x44 (lin L177/L226).
inline constexpr uint8_t kCheckSaveBytes[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55};
constexpr uint32_t kRvaCheckSave = 0x1EC460, kOffEffSaveFlags = 0x40, kOffEffSaveBonus = 0x44;
// Action bar: game:GetButtonArray() = CInfGame+0x79C0 (tolua 14046102E); 12 CButtonData of 0x2A0
// (PreRenderButton lin 3608B0: this + 672 * i); icon resref at +0 (GetQuickSlotData lin 357760).
inline constexpr uint8_t kButtonArrayBytes[] = {0x48, 0x8D, 0x97, 0xC0, 0x79, 0x00, 0x00};
constexpr uint32_t kRvaButtonArray = 0x46102E, kOffGameButtonArray = 0x79C0, kButtonDataSize = 0x2A0;
// CInfButtonArray::PostRenderButton (lin 3602F0, win 1402C8000): slot = array + 0x2A0 * i (1402C808C),
// icon CResRef at slot +0x128 (`lea rbx,[rsi+128h]` compared with "STONSPEL", 1402C82F5; count word
// +0x294 for CIcon::RenderIcon at 1402C82D9), disabled flag at slot +0x29C (1402C8097). Measured in
// game: weapons II232, spells SP093..SP096, items II175 at +0x128 + 0x2A0 * i.
inline constexpr uint8_t kBarIconBytes[] = {0x48, 0x8D, 0x9E, 0x28, 0x01, 0x00, 0x00};
inline constexpr uint8_t kBarDisabledBytes[] = {0x8B, 0x86, 0x9C, 0x02, 0x00, 0x00};
constexpr uint32_t kRvaBarIcon = 0x2C82F5, kRvaBarDisabled = 0x2C8097, kOffBarIcon = 0x128, kOffBarDisabled = 0x29C;
constexpr uint32_t kOffBarCount = 0x294;
// Projectiles are objects of type 0 (CGameObject +8) with the caster at +0x68 (DeliverEffects win
// 140282F3F `mov eax,[rbx+68h]`); every projectile class has AIUpdate at vtable slot 3 (+0x18, e.g.
// CProjectileAdder::AIUpdate 140261CA0 in vtable 140631510) and CGameObject::GetObjectType
// (140173050: movzx eax, byte [rcx+8]; ret) at slot 1: the check that a vtable met at runtime is one.
inline constexpr uint8_t kGetObjectTypeBytes[] = {0x0F, 0xB6, 0x41, 0x08, 0xC3};
constexpr uint32_t kRvaGetObjectType = 0x173050, kVtblObjAIUpdate = 0x18, kOffProjSource = 0x68;
// Visibility: CGameSprite::Render (lin 562A10, win 1403DD820) takes the area's CVisibilityMap at
// area+0xB88 (1403DD9D5 `lea rbx,[rdx+0B88h]`: +0 tile words, +8 width in tiles) and draws a sprite
// only if the tile word under it has EXPLORED_FLAG 0x8000 (rdata 14063976C) and any bit of
// VISIBILITY_MASK 0x7FFF (140639770): one bit per character slot (GetCharacterId: slots 0..14), tiles
// 32 x 32 px (rdata 140639772/3). Area size in px: area+0x688 / +0x68C.
inline constexpr uint8_t kVisMapLeaBytes[] = {0x48, 0x8D, 0x9A, 0x88, 0x0B, 0x00, 0x00};
inline constexpr uint8_t kVisMaskBytes[] = {0x00, 0x80, 0x14, 0x18, 0xFF, 0x7F, 0x20, 0x20};
constexpr uint32_t kRvaVisMapLea = 0x3DD9D5, kRvaVisMasks = 0x63976C, kOffAreaVisMap = 0xB88, kOffAreaWidthPx = 0x688,
                   kOffAreaHeightPx = 0x68C;
constexpr uint16_t kVisExplored = 0x8000, kVisCombat = 0x4000;  // slot 14: no party member uses it
constexpr int kVisTile = 32;
// Stopping a walk: CMessageDropPath::Run (lin 293C30, win 1402355D0) drops the path and, if the
// sprite's sequence (+0x4428) is SEQ_WALK (10, rdata 1406490FE), sets SEQ_READY (7, rdata 1406490F6)
// through CGameSprite::SetSequence (lin 50A270, win 1403E8800); else the walk cycle keeps playing in place.
inline constexpr uint8_t kDropPathSeqBytes[] = {0x0F, 0xB6, 0x05, 0xEA, 0x3A, 0x41, 0x00, 0x66, 0x39, 0x81, 0x28, 0x44,
                                                0x00, 0x00, 0x75, 0x11, 0x0F, 0xB6, 0x15, 0xD2, 0x3A, 0x41, 0x00};
inline constexpr uint8_t kSetSequenceBytes[] = {0x40, 0x55, 0x53, 0x56, 0x41, 0x54, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC};
constexpr uint32_t kRvaDropPathSeq = 0x23560D, kRvaSetSequence = 0x3E8800, kOffSpriteSequence = 0x4428;
constexpr uint16_t kSeqReady = 7, kSeqWalk = 10;
// Item use: the action handlers CGameSprite::UseItem(target) (lin 4D5180, win 140436690) and
// UseItemPoint() (lin 4D6CB0, win 140437B90) run every tick the action is current; a quick item can
// start and finish between two frames. Prologues: `mov [rsp+18h|10h], rbx` (5 bytes) first.
inline constexpr uint8_t kUseItemBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55};
inline constexpr uint8_t kUseItemPointBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
constexpr uint32_t kRvaUseItem = 0x436690, kRvaUseItemPoint = 0x437B90;
// Cursor mode: CInfGame::SetState(state, arg) (lin 36DA40, win 1402F3240) stores the state word at
// CInfGame+0x78B0 (1402F327A `mov [rbx+78B0h], si`): 0 = normal, other = aiming a spell/ability/item.
inline constexpr uint8_t kGameStateStoreBytes[] = {0x66, 0x89, 0xB3, 0xB0, 0x78, 0x00, 0x00};
constexpr uint32_t kRvaGameStateStore = 0x2F327A, kOffGameState = 0x78B0;
// Lingering spells: CProjectileArea::AIUpdate (lin 308680, win 140262180: clouds, Pacify, Tasha's) and
// CPsSpellSwarmCurse::AIUpdate (lin 2F8E30, win 1402783F0) fly until "arrived" (+0x3E8, both test it
// first: 140262222 / 140278422 `cmp dword [reg+3E8h], 0`), then count down one tick per AIUpdate and
// pulse their effects (area: every +0x3EC ticks; swarm: every 75 ticks of its duration counter).
inline constexpr uint8_t kAreaAIBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB7, 0x41, 0x60, 0x48, 0x8B, 0xD9};
inline constexpr uint8_t kSwarmAIBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89,
                                            0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
inline constexpr uint8_t kAreaArrivedBytes[] = {0x83, 0xBB, 0xE8, 0x03, 0x00, 0x00, 0x00};
inline constexpr uint8_t kSwarmArrivedBytes[] = {0x83, 0xB9, 0xE8, 0x03, 0x00, 0x00, 0x00};
constexpr uint32_t kRvaAreaAI = 0x262180, kRvaSwarmAI = 0x2783F0, kRvaAreaArrived = 0x262222,
                   kRvaSwarmArrived = 0x278422, kOffProjArrived = 0x3E8;

// Camera: CInfGame::CenterOnGroupLeader (lin 36D7F0, win 1402D31C0) ends with
// jmp CInfinity::SetViewPosition(inf = area+0x5D8, x - w/2, y - h/2, 1) (lin 3AE2B0, win 140309630).
inline constexpr uint8_t kSetViewPosBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x44,
                                               0x89, 0x40, 0x18, 0x89, 0x50, 0x10, 0x57, 0x48};
inline constexpr uint8_t kCenterLeaderJmpBytes[] = {0xE9, 0xC5, 0x63, 0x03, 0x00};
constexpr uint32_t kRvaSetViewPos = 0x309630, kRvaCenterLeaderJmp = 0x2D3266;

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
    PSTF_SYM("CanRestParty.objArray", kRvaObjArrayLea, kObjArrayLeaBytes, "lin 37AD20 / win 1402D2910"),
    PSTF_SYM("CanRestParty.objMax", kRvaObjMaxCmp, kObjMaxCmpBytes, "win 1402D2965"),
    PSTF_SYM("CanRestParty.objNext", kRvaObjNextCmp, kObjNextCmpBytes, "win 1402D297B"),
    PSTF_SYM("CanRestParty.partyCount", kRvaPartyCount, kPartyCountBytes, "win 1402D2917"),
    PSTF_SYM("CanRestParty.partyIds", kRvaPartyIds, kPartyIdsBytes, "win 1402D294C"),
    PSTF_SYM("CanRestParty.spriteArea", kRvaSpriteArea, kSpriteAreaBytes, "win 1402D29AA"),
    PSTF_SYM("CanRestParty.spritePos", kRvaSpritePos, kSpritePosBytes, "win 1402D29CF"),
    PSTF_SYM("CGameSprite::Animate", kRvaAnimate, kAnimateBytes, "lin 4A2030 / win 1403FB520"),
    PSTF_SYM("Enemy.ea", kRvaEnemyEa, kEnemyEaBytes, "lin 4AEA80 / win 140408B0A"),
    PSTF_SYM("Enemy.id", kRvaEnemyId, kEnemyIdBytes, "win 140408BED"),
    PSTF_SYM("GetNextAction.queueCount", kRvaQueueCount, kQueueCountBytes, "lin 183B00 / win 140172D31"),
    PSTF_SYM("ClearAI.curAction", kRvaClearAICurAction, kClearAICurActionBytes, "win 140404BC6"),
    PSTF_SYM("CGameSprite::ProcessAI", kRvaProcessAI, kProcessAIBytes, "lin 4FA830 / win 140421830"),
    PSTF_SYM("AIUpdate.walk1", kRvaWalkCall1, kWalkCall1Bytes, "lin 53A510 / win 1403B6211"),
    PSTF_SYM("AIUpdate.walk2", kRvaWalkCall2, kWalkCall2Bytes, "win 1403B677F"),
    PSTF_SYM("CGameSprite::ClearAI", kRvaClearAI, kClearAIBytes, "lin 4A5320 / win 140404B90"),
    PSTF_SYM("CGameSprite::DropPath", kRvaDropPath, kDropPathBytes, "lin 505F80 / win 1403C4280"),
    PSTF_SYM("CGameSprite::Enemy", kRvaEnemy, kEnemyBytes, "lin 4AEA80 / win 140408B00"),
    PSTF_SYM("FloatingHP.hp", kRvaHp, kHpBytes, "lin 571E90 / win 14043A900"),
    PSTF_SYM("luaL_loadstring", kRvaLuaLoadString, kLuaLoadStringBytes, "lin 6A58A0 / win 1400E7380"),
    PSTF_SYM("uiLoadMenu.copyText", kRvaMenuCopyCall, kMenuCopyCallBytes, "lin 581790 / win 140459EC3"),
    PSTF_SYM("CString::CString(char const*)", kRvaCStringCtor, kCStringCtorBytes,
             "lin 69BED0 / win 140484330"),
    PSTF_SYM("AnimPst.ctor.vtbl", kRvaAnimVtblLea, kAnimVtblLeaBytes, "lin 48FCB0 / win 14038042C"),
    PSTF_SYM("AnimPst.ctor.bestiary", kRvaAnimBestiary, kAnimBestiaryBytes, "win 140380487"),
    PSTF_SYM("GetTargetShareType.anim", kRvaSpriteAnim, kSpriteAnimBytes, "lin 184490 / win 14017358E"),
    PSTF_SYM("FloatingHP.maxhp", kRvaMaxHp, kMaxHpBytes, "lin 571E90 / win 14043A908"),
    PSTF_SYM("GetNameRef.strref", kRvaNameRef, kNameRefBytes, "lin 521CB0 / win 1403C93F4"),
    PSTF_SYM("AIUpdateWalk.path", kRvaPathPtr, kPathPtrBytes, "lin 51FFA0 / win 1403B718E"),
    PSTF_SYM("AIUpdateWalk.pathIndex", kRvaPathIndex, kPathIndexBytes, "win 1403B71FA"),
    PSTF_SYM("AIUpdateWalk.gridX", kRvaGridX, kGridXBytes, "win 1403B72E8"),
    PSTF_SYM("AIUpdateWalk.gridY", kRvaGridY, kGridYBytes, "win 1403B72EF"),
    PSTF_SYM("AIUpdateWalk.gridSx", kRvaGridSx, kGridSxBytes, "win 1403B72FF"),
    PSTF_SYM("AreaNotes.visibleArea", kRvaVisibleArea, kVisibleAreaBytes, "lin 1C1920 / win 1401A5B94"),
    PSTF_SYM("CInfinity::GetScreenCoordinates", kRvaScreenCoords, kScreenCoordsBytes,
             "lin 3A8110 / win 140305AB0"),
    PSTF_SYM("AreaNotes.zoom", kRvaZoom, kZoomBytes, "win 1401A5BDD"),
    PSTF_SYM("OnPortraitLClick.select", kRvaPortraitSelect, kPortraitSelectBytes, "lin 36E870 / win 1402EA411"),
    PSTF_SYM("CInfGame::UnselectAll", kRvaUnselectAll, kUnselectAllBytes, "lin 36A0D0 / win 1402FBDC0"),
    PSTF_SYM("CInfGame::SelectCharacter", kRvaSelectChar, kSelectCharBytes, "lin 36A1A0 / win 1402F2230"),
    PSTF_SYM("CInfGame::SelectToolbar", kRvaSelectToolbar, kSelectToolbarBytes, "lin 36A4F0 / win 1402F24D0"),
    PSTF_SYM("CGameSprite::Select.flag", kRvaSelectedFlag, kSelectedFlagBytes, "lin 509460 / win 1403E4CCD"),
    PSTF_SYM("RenderZoomed.render", kRvaAreaRenderCall, kAreaRenderCallBytes, "lin 1BC300 / win 14019F91C"),
    PSTF_SYM("RenderZoomed.vidmode", kRvaVidMode, kVidModeBytes, "win 14019F8DD"),
    PSTF_SYM("CVidMode::DrawLine2d", kRvaDrawLine2d, kDrawLine2dBytes, "lin 6774B0 / win 14049D870"),
    PSTF_SYM("RenderActions.line", kRvaRenderActionsLine, kRenderActionsLineBytes, "lin 5610A0 / win 1403DF80A"),
    PSTF_SYM("RenderActions.view", kRvaRenderView, kRenderViewBytes, "win 1403DF4F3"),
    PSTF_SYM("SetTarget.personalSpace", kRvaCostPs, kCostPsBytes, "lin 51E5F0 / win 1403E9376"),
    PSTF_SYM("SetTarget.terrain", kRvaCostTerrain, kCostTerrainBytes, "win 1403E939D"),
    PSTF_SYM("SetTarget.getCost", kRvaGetCostCall, kGetCostCallBytes, "win 1403E93D3"),
    PSTF_SYM("CSearchBitmap::GetCost", kRvaGetCost, kGetCostBytes, "lin 33F3D0 / win 1402ADAB0"),
    PSTF_SYM("SetTarget.impassable", kRvaImpassableCmp, kImpassableCmpBytes, "win 1403E93D8"),
    PSTF_SYM("OnMouseMove.store", kRvaMouseStore, kMouseStoreBytes, "lin 1B1410 / win 14019D8A2"),
    PSTF_SYM("ProcessEffectList.persistGate", kRvaPersistGate, kPersistGateBytes, "lin 4E21F0 / win 140425C36"),
    PSTF_SYM("CGameSprite::ProcessEffectList", kRvaProcessEffectList, kProcessEffectListBytes,
             "lin 4E21F0 / win 1404231E0"),
    PSTF_SYM("ProcessEffectList.aiTick", kRvaAiTickRead, kAiTickReadBytes, "win 14042338C"),
    PSTF_SYM("ExecuteAction.SetInterrupt", kRvaNoInterrupt, kNoInterruptBytes, "lin 4FDCC0 / win 14040CDA9"),
    PSTF_SYM("CGameSprite::HandlePersistantEffects", kRvaHandlePersist, kHandlePersistBytes,
             "lin 4A46D0 / win 140413BF0"),
    PSTF_SYM("ProcessEffectList.regenStamp", kRvaRegenStamp, kRegenStampBytes, "win 140423487"),
    PSTF_SYM("ProcessEffectList.timedList", kRvaEffListTimed, kEffListTimedBytes, "win 1404237D4"),
    PSTF_SYM("ProcessEffectList.equipList", kRvaEffListEquip, kEffListEquipBytes, "win 1404237E4"),
    PSTF_SYM("AIUpdate.forceEffects", kRvaForceEffects, kForceEffectsBytes, "lin 53A510 / win 1403B43D1"),
    PSTF_SYM("ResolveEffect.type", kRvaEffTypeStore, kEffTypeStoreBytes, "lin 1DFD20 / win 140209436"),
    PSTF_SYM("AnimPst::SetRunning", kRvaSetRunning, kSetRunningBytes, "lin 474370 / win 1403A4FA0"),
    PSTF_SYM("AnimPst::SetRunning.runScale", kRvaRunScale, kRunScaleBytes, "win 1403A5022"),
    PSTF_SYM("CGameArea::AIUpdate.battleCounter", kRvaBattleCounter, kBattleCounterBytes,
             "lin 1B8840 / win 14018E30B"),
    PSTF_SYM("Hit.feedback4", kRvaHitFeedbackCall, kHitFeedbackCallBytes, "lin 4C29F0 L639 / win 140415759"),
    PSTF_SYM("CGameSprite::FeedBack", kRvaFeedBack, kFeedBackBytes, "lin 4BD4E0 / win 14040EB20"),
    PSTF_SYM("Swing.hit1", kRvaSwingHit1, kSwingHit1Bytes, "lin 4EAC10 / win 140433E8A"),
    PSTF_SYM("Swing.hit2", kRvaSwingHit2, kSwingHit2Bytes, "win 140433F90"),
    PSTF_SYM("Swing.hit3", kRvaSwingHit3, kSwingHit3Bytes, "win 14043422D"),
    PSTF_SYM("Hit.thac0ac", kRvaThac0Ac, kThac0AcBytes, "lin 4C29F0 / win 140414888"),
    PSTF_SYM("CGameEffect::CheckSave", kRvaCheckSave, kCheckSaveBytes, "lin 1FBED0 / win 1401EC460"),
    PSTF_SYM("tolua.GetButtonArray", kRvaButtonArray, kButtonArrayBytes, "win 14046102E"),
    PSTF_SYM("PostRenderButton.icon", kRvaBarIcon, kBarIconBytes, "lin 3602F0 / win 1402C82F5"),
    PSTF_SYM("PostRenderButton.disabled", kRvaBarDisabled, kBarDisabledBytes, "win 1402C8097"),
    PSTF_SYM("CGameObject::GetObjectType", kRvaGetObjectType, kGetObjectTypeBytes, "lin 176FB0 / win 140173050"),
    PSTF_SYM("CGameSprite::Render.vismap", kRvaVisMapLea, kVisMapLeaBytes, "lin 562A10 / win 1403DD9D5"),
    PSTF_SYM("CMessageDropPath::Run.seq", kRvaDropPathSeq, kDropPathSeqBytes, "lin 293C30 / win 14023560D"),
    PSTF_SYM("CGameSprite::SetSequence", kRvaSetSequence, kSetSequenceBytes, "lin 50A270 / win 1403E8800"),
    PSTF_SYM("CGameSprite::UseItem", kRvaUseItem, kUseItemBytes, "lin 4D5180 / win 140436690"),
    PSTF_SYM("CGameSprite::UseItemPoint", kRvaUseItemPoint, kUseItemPointBytes, "lin 4D6CB0 / win 140437B90"),
    PSTF_SYM("CInfGame::SetState.store", kRvaGameStateStore, kGameStateStoreBytes, "lin 36DA40 / win 1402F327A"),
    PSTF_SYM("CProjectileArea::AIUpdate", kRvaAreaAI, kAreaAIBytes, "lin 308680 / win 140262180"),
    PSTF_SYM("CPsSpellSwarmCurse::AIUpdate", kRvaSwarmAI, kSwarmAIBytes, "lin 2F8E30 / win 1402783F0"),
    PSTF_SYM("CProjectileArea.arrived", kRvaAreaArrived, kAreaArrivedBytes, "win 140262222"),
    PSTF_SYM("CPsSpellSwarmCurse.arrived", kRvaSwarmArrived, kSwarmArrivedBytes, "win 140278422"),
    PSTF_SYM("CInfinity::SetViewPosition", kRvaSetViewPos, kSetViewPosBytes, "lin 3AE2B0 / win 140309630"),
    PSTF_SYM("CenterOnGroupLeader.jmp", kRvaCenterLeaderJmp, kCenterLeaderJmpBytes, "lin 36D7F0 / win 1402D3266"),
    PSTF_SYM("Chitin.Update.delay", kRvaDelayCall, kDelayCallBytes, "lin 6603A0 / win 1404A65A2"),
    PSTF_SYM("TimerSyncUpdate.gamesync", kRvaGameSyncCall, kGameSyncCallBytes, "lin 42D330 / win 140367824"),
    PSTF_SYM("TimerSyncUpdate.fps", kRvaFpsRead, kFpsReadBytes, "lin 42D330 / win 14036776C"),
    PSTF_SYM("AIUpdateWalk.walkStepX", kRvaWalkStepXRead, kWalkStepXReadBytes, "lin 51FFA0 / win 1403B769D"),
    PSTF_SYM("AIUpdateWalk.walkAccX", kRvaWalkAccXRead, kWalkAccXReadBytes, "lin 51FFA0 / win 1403B76A4"),
    PSTF_SYM("AIUpdateWalk.walkAccY", kRvaWalkAccYAdd, kWalkAccYAddBytes, "lin 51FFA0 / win 1403B76B2"),
    PSTF_SYM("AIUpdateWalk.posX", kRvaWalkPosX, kWalkPosXBytes, "lin 51FFA0 / win 1403B76E3"),
    PSTF_SYM("AIUpdateWalk.posY", kRvaWalkPosY, kWalkPosYBytes, "lin 51FFA0 / win 1403B76F8"),
    PSTF_SYM("CGameSprite::Render", kRvaSpriteRender, kSpriteRenderBytes, "lin 562A10 / win 1403DD820"),
    PSTF_SYM("CGameSprite::RenderHealthBar", kRvaHealthBarRender, kHealthBarRenderBytes,
             "lin 5074D0 / win 1403E1110"),
    PSTF_SYM("CGameSprite::RenderMarkers", kRvaMarkersRender, kMarkersRenderBytes, "lin 508150 / win 1403E1A50"),
    PSTF_SYM("CGameTemporal::Render", kRvaTemporalRender, kTemporalRenderBytes, "lin 504110 / win 1403DF110"),
    PSTF_SYM("WorldAsync.lockScrollDest", kRvaLockScrollDestCall, kLockScrollDestCallBytes,
             "lin 4308B0 / win 140354F8F"),
    PSTF_SYM("WorldAsync.lockId", kRvaLockIdRead, kLockIdReadBytes, "lin 4308B0 / win 140354EAF"),
    PSTF_SYM("WorldAsync.lockSpeed", kRvaLockSpeedStore, kLockSpeedStoreBytes, "lin 4308B0 / win 140354FB1"),
    PSTF_SYM("CGameArea::Render.scrollDest", kRvaAreaDestRead, kAreaDestReadBytes, "lin 1BB560 / win 14019E5F2"),
    PSTF_SYM("CGameArea::Render.scrollSpeed", kRvaAreaSpeedRead, kAreaSpeedReadBytes, "lin 1BB560 / win 14019E687"),
};

// Data-section entries (vtable slots): checked like kAll but not required to be executable.
inline constexpr Symbol kData[] = {
    PSTF_SYM("CGameSprite.vtbl.ProcessAI", kRvaSlotProcessAI, kSlotProcessAIBytes,
             "vtable win 140649328 slot 35 / lin E57298 slot 36"),
    PSTF_SYM("CVisibilityMap flags/masks/square", kRvaVisMasks, kVisMaskBytes, "rdata win 14063976C"),
    PSTF_SYM("AnimPst.vtbl.SetRunning", kRvaSlotSetRunning, kSlotSetRunningBytes,
             "vtable win 1406470B0 +0x1F0 / lin slot 63"),
    PSTF_SYM("CBaldurChitin.vtbl.AsyncUpdate", kRvaSlotChitinAsync, kSlotChitinAsyncBytes,
             "vtable win 140620308 slot 29 / lin E44D78 slot 29"),
};
#undef PSTF_SYM

}  // namespace pstf::sym
