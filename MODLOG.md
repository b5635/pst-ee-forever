# MODLOG

## 2026-11-21 — examine voice-over (module `voice`)

**Problem.** PST Revoiced (WeiDU, `setup-PST_Revoiced.tp2`) sets TLK sound resrefs (`[VV<strref>]`) for
dialogue and examine lines. Dialogue plays; info-point ("examine", ? cursor, ARE region type 1) text does not.
All 318 info-point strrefs in `D:\pst-voiceover-engine\pst_revoice_ee\infopoints.csv` have the sound set in
`lang/en_US/dialog.tlk`, so the data is fine.

**Engine fact.** `CGameTrigger::OnActionButton` (lin 26A680, win 140218DD0), info-point branch:
`CTlkTable::Fetch(strref, STR_RES&, 0)` fills text **and** `CSound`, then `CScreenWorld::DisplayText` +
`CGameText` show the text, then `~CSound`. No `Play`. There is no script action or Lua hook for this;
region scripts would need patching ~300 ARE entries and would miss areas already stored in saves.

**Route.** Native: the planned single `pst_forever.dll` (all modules) + tiny `version.dll` loader.
- Loader patches the exe entry (RVA 580AA0, 14 bytes checked) for one call, loads `pst_forever.dll`,
  calls `PSTF_Init`, restores the bytes.
- Voice module redirects the one `call Fetch` at 1402196FC (bytes `E8 3F 71 28 00`) through a near trampoline.
  After the real Fetch: if text non-empty and TLK sound present → `SetChannel(0 NARRATIO, no area)`,
  `SetFireForget(1)`, `Play(0)` — the engine's own non-positional voice pattern (`CScreenInventory::SetErrorString`).
  Same strref re-clicked while still playing is skipped (`GetPlayTime` ms).
- Windows `CSound` is a pimpl wrapper (`[rcx+18h]` = CSoundImp). `CSound::Play(int)` = 140494BD0 (port named
  it PickSound; wrong).
- Toggle: `pst_forever\pst_forever.ini` `[voice] examine=1`. No ini = vanilla.

**Verified.** `tools\check.ps1`: build, 6 unit cases, 7/7 manifest symbols vs on-disk exe. In game: log shows
`examine voice installed at 1402196FC` and `examine strref 5059 (4700 ms) played`, `5045 (4360 ms) played`
(Mortuary slab/corpse info points).

**Restore / uninstall.** `tools\deploy.ps1 -Remove` (deletes `version.dll`, `pst_forever.dll`), or set `examine=0`.

**Gotchas.**
1. `python tools/re.py` breaks (shadows stdlib `re`): use `python -P tools/re.py`.
2. Windows Hex-Rays truncates functions IDA marked `__noreturn` (OnActionButton, Play body); read `asm --all`.
3. The other `Fetch` in OnActionButton (1402193DB) is the travel-region message; deliberately not voiced.

**Next.** Ask user to confirm audio by ear; other unvoiced text paths (item descriptions?) if they report any.

## 2026-11-21 — turn-based combat groundwork (module `tbc` core, `dev` Lua bridge)

**Goal.** BG3-style turns on PST's own rules. Research, probes and open questions: `docs/TBC_RESEARCH.md`.

**Built.**
- Pure core `src/modules/tbc/`: `tbc_rules.h` (time maths, APR encoding, initiative, path clip,
  opportunity check), `director.*` (event-driven turn state machine: action/movement/reaction budgets,
  shared party initiative, late joiners, removal/end), `tbc_sim.*` (scripted fight). 12 unit cases.
- Engine adapters `src/engine/world.*` (ROUND_LENGTH, ENGINE_MODE, world clock) and `lua.*` (verified Lua
  5.2 functions only). 12 new manifest entries with `pstf_verify` RIP-target and `.data` cross-checks.
- `dev` module: redirects the `luaAuto.cfg` load in `CLUAConsole::LuaInit` (win 14022627A) and registers
  `PSTF_*` probes. Toggle `[dev] lua=1`.

**Engine facts found.** PST round = 51 ticks (not 100, not "5 s"): `LoadOptions` default by `ENGINE_MODE==3`.
15 ticks per game second. Windows `CBaldurChitin` game pointer at +0x10C8 (Linux +0x1118). Pathfinding is on
its own thread (`SearchThreadMain`). The ported `lua_createtable`/`lua_setfield`/`lua_type` names are wrong.

**Verified in game** (temporary `luaAuto.cfg`, then removed): log shows `dev: Lua bridge installed at 14022627A`,
7 functions registered, `PSTF_EngineMode -> 3`, `PSTF_TbcSim -> winner=party rounds=3 swings=10 clock=200`.
`PSTF_RoundLength -> 100` at that point: **luaAuto.cfg runs before LoadOptions**; the 51 needs the console.

**Gotchas.**
1. luaAuto.cfg is too early for option-derived values (round length); game object exists already.
2. Windows CBaldurChitin offsets differ from Linux; CInfGame offsets checked so far agree.
3. `pstf_verify` only accepts code-section symbols; globals are checked through RIP-relative operands.

**Restore.** `[dev] lua=0`, or `tools\deploy.ps1 -Remove`. No game files changed (probe cfg deleted).

**Next.** Owner: enable `Cheats` in Baldur.lua and run `Infinity_DisplayString(PSTF_SelfTest())` in a save
(expect `ok`). Then confirm `ProcessAI`/`AIUpdateWalk` vtable hooks and add `PSTF_Own/Release` (TBC_RESEARCH §7).

## 2026-11-21 — test lab: cheats, no movies, load a save with no input

- Baldur.lua (owner's request): `'Game Options','Cheats','1'`, `'Program Options','Disable Movies','1'`.
  Original backed up to `buildackup\Baldur.lua.20261121-020348`. Disable Movies skips story movies too.
- `tools\launch.ps1 [-Save PREFIX] [-Lua FILE] [-Close]`: sets `Last Save PST`, starts the game with
  `PSTF_AUTOLOAD=1`; the `dev` module presses Continue from a new per-frame tick and runs FILE when the world is up.
- New manifest entries (30 total): frame tick call 140144254, active engine/start/world/load screen offsets
  (Windows = Linux - 0x50 in CBaldurChitin), OnContinueGame, lua_pcallk/settop/tolstring, luaL_loadfilex, g_lua.
- Verified: `launch.ps1 -Lua tests\ingame\smoke.lua -Close` -> exit 0, `PSTF_RoundLength -> 51`,
  `PSTF_RoundsForSeconds(15) -> 5`, `PSTF_SelfTest -> ok`, about 3 s from launch to script.
- Gotchas: the game is killed by PID, so Baldur.lua is not rewritten on that exit (our edits persist).
  The ported name of 140144250 (`__acrt_uninitialize_tmpfile`) is wrong: it is CBaldurChitin::SynchronousUpdate.

## 2026-11-21: turn-based combat in the game (module `tbc`)

**Route.** No parallel rules engine: hold every combatant except the active one by skipping its
`CGameSprite::ProcessAI` (vtable slot 35, win 140649440) and `AIUpdateWalk` (two call sites in
`AIUpdate`). The active one runs the engine for one round of busy time (51 ticks), then
`ClearAI(0)` + `DropPath`. Census, turn clock and director are pure and unit-tested; details are in
`docs/TBC_RESEARCH.md` section 8.

**Engine facts found.**
- Windows `CGameSprite` vtable is at 140649328 (found by scanning `.rdata` for the ProcessAI pointer).
  Slot = Linux slot - 1 near AIUpdate/ProcessAI, but slots 29..34 do not map 1:1.
- Object array at 14072D860 (16-byte entries); party ids at CInfGame +0xA200.
- Sprite: +0x0C pos, +0x18 area, +0x38 EA, +0x48 id, +0x348 queue count, +0x3F0 current action,
  +0x574 HP. Alive is bit 0x800 of the state word chosen by +0x4BBC (+0xFE8 or +0x1C98; Linux +0x1C90).
- The Windows sprite layout drifts from Linux past about 0x1000.

**Verified.** `tbc_fight.lua` with party AI: the fight starts, turns rotate in initiative order,
the zombie dies on round 2 to the protagonist's attacks (engine rules), "Combat over", exit 0.

**Gotchas.**
1. `NearestEnemyOf` needs line of sight. The test therefore walks the protagonist up with
   `C:Eval('MoveToPoint([x.y])', 0)`.
2. `C:Eval(text, -1)` runs as the area's script object; `C:Eval(text, slot)` runs as party member `slot`.
3. Someone clicked examine points during one test run and the game then closed. Fullscreen test runs
   take the screen, so keep them short or ask first.

**Next.** Combat clock and effect rebase, opportunity attacks, turn order UI, Dexterity initiative.

## 2026-11-21: turn-based HUD and BG3 action economy (module `tbc`)

**Built.**
- Action economy (`economy.h`, pure): per turn 31 ft of movement (`[tbc] move_feet`) and ONE action.
  Walking spends feet (16 px across / 12 px up-down = 1 ft, one search-map cell); with no feet left the
  walk hook skips `AIUpdateWalk` and a path that still wants feet is cancelled. A non-move action
  (attack, cast, item, talk) that runs while not walking is the action; it ends when the engine finishes
  it or after one round (attack repeats otherwise). Later actions are cancelled. Party turns end when
  both are spent or on End Turn; AI turns also on idle or a cap. The engine still decides every roll.
- HUD (`hud.cpp`) drawn by the engine's own .menu UI: menu `PSTF_TBC` + inline Lua appended to UI.MENU
  at load (redirected CString copy in `uiLoadMenu`, win 140459EC3; nothing on disk changes).
  Initiative bar with **bestiary art** (animation INI key `bestiary` -> anim +0x1D0C, matched to the
  journal's Lua `bestiary` table), HP bars, active card larger with gold frame, acted cards dimmed,
  "Round N: name"; bottom panel with action pip, "Movement 21 / 31 ft" bar and End Turn button;
  movement line = dots along the engine's own walk path (+0x4470 cells), gold within the feet left,
  red beyond, path length at the end. World -> UI as `CGameAreaNotes` places map notes (zoom aware).
- 16 new manifest entries (66 total), `pstf_verify` checks call/RIP targets and the 16x12 / 320x320 grid.

**Verified in game** (`launch.ps1 -Lua tests\ingame\tbc_fight.lua`, party AI, screenshots): HUD shows
Nameless One / Zombie Worker / Morte bestiary cards; TNO's walk stopped at 0.0/31 ft ("no movement
left"); each later turn one attack round ("Attack round done"); zombie 8 -> 1 -> dead in round 3.

**Gotchas.**
1. Ported names: win 140459E00 (named `lua_type`) is uiLoadMenu; 140456630 (named uiLoadMenu) is its tokenizer.
2. `launch.ps1` matched the previous run's log for `-Until` before the game rewrote it: now deletes it first.
3. A `mosaic` in a small label is scaled to the label (bestiary art 316x492 fits 54x84 cards).
4. Bestiary entries become known when the engine renders the creature (Render -> SetBestiaryKnown),
   so the cards always use `imageKnown`.

**Not done / next.** Hover preview of the path before clicking (needs a synchronous `CPathSearch`
on the game thread; today the line appears once the move is ordered, and the walk stops at the
budget). Manual party turns and the End Turn button were not clicked in this test (party AI ran the
turns). Camera does not follow the active creature. Effect-duration rebase and opportunity attacks
still open.

## 2026-11-21: crashes fixed, crash tooling, hover path preview, selection lock, no party auto-attack

**Crash.** All 4 engine dumps (Documents\Infinity Engine - Enhanced Edition\crash) were ours: `IsSprite` read
`[-1]` while scanning the object array from the frame tick (free slots hold -1). Confirmed with cdb
(`mov rax,[rdx]`, rdx = FFFFFFFFFFFFFFFF, rdi = object array slot, r14 = sprite vtable). Fix: `Plausible()`
before every engine pointer dereference. Tooling: WinDbg/cdb installed, `tools\dbg.ps1`, `tools/crash.py`,
vectored crash log in our log, SEH guard around tick handlers and the overlay, PDB + map in Release.

**Hover preview + line.** Drawn by the engine itself in the world pass: `CGameArea::RenderZoomed` calls
`CGameArea::Render` (redirected at 14019F91C); right after it we call `CVidMode::DrawLine2d` (the line
`CGameSprite::RenderActions` uses) with a dark outline and a gold (feet left) / red (beyond) core, a diamond
at the end, length label from the menu. Before the click: A* on the engine's own cell cost
(`CSearchBitmap::GetCost` with the creature's terrain table and personal space, as `SetTarget` asks it),
string-pulled; after the click: the engine's real path cells. Unreachable spot: red cross.

**Turn control.** Only the active party member stays selected (per frame: `UnselectAll` + `SelectCharacter`
+ `SelectToolbar`, the portrait-click sequence; the portrait Lua click is also guarded); nobody on enemy turns.
Party AI is switched off for the fight with the engine's `game:ToggleAI()` and restored after; party turns
start with `ClearAI`, so party members never attack on their own.

**Verified in game.** `tbc_hud.lua` (manual turns): "party AI on: false", selection lock, preview 51.7 ft
around the slab with gold/red split (screenshot). `tbc_fight.lua` (party AI) full fight, exit 0, no faults.

**Gotchas.**
1. `near` is a Windows macro (minwindef.h): never name a variable `near`/`far`.
2. Open ground costs 5 per cell in the terrain table: the A* heuristic must scale by the minimum cost.
3. No corner-cut rule: isometric corridors are diagonal stairs of cells; with it the Mortuary splits apart.
4. The search map is 272x320 cells here (CSearchBitmap +312/+316), not GRID_ACTUAL 320x320.
5. Chain deploy after a successful check only (`check && deploy`); a failed build left the old DLL running.

**Not done.** Mouse hover not driven by a real mouse in tests (probe instead); End Turn button click untested.
