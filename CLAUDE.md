# CLAUDE.md

Guidance for Claude Code in this repository.

## What this is

`pst_forever`: a native hook DLL (plus a `version.dll` proxy loader) that extends **Planescape: Torment:
Enhanced Edition** in place. Modules: turn-based combat, upscaled areas/sprites, dynamic lighting + depth,
examine voice-over, 8-member party, dev/test tooling. Architecture: `docs/ARCHITECTURE.md`.
Turn-based plan: `TURN_BASED_COMBAT_EXTENSION_PLAN.md`. Lighting/VFX plan: `docs/VISUALS_PLAN.md`.

Rule for every feature: **loose on presentation, exact on anything a script, dialogue, item or spell can
observe.** Use the engine's own rules; never write a parallel rules engine.

## Target (only this build)

- Game: `C:\GOG Games\Planescape Torment - Enhanced Edition` (GOG **3.2.2.0**). Deploying and testing there is fine.
  `linux\Torment` there is the same release's Linux binary (not stripped): our named reference.
- `Torment.exe`: **x64**, base `0x140000000`, no ASLR, no RTTI/PDB, SHA-256 `99ae1586…a3c5`. The DLL is x64.
  If the game updates again, everything in `resources/re/` is stale: re-fingerprint and re-run `tools/re_pipeline.sh`.
- PST-UB-Reloaded is installed (WeiDU). Don't uninstall mods without asking.

## Reverse engineering: search, don't read

- **`python -P tools/re.py <cmd>`** is the way into the dumps (`-P`: the script name shadows stdlib `re`) (SQLite index `resources/re/index.sqlite`;
  rebuild with `python tools/re.py build` after any dump change):
  - `find REGEX` names in both binaries with the Linux↔Windows mapping
  - `code NAME [--win] [--lines A-B]` pseudocode (Linux is named; `--win` is the hook target)
  - `xref NAME [--win]` callers/callees · `str TEXT` strings + users · `grep REGEX [--win]` over all pseudocode
  - `asm NAME [--lin] [--lines A-B]` disassembly + IDA prototype (Windows by default: hook prologues)
  - `data REGEX [--win]` who reads/writes a global · `data FUNC --uses [--win]` globals a function touches
  - `addr NAME` Windows VA/RVA/bytes/unique signature (for the symbol manifest)
- `python -I tools/vtable.py lin CLASS | win VA [N]`: vtable slots with names (Linux) / ported names (Windows).
- `python -I tools/exe_bytes.py VA N [--rel32|--u32]`: raw bytes of the on-disk exe at any VA (code or
  `.data`), as C++ array, with rel32/RIP target or dword value. Use it for every manifest entry.
- The dumps are complete (every function: pseudocode, disassembly, calls, data refs, prototype; proof in
  `resources/re/*/dump/verify.json`, asserted by the tests). Don't open IDA for lookups; use `re.py`.
  Never open `all_Torment.c`/`.asm` (tens of MB) or list `dec_all/` directly.
- Logic comes from the **Linux** pseudocode (`resources/re/lin`, real names, same 3.2.2 release). Addresses and
  offsets come from the **Windows** side; both are x64 so offsets usually agree, but confirm in the Windows code.
- Ported names (`resources/re/win/dump/names_win.tsv`) are ~85–90% right. Confirm a function on both
  sides before hooking it. When you confirm or name one by hand, add it to `resources/re/win/manual_names.tsv`.
  Then `bash tools/re_pipeline.sh rematch` (machine time, not tokens).
- IDA 9.1: `C:\Program Files\IDA Professional 9.1\idat.exe` (headless `-A -S"script.py"`); Hex-Rays works for x86 and x64.
- Other references: `resources/iesdp-gh-pages` (formats/opcodes/actions), `resources/ext/InfinityLoader`
  (Bubb's loader, design reference; its PST:EE header is 3.1.4 x86), `D:\gemrb` (previous GemRB fork; reference only).

## Build, deploy, test

- Toolchain: MSVC 2019 BuildTools v142, CMake, `-G "Visual Studio 16 2019" -A x64`. The user PATH has a stray
  quote (Nsight entry) that breaks `vcvars64.bat`: build scripts must set a clean PATH before calling it.
- **Default loop: `tools\check.ps1`**: build, L0 unit tests, static hook verification against the on-disk
  `Torment.exe`. Prints only errors and the summary.
- `tools\deploy.ps1` copies `version.dll`, `pst_forever.dll` and `pst_forever\` into the game folder (closes the game first).
- **`tools\launch.ps1 [-Save PREFIX] [-Lua FILE] [-Close]`**: starts the game straight into a save (newest by default)
  and runs FILE in the engine's Lua state once the world is up; prints our log lines; exit 0 = loaded and the
  script raised no error. ~3 s to a loaded save. Smoke test: `tools\launch.ps1 -Lua tests\ingame\smoke.lua -Close`.
  How: sets Baldur.lua `Last Save PST`, env `PSTF_AUTOLOAD=1`/`PSTF_RUN_LUA`, the `dev` module presses Continue.
  Baldur.lua on this machine has `Cheats=1` (console) and `Disable Movies=1` (skips ALL movies, story ones too);
  backup of the original in `build\backup\`.
- `python tools\pstf.py CMD` queries a running game over the dev pipe (`lua`, `actors`, `actor ID`, `hooks`, `shot`).
- RE tooling guards: `python -m pytest tests/tools -q` (exe hashes, dump completeness, key mappings).
- RE pipeline: `bash tools/re_pipeline.sh` (see `docs/ARCHITECTURE.md` §2); ~2.5 h, run in the background.
- Exist now: `check.ps1`, `deploy.ps1` (`-Remove` = back to vanilla), `launch.ps1`. Not yet: `pstf.py` (pipe).
- **Turn-based fight test**: `tools\launch.ps1 -Lua tests\ingame\tbc_fight.lua -Env @{PSTF_TBC_AUTOPARTY='1'} -Until 'tbc: fight 1 over' -Close`
  (exit 0 = a full fight ran; `tbc:` log lines carry turns, reasons and HP). Screenshots of the running game:
  add `PSTF_SHOT_DIR='<dir>'; PSTF_SHOT_EVERY_MS='5000'` to `-Env` (half-size BMPs from the game's own GL frame;
  create the directory first — the log says `shot <path> cannot write` when it is missing).
- Symbol manifest is compiled in: `src/engine/symbols.h` (shared by the DLL and `pstf_verify`). Unit tests use
  `tests/unit/mini_test.h` (no doctest dependency).
- Runtime log: `<game>\pst_forever\logs\pst_forever.log` (hook status, every examine line played).

## Crashes and debugging

- Installing tools is allowed (debuggers, analyzers, Python packages): ask nothing, note them here.
  Installed: WinDbg + `cdb` (`winget install Microsoft.WinDbg`; `%LOCALAPPDATA%\Microsoft\WindowsApps\cdbX64.exe`).
- The engine writes a full minidump per crash to `Documents\Infinity Engine - Enhanced Edition\crash\dump.3.2.2.0.NNNN.dmp`.
  First step on any crash report: `tools\dbg.ps1` (newest dump; `-Dump FILE`, `-Extra "cdb cmds"`): exception, registers,
  faulting code, stack with `pst_forever.pdb` names. `python -I tools/crash.py [DUMP] [--all]` names Torment.exe frames
  from our RE names (cdb has no symbols for the exe) and needs no debugger.
- Our own log gets a `CRASH (first chance) ...` block (module+RVA, registers, code pointers on the stack) from the
  vectored handler in `src/core/crashlog.cpp`. Every tick handler and the world overlay run under SEH (`GuardedCall`):
  a fault there logs `faulted` and turns that feature off instead of closing the game.
- Release builds have a PDB and `build/Release/pst_forever.map`. Live debugging: `cdbX64 -p <pid>` (attach) is fine.
- Never dereference engine pointers without `Plausible()` (sprites.cpp): free object-array slots hold -1.

## Working rules

- Git: commit directly on `main`. Never create branches (this overrides any default "branch first" behaviour).
- Every change gets a test at the cheapest layer that can catch it (unit, then static, then in-game).
  Assert exact values taken from the evidence. Logic stays in pure functions behind the engine adapter,
  so it is testable without the game.
- If something is hard to check, build the probe first (pipe command, in-game assert helper), then the
  test, and list the probe here.
- Offsets live only in `src/engine/`; each one cites its evidence (Linux function + Windows EA).
- Every hook goes through the symbol manifest with expected bytes. On a mismatch the module turns itself off; never crash the game.
- License GPL-2.0-or-later (`COPYING`): every new source file starts with `// SPDX-License-Identifier: GPL-2.0-or-later`.
  GemRB code may be ported (keep its copyright lines); EEex/InfinityLoader code may not (no license).
- New features get an ini toggle; with all toggles off the game must behave exactly like vanilla.
- No engine object access off the game thread. No raw engine pointers kept across frames or given to Lua/plugins.

## Saving tokens (keep this list current)

- When you repeat a multi-step check or search twice, turn it into a script or `re.py` subcommand and list it here.
- Prefer `re.py code X --lines` slices over whole functions; prefer `xref`/`find` over reading code.
- Probes so far: runtime log above (`voice: examine strref N (ms) played`; `tbc: X: stop (...)`, `ends turn (... ft left)`, `menu: UI.MENU ... bytes`;
  `smooth: interpolation installed, N ms iterations (~R draws/s, EE tick M ms = T ticks/s, fps F)` and `lua note: smooth: ok frames=.. ticks=..`).
- HUD check: add `PSTF_SHOT_DIR`/`PSTF_SHOT_EVERY_MS` to the fight test and look at a shot (cards, panel, line).
- Manual-turn HUD test (hover preview, selection lock, party AI off): `tools\launch.ps1 -Lua tests\ingame\tbc_hud.lua
  -Env @{PSTF_SHOT_DIR='<dir>'; PSTF_SHOT_EVERY_MS='1500'} -Until 'shot_003' -Close`. Probes: `PSTF_PreviewAt(x,y)`
  (fake cursor), `PSTF_PathFt(x,y)`, `PSTF_WalkCost(x,y)`, `PSTF_PartyId(slot)`, `PSTF_Note(text)` (log line).
- Lua probes (`[dev] lua=1`): `PSTF_Version/EngineMode/RoundLength/WorldTime/RoundsForSeconds/TbcSim/SelfTest/Screen`, with tbc on also
  `PSTF_TbcState/EndTurn/MakeEnemyNearest/SpritePos`, with `[smooth] interpolation=1` also
  `PSTF_SmoothState/Frames/Ticks/TickMs/Fps` (draw iterations vs world ticks, EE's tick interval and fps),
  `PSTF_SmoothTrace(id, frames)` + `PSTF_SmoothTraceResult()` (per-frame trace of one creature: shown pos, view;
  result = frames, walk frames, still frames, wobble px, longest hold ms; `python -I tools/smooth_trace.py [LOG] [--rows]`
  summarises the log rows), `PSTF_LockCamera(id)` (the HUD scroll lock, -1 off), `PSTF_SmoothLockFrames()`,
  with `[tbc] camera_follow=1` also `PSTF_ViewCenter()` (view centre, -1 with no world screen) and
  `PSTF_CameraSteps()` (follow steps + 1 while a quick pan runs; with `camera_smooth=1` the steps count
  drawn frames, so they grow faster than `PSTF_SmoothTicks`),
  callable from the in-game console (Ctrl+Space, needs `Cheats` or `Debug Mode` in Baldur.lua) or from a
  temporary `<game>\luaAuto.cfg` (runs at boot, before LoadOptions). Every call logs `lua: NAME -> value`.
  Details, engine facts and open questions for turn-based combat: `docs/TBC_RESEARCH.md`.
- Test-driver probes (env `PSTF_TEST_TICK=1`: Lua `pstfTestTick()` runs after the round-clock shift on the
  iterations that advance the world — with `[smooth] interpolation=1` the extra draw-only iterations are
  skipped, so frame counts in tests stay calibrated to the tick rate):
  `PSTF_TbcCurrent()` (id, round, party, used, acting, ft), `PSTF_FightId(i)`, `PSTF_EvalAs(id, bcs[, 1])`
  (queue BCS on any creature; 1 = take over), `PSTF_MakeEnemy`, `PSTF_SetHp`, `PSTF_Effects/HasEffect(id, op)`,
  `PSTF_Objects([caster|-3])`, `PSTF_Stats(id)`, `PSTF_HoverAt(x,y)`, `PSTF_RollProbe(kind, ok, roll)`, `PSTF_BarSlots()`.
  Env `PSTF_FEEDBACK_LOG=1` logs every engine FeedBack (why a cast failed); `PSTF_TBC_NOFIGHT=1` = probes, no fights.
- In-game suites: `tests\ingame\tbc_rules.lua` (round clock: Blur, Adder's Kiss, regen), `tbc_spells.lua` (needs the Dead Nations save, no longer on disk),
  `tbc_bar.lua` (hotbar), `tbc_hud2.lua` (hover/dice screenshots), `smooth.lua` (frame interpolation: the loop
  iterates faster than the world ticks while `tick_ms` still equals EE's own; run
  `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\smooth.lua -Env @{PSTF_TEST_TICK='1'} -Until 'smooth: ok|TEST FAIL' -Close -Timeout 200`),
  `smooth_lock.lua` (walk under the engine lock camera: no hold >= 0.75 tick, wobble <= 1 px; run
  `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\smooth_lock.lua -Env @{PSTF_TEST_TICK='1'} -Until 'smooth_lock: ok|TEST FAIL' -Close`;
  `[smooth] lock_camera=0` must fail it with wobble — the toggle check), engine fact: a walker's step lands every OTHER tick,
  `smooth_cam.lua` (the follow camera runs in the draw pass: while the creature whose turn it is walks, the
  camera steps once per drawn frame instead of once per tick; run
  `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\smooth_cam.lua -Env @{PSTF_TBC_AUTOPARTY='1'; PSTF_TEST_TICK='1'} -Until 'smooth_cam: ok|TEST FAIL' -Close -Timeout 220`;
  `[tbc] camera_smooth=0` makes it report `camera stepped once per tick` instead — that is the toggle check).
  After a run: `bash tools/tlog.sh [pattern]` — `bash` is broken on this machine (WSL `/bin/bash` missing), so
  grep the log with `Select-String -Path '<game>\pst_forever\logs\pst_forever.log' -Pattern 'smooth|tbc:'`.
- Spells in tests as in a real game: `AddXPObject(Myself,3000000)` (real level-ups), then
  `SpellNoDec(target, 2xxx)` (PST:EE: needs the caster's own level, not memorised); items with
  `GiveItemCreate("RES",Myself,1,0,0)` + `UseItem("RES",Myself)`. No forced caster levels.
- Test save: the Hive, `-Save 000000126` (many Hive thugs to turn hostile; `tbc_rules`, `tbc_sim`, `tbc_hive`).
  `tbc_hive.lua`: Swarm Curse pacing, Adder's Kiss, Whispering Flask/Embalming take the action, save sound.
- Game resources: `python -I tools/ieres.py spl|itm NAME` (effects per header), `named REGEX` (SPL/ITM by TLK
  name), `find REGEX [EXT]`, `raw NAME.EXT [OUT]` (override, then chitin.key/BIFs).
- Lighting (`[lighting] enabled=1`, `docs/VISUALS_PLAN.md`): `tools\launch.ps1 -Save 000000076|126|164 -Lua tests\ingame\lighting.lua
  -Env @{PSTF_TEST_TICK='1'} -Until 'lighting: ok|TEST FAIL' -Close` (round trip 0 px, lit changes pixels, GPU = CPU model,
  shots `pst_forever\logs\light_<AREA>_{off,lit,light,falloff}.bmp`). Probes: `PSTF_LightDebug(0 lit|1 light|2 roundtrip|3 falloff|4 off)`,
  `PSTF_LightState()`, `PSTF_LightCheck()`+`PSTF_LightCheckResult()` (pixels our pass changed), `PSTF_LightCompare()`+
  `PSTF_LightCompareResult()` (falloff buffer vs `DebugFalloffAt`), `PSTF_LightShot(path)`; env `PSTF_LIGHT_DEBUG=N`.
  Log: `lighting: <WED>: N baked lights ...` per area. Data: `<game>\pst_forever\assets\lighting\<WED>\` (copied from GemRB).
  Script area travel from Lua does not work (tried LeaveAreaLUA/Panic, ActionOverride, MoveGlobalsTo): use a save.
  Phase 4/5 (glow draws, effect lights, bloom, atmosphere): `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\lighting_fx.lua
  -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'lighting_fx: ok|TEST FAIL' -Close` (Dak'kon casts SPWI107).
  Probes: `PSTF_LightFx()` (glows, table hits, fx lights, frames with one, strongest light x/y; logs RenderTexture call
  counts and the OR of world-pass blend flags), `PSTF_LightBloom()`, `PSTF_LightAtmo()`, `PSTF_LightSet(name, value)`
  (A/B toggles), debug mode 5 = glow mask (red) + bloom (green). Glow table: `python -I tools/vfxscan.py run`
  (-> `<game>\pst_forever\assets\vfx\vfxauto.tsv`, needs `tools/sprite_depth/sprite_depth.py` for the BAM decoder);
  table misses are logged as `lighting: glow draw not in the vfx table`.
  Emitters: additive draws plus `assets/vfx/vfxemit.txt` (vfxscan: BAMs named in Torment.exe or SPL/ITM/EFF/PRO/VVC/BCS).
  Draws with their own cell or additive blend are re-drawn into an emissive buffer (per-pixel glow mask, bloom
  source); normal-blend FX-surface blits are sprite+effect composites, so they give lights only. Probes:
  `PSTF_LightCoverage()` (capture px / frame-rect px, <0.9 = per pixel), `PSTF_LightTrace(prefix|'*')` (logs each
  distinct draw path/resref/flags). Hand lights: `data/vfxlight.tsv` (deployed; Ignus `????IGYB`); buff colour
  lights from opcodes 8/9/52/61/353 (`engine::ColorEffects`). Test: `tests\ingame\lighting_lights.lua` (same env as
  lighting_fx). The engine flashes one white frame when Magic Missile launches (vanilla, cast+22 ticks).
  Script casts: `SpellNoDec`/`ForceSpell` at a party member can fizzle (feedback 5); SPWI107 at Morte works.
  FX surface: every effect is drawn alone (`CInfinity::FXRender` one cell, then one `FXBltToBack`; blits = cells), so a
  blit whose cell is an emitter is the effect's exact pixels (captured per pixel, softened with the screen blend,
  `[lighting] fx_soften`). Probe `PSTF_LightFxCells()` -> blits, pure, softened, cells, "RES cyc/frame anchor x,y rect
  wxh pure;..." (area px; the cell's anchor = the owner's feet). Hand glow overrides: `data/vfxglow.tsv`.
  Phase 2 (sprite buffer, `tests\ingame\lighting_sprites.lua`, same env as lighting_fx): `PSTF_LightSprites()` (creature
  draws seen / with depth / drawn, frames, mean LM, depth files), `PSTF_LightSpriteCheck()` (buffer px, coverage px,
  both; measured on the next frame), debug mode 6 (creatures violet, engine shadow blobs green, key shadows yellow).
  `src/core/inflate.*` = our zlib decoder (for .DEP); `src/modules/lighting/sprite_depth.*` = SPD1 reader.
- Gore (`[gore]`, `src/modules/gore/`, GemRB ScenePolish port): `CGameSprite::AddBlood(dir, type)` hook; gushes 3/4/5,
  artery 6, exploding death 7 become material effects (RESDATA.INI `armor` per animation, `data/hitarmor.tsv` from
  `python -I tools/hitmat.py` - re-run after a mod changes RESDATA - refined by `data/hitmat.2da` ANIM/ARMOR rows; EE
  animation ids go past 255, exact id first, then the low byte). Crits 10..17 keep their animation plus an
  exaggerated spray. Stains draw at the floor point (after `CInfinity::Render`, before objects: under the actors,
  lit by the lighting pass). Test: `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\gore.lua -Env @{PSTF_TEST_TICK='1';
  PSTF_TBC_AUTOPARTY='1'} -Until 'gore: ok|TEST FAIL' -Close -Timeout 300`. Probes: `PSTF_GoreState()`,
  `PSTF_GoreHit(id, severity[, crit[, anim]])`. Deploy keeps an existing ini: add new sections to the game's ini by hand.
- Combat log: `<game>\pst_forever\logs\combat.log` (wall clock, world tick, round, ticks into the turn, event):
  read it first after a fight.
