// SPDX-License-Identifier: GPL-2.0-or-later
// 8-member party: the engine adapter (CInfGame party arrays moved and grown to 8; bounds patched).
// Layout and the generated site table: engine/party_layout.h, engine/party_sites.h.
#pragma once
#include <cstdint>
#include <string>

namespace pstf::engine {

// Applies every site and patch (all-or-nothing, before CBaldurChitin::Init allocates CInfGame).
// Must run from PSTF_Init, before any other module checks or patches its symbols.
bool InstallParty8();

// Where the party arrays live now (vanilla offsets until InstallParty8 succeeded).
struct PartyLayout {
  uint32_t fixed_off, order_off, count_off;
  int max;  // 6 or 8
};
const PartyLayout& Party();

// "max=8 count=N order=a,b,.. fixed=a,b,.." (ids, -1 = empty) for probes; "" without a game.
// Game thread only.
std::string PartyDump();

// Script names of the party in order ("NAMELESS,MORTE,..."; CGameSprite +0x478 char[32], the name
// CPlanescape::PSIsCharacterInParty compares, lin 5A4780 / win AddCharacterToParty 1402D0C68).
std::string PartyNames();

// Evaluates a status trigger the engine's way (CGameAIBase::EvaluateStatusTrigger) for creature `who`:
// trigger id (TRIGGER.IDS), first integer parameter, optional object name and first string parameter
// (CAITrigger +0x30 CString). -1 if unavailable.
// For tests (NumInParty, NumInPartyGT, InParty, InPartySlot, ...). Game thread only.
int EvalTrigger(int32_t who, uint16_t id, int32_t param1, const char* object_name, const char* string1 = "");

// CInfGame::CanSaveGame(reason, 0, 0, 1) as the quick save asks it: 1/0, *reason = refusal strref
// (-1 = none). -1 if unavailable. Game thread only.
int CanSaveGame(uint32_t* reason);

// CAIGroup::GetOffsets(selection group, formation, angle, party=1) through its hooked entry, as area
// moves call it: "selected=N pts=x,y;..." (8 points, x1000 grid units). "" without a game. Probe;
// game thread only.
std::string FormationPoints(int formation, int angle);

// CInfGame::UnselectAll (probe: a selection smaller than the party). Game thread only.
bool UnselectAll();

// The per-party-member caches the party layout grew in other screens, for probes:
// "map count=N ids=a,b,.. mask=X inv sel=S piles=a,b,.. flags=a,b,.." (8 entries each when the layout is
// on, 6 otherwise). "" without the screens. Game thread only.
std::string ScreenCaches();

// True when the 8-slot layout is active.
inline bool Party8() { return Party().max == 8; }

}  // namespace pstf::engine
