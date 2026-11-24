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
  NOT YET VERIFIED in game (the C: drive ran full during the run).
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
- In-game: `tests/ingame/party8.lua` (Hive save 126): 3 companions join with the engine's JoinParty
  (party 8, a portrait slot each); triggers NumInParty(8)/GT/LT, InParty, InPartySlot(.., 6/7);
  GiveItemCreate on member 8 + PartyHasItem + TakePartyItem; a 9th is refused and waits in the engine's
  join queue (CInfGame +0xA258), takes the place when member 7 leaves; leave/rejoin reuses the free
  portrait slot; then a quick save (stepping through banter dialogues first) - passing up to the save.
  `tests/ingame/party8_load.lua` loads that quick save (000000001) and checks all 8 come back.

## Findings worth keeping

- Grace's interjection banters fire with 7+ members in the Hive (real content): the engine refuses to
  save during a dialogue (input mode 0x502, CanSaveGame reason 0xF00380) - tests must step the dialogue.
- `C:Eval(script)` without an actor runs on the area's actor; after `ActionOverride(..)` it stops running
  further actions (vanilla too). `AddexperienceParty` gives each member its own share (WIS bonus).
- `LeaveParty` re-selects the protagonist and then pulls a waiting joiner from `+0xA258`; destroying that
  waiting creature first crashes in `CGameSprite::Select` (vanilla behaviour, test artefact).

## Not covered / open

- Save/load with 8 after the Marshal frame fix (needs a run; see above). Then the load path
  (`CInfGame::Unmarshal`) gets the same stack-array check.
- A systematic scan for other stack arrays indexed by party slot in the functions whose loops now run
  to 8 (the Marshal case was found by a crash).
- Multiplayer (CMultiplayerSettings i32[6]/u8[6] per character, CBaldurMessage slot messages,
  UpdateCharacterPartyOrder, MakeCharacterProtagonist, char-creation screens): left at 6.
- Player1..Player6 object ids and `<PLAYER1>`..`<PLAYER6>` tokens stay 1..6 (OBJECT.IDS has no 7/8);
  Player6 now finds the 6th occupied of 8 portrait slots.
- Presentation: map screen party markers (CScreenMap::SetMap, 12-byte entries x 6), the world screen's
  first-3/last-3 group hotkeys and portrait number keys stay 6. Banter selection is count-based already.
- HUD: the BG3 column has 8 slots (tbc ui_layout); not yet checked on screen with 8 members.
