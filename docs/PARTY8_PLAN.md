# 8-member party: plan, implementation and findings

Goal: up to 8 party members (TNO + all 7 companions), with every script, dialogue, opcode and screen
behaving as for 6. Toggle: `[party] enabled=1`, `size=8` (the HUD reads these too and gives the BG3
portrait column 8 slots; with them off nothing is patched). Single player only (multiplayer paths keep
their 6-slot arrays; see "Not covered").

## Engine layout (Windows 3.2.2, confirmed on Linux)

- `CInfGame +0xA1E8` i32[6]: fixed-order slots (portrait numbers), `+0xA200` i32[6]: party order,
  `+0xA218` i16: count, `+0xA220` CPtrList right after. No room to grow in place.
  Evidence: lin `CInfGame::AddCharacterToParty` 375A70 (this+10362.., this+10368.., word 20748),
  win 1402D0A70.
- `CInfGame` is allocated by `CBaldurChitin::Init` with `operator new(0xD140)` (win 140141255), freed with
  a sized delete (win 14013F664). Only one instance (one ctor caller).

## Implementation (done)

- **Relocation.** The allocation grows to 0xD180; both arrays move past the old end: fixed i32[8] at
  `+0xD140`, order i32[8] at `+0xD160` (so "end of fixed" == "start of order", as before, and pointer
  loops that end at the order array stay right). `tools/party_sites.py` scans every instruction of the
  Windows dump (capstone) whose displacement or immediate is in `[0xA1E8, 0xA218)` and rewrites its
  4-byte field in place: 467 sites in 277 functions, generated into `src/engine/party_sites.h` with the
  expected instruction bytes. All of them were checked to address CInfGame (they sit next to
  `g_pBaldurChitin->m_pObjectGame` (+0x10C8) loads or the count, incl. the misnamed trigger evaluator
  `__crt_strtox::multiply_by_power_of_ten` = `CGameAIBase::EvaluateStatusTrigger`). The dump has holes
  (code IDA left undefined); a linear sweep of all 17 372 holes found no further array references.
- **Bounds** (`tools/party_patches.tsv`, 100 hand-reviewed patches, merged into the same header):
  every inlined `GetCharacterSlot` guard (`idx < 6` before an array read, 43), every party-slot loop
  (`i < 6`, `mov r, 6` counters, the PST leave-area loops that start at slot 5 -> 7), the party-full test
  in `AddCharacterToParty`, `CMultiplayerSettings::MAX_CHARACTERS` (rdata byte, 6 -> 8; read by
  TransformPartyItem, ~CGameArea, StartStore, DeleteArea, LeaveAreaLuaPanic, EnterArea, PST leave-area).
- **Rewritten in place:** `RemoveCharacterFromParty`'s unrolled clear of fixed slots 0..5 becomes a loop
  over 8 in the same 90 bytes; its `order[count-1] = -1` store (disp A1FC = order-4) gets D15C.
- **Hooks** (`src/engine/party.cpp`): CInfGame ctor call (new slots = -1), `DestroyGame` (its three
  qword stores clear fixed 0..5; we clear 6..7), `CMultiplayerSettings::GetCharacterControlledByPlayer`
  and the two byte getters (`GetCharacterReady`/`Status`, rewritten in their 16 bytes): slots 6..7 read
  slot 0 (single player: one player controls everyone).
- **CInfGame::Marshal** (GAM writer, lin 365550 / win 1402E78F0) keeps one `CGameSaveCharacter` per party
  member in a stack array of 6 x 0x168 bytes right under the GS cookie: with 7+ members the save ended in
  `FAST_FAIL_STACK_COOKIE_CHECK_FAILURE` (the game just closes). The frame grows by 0x2D0 (`lea rbp`,
  `sub/add rsp`, cookie store/check, xmm6/rbx slots) together with its unwind codes and the
  `__GSHandlerCheck` cookie offset in rdata; entries 6..7 are zeroed with entry 5 (memset call redirect).
  Verified: party8.lua saves with 8, party8_load.lua loads it back.
- **Save portraits.** `CInfGame::SaveGame` (and the save-screen path in `SynchronousUpdate`) write
  `PORTRT<slot>.BMP` per party member through `dimmGetResObject(ref, BMP, create=0)` (lin 3628A0): only
  resources that exist. The game ships PORTRT0..5 (3128-byte placeholders); with 7 members the save
  faulted in `CRes::Write(null)` (called from SaveGame+0xC58). We ship PORTRT6/7.BMP (copies of
  PORTRT0) in `data/override` (deploy copies them). `BuildGameSlot` reads PORTRT0..5 for the save
  list: members 7/8 have no picture there (presentation).
- **Formation points** (`CAIGroup::GetOffsets(group, formation, angle, party=1)`, lin 5D92D0 / win
  140125720): area moves (`CGameSprite::LeaveAreaName`, `CScreenWorldMap::EnterArea`,
  `CGameSprite::CompressTime`, the Lua leave-area entries) read point[slot] for every party/portrait
  slot, but the buffer holds max(selected, 7) points: member 8 read past the heap block whenever fewer
  than 8 were selected (a garbage arrival point). The allocation is doubled (patch at 14012577F) and a
  prologue hook fills point 7 = point 6 + (point 6 - point 4) (same side of the formation); the engine
  then finds the nearest open square for it. With all 8 selected the engine's own rule for groups
  past 7 applies (point 7 = member 8's place relative to the leader).
- **Inventory ground-pile cache** (`CScreenInventory`, lin FetchGroundPile 3F60B0 / win 140331F30): one
  ground pile id (i32[6] at +0x6F0) and a "fetched" flag (i32[6] at +0x708) per portrait. Portraits 6/7
  read past them (pile[6] = flag[0], flag[6] = +0x720): a garbage pile id left `MapButtonIdToItemInfo`'s
  CItem* uninitialised and member 7/8's inventory crashed in `GetUseButtonMode` (from
  UpdateLuaEquipment on the portrait click). The allocation grows 0x7D8 -> 0x818 (Init 140141025, sized
  delete 140330916); pile i32[8] at +0x7D8, flag i32[8] at +0x7F8: FetchGroundPile's five accesses, the
  flush loop in CancelEngine (start, 6 -> 8, pile = flag - 0x20), and the flag clears in EngineActivated /
  ResetGroundPile (rewritten in place as two 16-byte movups). GetMaxGroundPage is FetchGroundPile on Windows.
- **Area map markers** (`CScreenMap`, lin SetMap 4012C0 / RenderAreaMap 400DF0 / TimerAsynchronousUpdate
  401DE0; win 14033C6E0 / 14033C260 (ported as RenderFogOfWar) / 14033CAE0): 12-byte entries (pos, id) at
  +0x918, addressed as (i + 0xC2) * 12. The allocation grows 0x980 -> 0x9E8 and the 8 entries sit at
  +0x984 = 0xCB * 12; SetMap's loop runs over 8 order slots; the timer refreshes marker `tick & 7`
  (was `tick % 6`). The count (+0x960) and dirty mask (+0x964, bit = portrait) stay in place.
- **Other modules' manifest checks**: `CheckSymbol` accepts bytes the party layout retargeted
  (`SetManifestRewrite(party::RewriteBytes)`); the tbc selection lock (`OnPortraitLClick.select`,
  win 1402EA411, holds a retargeted `mov edx,[..+0A200h]`) was turned off by the mismatch before.
- **Own code** reads the party through `engine::Party()` (layout + max); `CanRestParty.partyIds` is
  allowed to differ from the manifest when the layout is moved (`pstf_verify` fails on any other
  manifest symbol that overlaps a site/patch).

## Probes and tests

- Lua (`[party] enabled=1`): `PSTF_Party()` (max, count, order ids, fixed ids), `PSTF_PartyNames()`
  (script names, CGameSprite +0x478), `PSTF_Trigger(who, id, p1[, objectName[, string1[, quiet]]])`
  (CGameAIBase::EvaluateStatusTrigger on a real CAITrigger), `PSTF_CanSave()` (CInfGame::CanSaveGame
  as the quick save asks it: 1/0 + refusal strref).
- Unit: `tests/unit/test_party.cpp` (mapping, table consistency, key patches, all-or-nothing apply).
- Static: `pstf_verify` checks every site and patch against the on-disk exe.
- Lua (more): `PSTF_Formation(formation[, angle])` (GetOffsets through the hook: "selected=N pts=x,y;.."),
  `PSTF_UnselectAll()`.
- Tools: `python -I tools/party_scan.py [--all]` (local arrays sized 6k, "%d" formats, 6k memsets in every
  function with a site or patch: the narrowing for the stack-array review), `python -I tools/crash.py
  --va RVA ...` (names Torment.exe addresses from our log's CRASH block, no dump needed).
- In-game: `tests/ingame/party8.lua` (Hive save 126): 3 companions join with the engine's JoinParty
  (party 8, a portrait slot each); triggers NumInParty(8)/GT/LT, InParty, InPartySlot(.., 6/7);
  GiveItemCreate on member 8 + PartyHasItem + TakePartyItem; a 9th is refused and waits in the engine's
  join queue (CInfGame +0xA258), takes the place when member 7 leaves; leave/rejoin reuses the free
  portrait slot; then a quick save (stepping through banter dialogues first) - passing up to the save.
  `tests/ingame/party8_load.lua` loads that quick save (000000001): all 8 come back in the same order
  with the same portrait slots (Grace rejoined last but kept portrait 6; InPartySlot reads the portrait
  slot). `party8_formation.lua` (same save): point 7 for 10 formation/angle pairs with a selection
  smaller than 8. `party8_fight.lua` (same save, `PSTF_TBC_AUTOPARTY=1`): all 8 are in the initiative
  order and each takes a turn; with `PSTF_SHOT_DIR` the shots show the 8-slot BG3 column and 9 cards.
  `party8_screens.lua` (same save, env `PSTF_DLG_TEST=1`): every member's record, inventory, mage and priest
  screens and the area map, opened through `OnLeftPanelButtonClick` as the radial buttons do (record shows
  the selected member, the ground cache fills slot k and is flushed on close, 8 map markers). Probe
  `PSTF_ScreenCaches()`. Opening a screen with Lua `SelectEngine` skips the target's OnPortraitLClick: the
  record then shows a stale `currentID` (empty sheet) - not a bug, the buttons never do that.

## Findings worth keeping

- Grace's interjection banters fire with 7+ members in the Hive (real content): the engine refuses to
  save during a dialogue (input mode 0x502, CanSaveGame reason 0xF00380) - tests must step the dialogue.
- `C:Eval(script)` without an actor runs on the area's actor; after `ActionOverride(..)` it stops running
  further actions (vanilla too). `AddexperienceParty` gives each member its own share (WIS bonus).
- `LeaveParty` re-selects the protagonist and then pulls a waiting joiner from `+0xA258`; destroying that
  waiting creature first crashes in `CGameSprite::Select` (vanilla behaviour, test artefact).

## Scan for other 6-slot arrays (done)

Every function with a site or patch (`tools/party_scan.py`), plus every Linux function that calls the
party accessors (`GetCharacterSlot`, `GetCharacterPortraitNum`, ...) in a loop bounded by 6, plus every
use of a portrait number as an index:
- Stack arrays: only Marshal's CGameSaveCharacter[6] (fixed). The 24/12-byte hits are CString/CAIObjectType
  temporaries; `StartDeathReaction` has 256-entry arrays; `RenderEncumbrance`'s CRes[6] is not per slot.
- Resources by slot: PORTRT%d (fixed above). `ExportParty` ("%s%d") and `ReadyEndCredits` ("CHAR%d")
  write files by a running counter (no fixed set).
- Loops `!= 6` over accessors: `JoinParty`, `LeaveParty`, `PostMoveCharacterFromOverflowToParty`,
  `CScreenCreateParty::*` are all multiplayer (`g_pChitin +810 == 1`) or party creation: left at 6.
- Indexed by portrait number: `CAIGroup::GetOffsets` points (fixed above). Load (`CInfGame::Unmarshal`)
  has no per-slot stack array; loading 8 works.

## Not covered / open

- Multiplayer (CMultiplayerSettings i32[6]/u8[6] per character, CBaldurMessage slot messages,
  UpdateCharacterPartyOrder, MakeCharacterProtagonist, char-creation screens): left at 6.
- Player1..Player6 object ids and `<PLAYER1>`..`<PLAYER6>` tokens stay 1..6 (OBJECT.IDS has no 7/8);
  Player6 now finds the 6th occupied of 8 portrait slots.
- The world screen's first-3/last-3 group hotkeys and portrait number keys stay 6 (members 7/8 are
  selected through their portraits). Banter selection is count-based already.
- The save list (`BuildGameSlot`) shows PORTRT0..5 only.
- HUD: checked on screen with 8 (party8_fight shots).
- Test content, not party size: `CreateCreature("VHAIL")` is the chained statue (ANIMATE 0xF062
  cl_1_Vhailor_Still). The record portrait is "STP" + the body animation resref[4:] (lin UpdateLuaStats
  566420) and the inventory doll comes from the animation too, so the statue had neither (STPTDB). His
  memory dialogue (DVHAIL) runs `Polymorph(Vhailor)` (0xF047) before he can join; party8.lua does the same.
  The 8-member save for playing/recording: `tests\saves\000000200-PSTF-8-Party` (party8.lua's quick save).
