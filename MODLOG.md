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
