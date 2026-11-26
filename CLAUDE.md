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
- Release zip: `tools\package.ps1 [-Version V] [-Assets '<game>\pst_forever\assets']` -> `build\package\pst_forever-V.zip`
  (layout = the game folder: players unzip it next to Torment.exe; same files as deploy). `-Assets` also writes
  `-lighting.zip` (GemRB maps + sprite depth, ~1.4 GB, not in git). `.github/workflows/release.yml`: a pushed `v*` tag
  builds (VS 2022 runner), runs unit tests, packages and makes the GitHub release; the lighting zip is uploaded by hand to the `b5635/pst-ee-forever-assets` release as `pst_forever-assets.zip` (README and `docs/RELEASE_INSTALL.md` link its `latest/download` URL).
- `tools\deploy.ps1` copies `version.dll`, `pst_forever.dll`, `pst_forever_config.exe` and `pst_forever\` into the game folder (closes the game first).
- Options window `pst_forever_config.exe` (`src/config_tool/`, logic `src/core/ini_doc.*`, tests `test_ini_doc.cpp`): the shipped
  `data/pst_forever.ini` is embedded as its schema (sections, defaults, the comment above a key = its help; 0/1 defaults are
  switches except the `kNotBool` keys in ini_doc.cpp: add a new numeric key with a 0/1 default there). Section "Game (Baldur.lua)"
  = engine options we recommend (`kLuaOptions` in main.cpp: Disable Cosmetic Attacks = 1; refuses to save it while the game
  runs, the engine rewrites Baldur.lua on exit). CLI: `--upgrade` (deploy runs it: an existing ini gets this build's layout and
  new keys with defaults, values kept, old file `.bak`), `--set S.K=V`, `--lua S/K=V`, `--shot FILE.bmp [--section N]`
  (picture of the window), `--game DIR`. GUI subsystem: run it with `Start-Process -Wait -PassThru` for the exit code.
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
  Also ffmpeg (`C:\ffmpeg\bin`, Gyan full build) and gifski 1.34 (copied there from the GitHub release; winget's MSI hangs).
  PyAudioWPatch (`python -m pip install --user PyAudioWPatch`; run `tools/audio_loop.py` with `python -P`, not `-I`: user site).
- README media: `tools\record.ps1 -Out build\rec\X.mp4 -Width 1920 -Launch @{Save=..; Lua=..; Env=@{..}; Until=..}` records the
  screen with ffmpeg ddagrab while launch.ps1 runs (ffmpeg starts at `autoload: world ready`: the full-screen switch kills
  Desktop Duplication before that). It records the game sound too (`tools/audio_loop.py`, WASAPI loopback of the default
  output; synced by the wall clock of the first sample vs the first frame, log `record: audio offset N ms`; `-NoAudio` = video
  only). `AcquireNextFrame failed: 887a0026` at the end is the game closing, not a cut. Each recording also writes
  `<name>.events.txt` (lua notes / voice lines at their second in the video: use it for cut points). `-Clicks -DrawMouse`:
  a log line `showcase_click: DX,DY` (px from the screen centre) glides the real mouse there and clicks (probe `PSTF_CenterAt(x,y)`).
  YouTube Short: `tools\showcase\record_short.ps1 [-Only hammer,spells,moves,blood,dice,voice,party8]` records each clip
  alone (game music 0 while recording, put back after) to `build\rec\short\`; `python -I tools/showcase/make_short.py
  [--preview]` cuts `short.mp4` (1080x1920 60 fps; SEGMENTS table = clip, times, crop focus, titles, volumes; music
  `sounds/planescape main title ambient guitar lofi.mp3`). Discord/NVIDIA overlay toasts show for ~15 s after launch: the scenarios in
  `tools\showcase\` wait that long. `python -I tools/readme_gifs.py [--only X.gif]` cuts docs/media GIFs (clip times in
  its table), `tools/readme_media.py` the stills. Blood clip: set `[tbc] start_radius=0`/`join_radius=0` while recording (real-time melee).
- The engine writes a full minidump per crash to `Documents\Infinity Engine - Enhanced Edition\crash\dump.3.2.2.0.NNNN.dmp`.
  First step on any crash report: `tools\dbg.ps1` (newest dump; `-Dump FILE`, `-Extra "cdb cmds"`): exception, registers,
  faulting code, stack with `pst_forever.pdb` names. `python -I tools/crash.py [DUMP] [--all]` (or `--va RVA ...` for the addresses in our log's CRASH block) names Torment.exe frames
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
- **Logging and debugging are part of every change, never an afterthought.** When you cannot tell from the logs
  why something happened, first add the log line or probe that would have told you (a `tbc:`/combat.log line, a
  `PSTF_*` Lua probe, a tool), then investigate. Keep them: improve the existing lines when they mislead, list each
  new probe/tool in "Saving tokens" below, and remove nothing a test or the plan docs rely on. New behaviour that a
  script, dialogue or the player can observe gets a log line that names the creature and the reason.
- Offsets live only in `src/engine/`; each one cites its evidence (Linux function + Windows EA).
- Every hook goes through the symbol manifest with expected bytes. On a mismatch the module turns itself off; never crash the game.
- License GPL-2.0-or-later (`COPYING`): every new source file starts with `// SPDX-License-Identifier: GPL-2.0-or-later`.
  GemRB code may be ported (keep its copyright lines); EEex/InfinityLoader code may not (no license).
- New features get an ini toggle; with all toggles off the game must behave exactly like vanilla.
- Designing graphics (icons, banners, panels, effects): when the user gives a reference (video, screenshot),
  download it (`yt-dlp` into `build\ref\`), pull frames every 0.1-0.2 s (and 30 fps around the key moments) with
  `ffmpeg -vf "crop=..,scale=..,tile=.."` and read them: timings, shapes, colours, fades, positions. Recreate it
  faithfully, but in the Planescape: Torment UI style: bone/ivory lines, dark umber fills, bronze, verdigris and
  blood-red accents, a thin dark rim so it reads over any floor, the game's serif fonts (VOLLKORN, PTSERIF; EXOCET
  for titles). Art comes from a `tools/make_*.py` script (drawn 4x, box-filtered, `--preview` strip) into
  `data/override` BAMs; then check it in game with screenshots against the reference frames.
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
  `GiveItemCreate("RES",Myself,1,0,0)` + `UseItem("RES",Myself)`. No forced caster levels (ReallyForceSpellLevel).
  PST level-ups wait for the Level Up button, so AddXP alone leaves the level low: raise it with
  `ChangeStat(Myself,LEVEL|LEVEL2|LEVEL3,N,SET)` (TNO's class levels are kept apart). The engine picks the SPL
  ability for the caster level and refuses a spell whose first ability needs more, without a word (SpellPST).
- Test save: the Hive, `-Save 000000126` (many Hive thugs to turn hostile; `tbc_rules`, `tbc_sim`, `tbc_hive`).
  `tbc_hive.lua`: Swarm Curse pacing, Adder's Kiss, Whispering Flask/Embalming take the action, save sound.
- Vetting a spell by hand: `python -I tools/spell_vet.py RES ...` (description, the suite caster's SPL header, plan row,
  last measurement, vetted?), `python -I tools/spell_vet.py --log` (per spell of the last suite run: effects before/after
  as op:type:ticks, onsets, saves, holds), `tools\spell_follow.ps1 -Caster Grace -Cast 'SpellNoDec("Morte",1302)' -Owner
  Morte|self|foe|hit -Stats 14,84 -Turns N [-Level L] [-Saves pass]` (one spell turn by turn: effects, stats, state,
  icons of its owner at each of its turn starts). Vetted spells: `tests/unit/spell_expect.h`.
- Game resources: `python -I tools/ieres.py spl|itm NAME` (effects per header), `desc NAME` (TLK description),
  `named REGEX` (SPL/ITM by TLK name), `find REGEX [EXT]`, `raw NAME.EXT [OUT]` (override, then chitin.key/BIFs).
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
  Shadows (`tests\ingame\lighting_shadows.lua`, same env, save 126): `[lighting] bam_shadows=0` NOPs the four
  "cell has shadow" checks so palette entry 1 (the BAM blob) is always cleared; our contact shadows (ellipse per
  creature draw) + key shadows replace it. Probe `PSTF_LightShadows()` -> contact count, bam hidden, buffer used,
  last creature light x/y/red. Creature lights: `data/vfxlight.tsv` `anim:N` rows (Ignus `anim:36 ... noshadow
  glow=????IGYB`): at the creature's SHOWN position (`engine::InterpDrawPos`, smooth-interpolated), steady;
  noshadow = nothing blocks it, no contact/key shadow; glow = its BAMs are emissive. Probe
  `PSTF_LightCreatureTrace(frames)` then `()` -> samples, moving, holds, largest step px (light, sprite anchor):
  per drawn frame, so it sees the draw-only frames between ticks (the walk check in lighting_shadows.lua). Engine fact: PST realizes creature palettes without flag 4
  (cdb: 0x300C0), the blob alpha is the BAM palette's own alpha byte.
  Sprite AO + painted emitter glow + hand area lights (`tests\ingame\lighting_ao.lua`, save `000000076` — 126 has no
  baked emitter in view; same env as lighting_sprites): `[lighting] sprite_ao` (ao.bin at the creature's feet scales its
  ambient only), `painted_glow` (a baked light's painted flame at (x, y - z) goes into the emissive buffer), `area_lights`
  (`data/arealights.tsv` -> `assets\lighting\`, lights.txt fields after the WED). Probes `PSTF_LightAo(x,y)` -> AO
  factor, raw ao, hand area lights here, painted emitters drawn; `PSTF_LightSpriteMean()` -> mean luma of the lit
  creature pixels (measured next frame). Baldur.lua `Disable Movies` must be 1: with movies on, `PSTF_TEST_TICK=1`
  makes autoload fail at once (`autoload: FAILED, timeout (screen other)`).
  Day/night (`tests\ingame\lighting_night.lua`, night save `000000166`): the engine's time-of-day tint (CInfinity
  +0x2EC; applied = min(+0x2E4, +0x2EC) + +0x2E0) is divided out in the composite and replaced by our ambient
  (moonlight / dusk warmth / night light gain / lightmap pools, `[lighting] day_night` keys). Probe
  `PSTF_LightGlobal()` -> tod, applied, day/night flag; log `lighting: time of day: ...` when it changes.
- Gore (`[gore]`, `src/modules/gore/`, GemRB ScenePolish port): `CGameSprite::AddBlood(dir, type)` hook; gushes 3/4/5,
  artery 6, exploding death 7 become material effects (RESDATA.INI `armor` per animation, `data/hitarmor.tsv` from
  `python -I tools/hitmat.py` - re-run after a mod changes RESDATA - refined by `data/hitmat.2da` ANIM/ARMOR rows; EE
  animation ids go past 255, exact id first, then the low byte). Crits 10..17 keep their animation plus an
  exaggerated spray. Stains draw at the floor point (after `CInfinity::Render`, before objects: under the actors,
  lit by the lighting pass). Test: `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\gore.lua -Env @{PSTF_TEST_TICK='1';
  PSTF_TBC_AUTOPARTY='1'} -Until 'gore: ok|TEST FAIL' -Close -Timeout 300`. Probes: `PSTF_GoreState()`,
  `PSTF_GoreHit(id, severity[, crit[, anim]])`. Deploy upgrades an existing ini (new keys get their defaults).
- Turn-based APR + menu screens: `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_apr.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'tbc_apr: ok|TEST FAIL' -Close -Timeout 400`
  (TNO 1 1/2 APR swings 2,1,2,1; the inventory mid-fight keeps fight and order). Probe `PSTF_TbcAttacks()` -> wanted,
  swung, APR x2 of the active creature; combat.log `APR` lines. Path preview vs engine walk: `tests\ingame\tbc_path.lua`
  (same env without AUTOPARTY, `-Until 'tbc_path: ok|TEST FAIL'`); probes `PSTF_PreviewPts(x,y[,id])`, `PSTF_EnginePath(id)`,
  `PSTF_SearchByte(x,y)` (search-map occupancy: a standing creature is 0x02, dear ground to the engine's search, not a wall).
  `PSTF_CenterOn(id)` (initiative card click). BG3 bar layout = a UI.MENU text transform (`src/modules/tbc/ui_layout.h`,
  log `tbc: BG3 layout: ...`); hotbar BAMs `data/override/PF*.BAM` from `python -I tools/make_ui_bams.py` (deploy copies
  them into `<game>\override`). `launch.ps1`/`deploy.ps1` close a running game: check `Get-Process Torment` first, the
  user may be playing.
- HUD across screens (`tests\ingame\tbc_screens.lua`, save 126, env `PSTF_TEST_TICK=1`, `-Until 'tbc_screens: ok|TEST FAIL'`):
  item icon aspect, Blessing status icon, hover card out of a fight, hotbar pages, bar after the inventory,
  nothing of ours on the options screen. Our menus (PSTF_BAR, PSTF_PORTRAITS, PSTF_STATUS, PSTF_TBC) are put on/off
  the stack every frame by Lua `pstfHudGuard()` (an engine bar pushed again goes over ours: `pstfHudRaise()` from
  its onOpen re-pushes ours). Probes: `PSTF_Status(id)` (icons + raw parent effects), `PSTF_IconSize(bam)` (frame 0
  of cycle 0: BAM V1/BAMC/V2), Lua `pstfStat`, `pstfHov`, `pstfBarPages()`. `src/core/gameres.*` reads override +
  chitin.key/BIF (BAM sizes, SPL/ITM icon and name). CGameEffect parent: +0x90 type, +0x94 resref, +0x9C flags.
- 8-member party: `docs/PARTY8_PLAN.md`; `python -I tools/party_refs.py [--list|--bounds]` lists every use of the
  CInfGame party arrays; `python -I tools/party_scan.py` lists 6-sized locals / "%d" names in the functions we patched.
  Tests (Hive): `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\party8.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'}
  -Until 'party8: ok|TEST FAIL' -Close -Timeout 200` (joins to 8, quick-saves = `000000001`), then with `-Save 000000001`:
  `party8_load.lua` (`-Until 'party8_load: ok|TEST FAIL'`), `party8_formation.lua` (same env), `party8_fight.lua`
  (env `PSTF_TEST_TICK=1; PSTF_TBC_AUTOPARTY=1`, `-Timeout 300`), `party8_screens.lua` (env `PSTF_DLG_TEST=1;
  PSTF_TBC_NOFIGHT=1`, `-Until 'party8_screens: ok|TEST FAIL|faulted'`: record/inventory/spells/map for all 8).
  Probes: `PSTF_Party/PartyNames/Trigger/CanSave/Formation/UnselectAll/ScreenCaches`. Open menu screens in
  tests with `e:GetActiveEngine():OnLeftPanelButtonClick(N)` (1 map, 3 inventory, 4 record, 5 mage, 6 priest),
  not `SelectEngine` (skips the screen's portrait update: empty record sheet).
  Screen objects with per-member arrays grown for 8 (party_patches.tsv): CScreenInventory ground-pile cache,
  CScreenMap markers. Stable 8-member save: `-Save 000000200` (`tests\saves\000000200-PSTF-8-Party`; the
  quick saves 000000001-* get overwritten by party8.lua). Spawned companions need their joining dialogue's
  own actions (Vhailor: `Polymorph(Vhailor)`, else the statue animation has no portrait/paper doll). `CheckSymbol` accepts bytes the party layout retargeted (manifest rewrite).
- Maneuvers (`[tbc] maneuvers`, `opportunity_attacks`; rules in `src/modules/tbc/maneuvers.h`, GemRB fork port): Run,
  Withdraw (party side only), Block, Trip/Knockdown on the hotbar (Block wears the engine's Guard icon; Guard is left out),
  directed AI (`DirectedAi`: run to close, block when hurt and surrounded, 20 % takedowns), attacks of opportunity.
  Melee swings are Swing's Hit call **site 3** on Windows (Ignus' fire = site 1; roll log `(site N)`): the THAC0 / outcome
  filter sits there. Knocked down = op 0 AC -4 for 5 s (save vs death; +4 to hit it, gone with its round clock at its
  next turn start) via `engine::ApplyEffect` (DecodeEffect + vtable AddEffect); no op 39 (its sleep overlay). The pose
  is ours: `engine::PoseLying` holds the PST animation's die cycle (anim SetSequence vtable +0x170, cell frame +0x118),
  played backwards to get up, from the CGameSprite::AIUpdate slot hook (`InstallSpriteUpdate`), which also paces the
  trip swing (half speed to the hit, 2x after; `TripPace`; combat.log `PACE` lines per tick: phase, updates, anim run/cycle/frame/seq). A trip hit plays the engine's armour hit sound
  (`PlayArmorHitSound`) and the target's damage sequence. Engine melee reach = Chebyshev cells (CAIUtil::CountSquares)
  + RANGE_FUDGE 1 while sprite+0x49BC is set; a reaction starts a new attack round (+0x48B2 = 0xFFFF) or it waits a
  whole round to swing. Withdraw walks: the walk/run cycle is chosen only in the anim's SetSequence (re-set on a flip).
  Path points are sparse waypoints; Ignus floats (no walk path: AoO caught a tick late, his update paused for the swing).
  `[tbc] no_push` = CheckBumpable redirected (2 call sites): combatants are never pushed in a fight.
  Tests: `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_moves.lua -Env @{PSTF_TEST_TICK='1'} -Until
  'tbc_moves: ok|TEST FAIL' -Close -Timeout 400` and `tbc_moves2.lua` (same, `-Until 'tbc_moves2: ok|TEST FAIL' -Timeout 600`:
  withdraw walk cycle, AoO swing, knock-down AC +4 / fall / get up, Submerge the Will icon + shield on the round clock,
  no walking in place). Probes: `PSTF_Maneuver(name)`, `PSTF_Stance(id)`, `PSTF_Aoo()`, `PSTF_Class(id)` (CLASS/SPECIFIC.IDS),
  `PSTF_MoveStats()` (13th = AI short runs), `PSTF_AnimProbe(id)` (run flag, cycle, frame, sequence), `PSTF_Pose(id)`,
  `PSTF_Attached(id)` (PST attached spell objects: ticks to each end); env `PSTF_AOO_TRACE=N` (reach checks + reaction
  ticks); combat.log `MOVE`/`AOO`/`AI`/`PUSH` lines. Sounds `data/override/PFSND*.WAV` (Ogg in WAV, from the GemRB fork).
- Maneuver ground effects = filled noisy puffs (`src/modules/tbc/ground_fx.h`, `engine::DrawSoftPuffs`): Run dust burst +
  trail, Withdraw smoke burst + trail, knock-down dust. Look at them: `tools\launch.ps1 -Save 000000126 -Lua
  tests\ingame\tbc_groundfx.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'; PSTF_SHOT_DIR='<dir>'; PSTF_SHOT_EVERY_MS='150'}
  -Until 'tbc_groundfx: ok' -Close` (TNO stands at ~572,475 of the half-size shot). Probe `PSTF_GroundFx(kind, id)`.
  Trip hits play the crushing armour sound (`PlayArmorHitSound(id, sym::kHitSoundCrushing)`, HIT_03<armor>).
- Delayed effects that arrived together are classified together (`SyncSpellOnsets`): a 75-tick onset timed with its
  spell's visuals resolves now (Stygian Ice Storm's 8d8 lands with the ice drop; log `..., with its spell's visuals`).
  Test: `tools\launch.ps1 -Save 000000990 -Lua tests\ingame\tbc_ice.lua -Env @{PSTF_TEST_TICK='1'} -Until 'tbc_ice: ok|TEST FAIL' -Close`.
  A projectile born outside its caster's turn flies on (`ProjectilePace`): `tests\ingame\tbc_ignus_fire.lua` (save 990,
  same env, `-Until 'tbc_ignus_fire: ok|TEST FAIL'`; Ignus' fire starts the fight and must land before his turn).
- Buff/debuff timing: `tools\launch.ps1 -Save 000000990 -Lua tests\ingame\tbc_decay.lua -Env @{PSTF_TEST_TICK='1'} -Until
  'tbc_decay: ok|TEST FAIL' -Close -Timeout 600` (Blessing, Vilquar's Eye, Curse, Prayer, Submerge: on in the cast turn,
  icon -1 per owner turn start, gone on the right turn). `ChangeStat` is queued: set levels before the fight starts.
- Buffs: PST attached spell objects (op 375: Submerge the Will, shields) end on their own world-time stamp (+0x3FC),
  outside the effect lists: `ShiftEffectClock` moves them too. Their effects (and Power of One's) have no parent
  resref: `AttributeOrphans` (status.h) gives them the spell seen on its way (a delayed effect with a parent).
- All spells: `tools\make_spell_save.ps1` builds `tests\saves\000000990-PSTF-AllSpells` (Grace joins, 3M XP, TNO knows
  every mage spell, Grace every priest spell; `C:AddSpell`) and copies it to the save folder; `tools\install_test_saves.ps1`
  copies `tests\saves\*` there on a new machine. `tools\spell_suite.ps1 [-Only SPWI119,...]` casts every spell in a fight
  (`tests\ingame\tbc_spells_all.lua`, ~17 min, `-Trace` logs the caster's action/aura/attached objects) and writes
  `tests\unit\spell_results.h` (only when the run passed); `test_spells.cpp` needs `ok` = a real engine cast for every
  spell in `tests\unit\spell_table.h` (`python -I tools/spell_list.py`, also `tests\ingame\spell_list.lua`). A new spell
  (mod) = re-run both. The suite raises levels (TNO 20, Grace 14), casts area spells at the creature (a scripted
  `SpellPointNoDec` at [x.y] is dropped by the engine for most), removes each spell's effects after it (Fist of Iron's
  op 60 spell failure fails every later cast), jumps the caster beside a foe > 20 ft off, retries a miss once.
  Outcome columns per spell (log line + spell_results.h): `st` derived stat deltas, `state` new STATE bits, `icon` the
  spell's status icon (rN:buff|debuff) at the end of the caster's turn, `chp`/`cst` caster HP/stats; the foe always fails
  saves (`-Saves pass`: always saves) and has 1000 max HP. Plan per spell: `docs/SPELL_TEST_PLAN.md`.
  Probes: `PSTF_Stat(id, stat...)` (any derived stat by STATS.IDS number via CDerivedStats::GetAtOffset, one "v,v,.."
  string), `PSTF_SetHp(id, hp, max)` (max = base max HP too: the engine clamps HP to max), `PSTF_Status(id[, quiet])`
  (count, "RES BAM strref rN buff|debuff;" list), `PSTF_State(id[, quiet])`. Probes: `PSTF_Casts(id)` (the engine's "casts" feedback count; the FeedBack hook is on with tbc, env
  PSTF_FEEDBACK_LOG also logs every FeedBack), `PSTF_Action(id[, quiet])` (action, busy, path; the log line adds
  SpellPST's cast flag and aura counter), `PSTF_Unapply(id, RES)` (CGameSprite::UnapplySpell + attached objects end).
  Engine facts: the casting aura (sprite word +0x46A8, -1 = may cast) counts the caster's own AI ticks up to
  ROUND_LENGTH (DoAction); the round clock steps it at turn start (`StepCastAura`), else a caster casts every few turns.
  A cast with no casting time (Identify) never shows its action to the economy: its cast feedback spends the action.
  Long casts (Rune of Torment, speed 9) are not cut by the one-round attack limit (`IsCastAction`, 4-round cap).
- Dialogue ability rolls (`[dialogue] rolls`, `src/modules/dlgroll/`, rules in `roll_logic.h`): a reply the engine hides only
  because the Protagonist's STR/DEX/CON/INT/WIS/CHR is too low is listed obscured; clicking it opens the BG3 roll panel
  (d20 + floor((ability-10)/2) vs max(10, (needed-10)*2), nat 20/1). A win re-evaluates the node's replies with the engine's
  own `CGameDialogReply::Hold` and the ability set to the needed value (that node only). Hook: `CGameDialogEntry::Display`;
  `Hold` is called directly (its only caller is Display). Panel timing follows `sounds/check_success.wav` (starts at the
  throw: land 2.0 s, bonus 4.4 s, pop 5.3 s, banner 6.25 s; failure banner 7.0 s). Art: `python -I tools/make_dice_bams.py
  [--only NAME,..] [--preview OUT.png]` -> `data/override/PFD*.BAM` (BAMC, palette alpha). DLG conditions:
  `python -I tools/dlg_checks.py dump DLGNAME [REGEX]` / `scan [--start]`. Test: `tools\launch.ps1 -Save 000000126 -Lua
  tests\ingame\dlgroll.lua -Env @{PSTF_DLG_TEST='1'; PSTF_TBC_NOFIGHT='1'} -Until 'dlgroll: ok|TEST FAIL' -Close -Timeout 200`
  (DSKELW's STR-gated "pry out the joint bolts": fail, leave, re-enter, win, pick; shots `logs\dlg_*.bmp`). Env
  `PSTF_DLG_TEST=1` runs Lua `pstfDlgTest()` every main-loop iteration (dialogue holds the world still). Probes:
  `PSTF_DlgState()` (rolls, overrides, engine reply numbers, conditions, abilities), `PSTF_DlgNode()`, `PSTF_RollForce(n)`
  (next natural d20 of any roll panel), Lua `pstfDlg.rows`, `pstfDr` (panel), `pstfDlgFakeMouse`. Engine facts: reply flags +0 (1 text, 2 cond,
  8 end), number byte +0xA4 (-1 hidden), condition text +0xA8; CGameDialogSprite +0x54 = dialogue running.
- Roll panel (`src/modules/rolls/`): the d20 rule + timeline (`d20.h`) and the PSTF_ROLL menu (`roll_panel.cpp`) shared by
  the dialogue and lock rolls; a module opens it with Lua `pstfDrOpen{title, ab, card, mod, dc, roll, done, alive, back,
  note, pause}` (see roll_panel.h). Lock rolls (`[locks] rolls`, `src/modules/locks/`, rules `lock_rules.h`, adapter
  `engine/locks.*`): the ExecuteAction calls of PickLock/BashDoor are redirected; at the lock a party member waits
  (ACTION_NORMAL) while the panel is up (world paused), then the engine's own action runs with its UtilRandInt call site
  forced, so triggers/sounds/XP stay the engine's. Difficulty 100 = key only (vanilla, no panel). Test:
  `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\locks.lua -Env @{PSTF_DLG_TEST='1'; PSTF_TBC_NOFIGHT='1'} -Until
  'locks: ok|TEST FAIL' -Close -Timeout 240` (the Hive door ar0400d1: fail, blocked retry, bash, key-only; shots
  `logs\lock_*.bmp`). Probes: `PSTF_Locks(actor, radius)` (id:door|box:locked:difficulty:x,y:name), `PSTF_SetLock(id,
  locked, difficulty)`, `PSTF_LockState()`, `PSTF_LockForget()`; log `locks: ...` per approach/roll. The saves on disk
  have no containers in view (Hive outdoor = doors only); a door's script name is +0x5E8, a container's +0x478.
- Real-time 3D d20 (`[rolls] die_3d`, default 1): motion/geometry pure in `src/modules/rolls/die3d.h` (faces, numbers
  and rest poses equal `make_dice_bams.py`'s; unit `test_die3d.cpp`), GL in `src/engine/die_draw.cpp`, drawn from the
  die item's `frame lua` (drawItem evaluates it mid-UI-pass: flush, draw, restore, so later items stay on top), frame 174
  of PFD20 = blank; the BAM is the fallback. Number atlas `data/d20_numbers.bin` (`make_dice_bams.py --only D20NUM`,
  deployed to `pst_forever\assets\dice\`). Test: `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\die3d.lua -Env
  @{PSTF_DLG_TEST='1'; PSTF_TBC_NOFIGHT='1'} -Until 'die3d: ok|TEST FAIL' -Close` (shots `logs\die3d_*.bmp`; Lua
  `PSTF_DIE3D = false` = the toggle check). Probe `PSTF_DieState([1])` -> on/off, draws, failures, centre pixel, why.
- Hotbar with long lists + right-click + hidden maneuvers: `tools\launch.ps1 -Save 000000990 -Lua tests\ingame\tbc_bar2.lua
  -Env @{PSTF_TEST_TICK='1'} -Until 'tbc_bar2: ok|TEST FAIL' -Close` (shot `logs\bar2_desc.bmp`). The engine shows 10 of a
  list longer than 12 (scroll arrows in slots 0/11, `CInfButtonArray::pList` + scroll index +0x2264): probes `PSTF_BarCount()`,
  `PSTF_BarScroll(index)` -> first, last slot. Right-click = `OnRButtonPressed` (engine spell/item description); maneuvers
  open our `PSTF_DESC` (Lua `pstfMoveDesc`).
- Enemy walk animation (no gliding) + AI Run by path length: `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_walk.lua
  -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'tbc_walk: ok|TEST FAIL' -Close -Timeout 300`. Engine fact: an AI's
  Attack chase turns running off every tick; re-setting the walk sequence each time froze its frame (ForceRun now only
  re-sets when the cycle 4/5 must change). combat.log `AI ... path X ft of 31` lines.
- Missile of Patience: `tools\launch.ps1 -Save 000000990 -Lua tests\ingame\tbc_mop.lua -Env @{PSTF_TEST_TICK='1';
  PSTF_FEEDBACK_LOG='1'} -Until 'tbc_mop: ok|TEST FAIL' -Close` (3 shots, then gone). Probes `PSTF_Cursor()` (CInfGame
  +0x78B8, 20 = a hovering missile waits), `PSTF_MopFire(caster, target)` (a target click: caster +0x5124 = id+1).
- Spell glow at night: `tools\launch.ps1 -Save 000000990 -Lua tests\ingame\lighting_hammer.lua -Env @{PSTF_TEST_TICK='1';
  PSTF_TBC_NOFIGHT='1'} -Until 'lighting_hammer: ok|TEST FAIL' -Close` (shots `logs\hammer_night_<lit|off>_NN.bmp`; prefix
  `pstfHammerRuns = {{'night','lit'},{'night','x','fx_soften',0},...}` for A/B). `DayNight(12)` does not change the tint
  in a running area. Engine facts: effects are added untinted; `fx_soften`'s screen blend ignores the night tint, so it is
  day-only (`SoftenAt`).
- Spell visuals lit vs vanilla, night and day: `tests\ingame\lighting_spellfx.lua` (save 000000990; a generated copy with a
  prefix `pstfFxList = {{'Nameless','SPWI803',2803,'foe',260},...}`, `pstfFxModes = {{'night','alt'},{'day','alt'}}`,
  `pstfFxEvery`, `pstfFxTrace = 'A0,SP2'`, `pstfFxSets = {nomask={'untinted_fx',0,1}}`); mode `alt` shoots each tick lit
  and the next drawn frame with our pass off (same animation frame), `raw` = debug 7 (emissive capture), `untint` =
  debug 8 (red untinted cover, green unlit cover). `python -I tools/fx_sheet.py [--crop L,T,R,B] [--scale K]
  [--ticks ..] [--only RES]` -> `logs\fx_sheet_<RES>.png` (columns lit/off). `PSTF_LightTrace('A0,SP2')` takes a
  comma list and logs each draw's colour (DrawColor, `kVaDrawColor`) too.
  Engine facts: every cell draw decodes into a scratch-surface region (`kVaScratch`); the batch flush uploads and
  resets it, so a re-draw after a flush must put the region back (gl_world `redraw`), or it samples another object
  (was: a creature inside Spiritual Hammer, an Ignus over Ignus). Palettes get the night tint only with draw flag
  0x10000 (creatures 0x300C0); most spell BAMs are 0x40/0x41: untinted, so the composite keeps them as drawn
  (`[lighting] untinted_fx`, effect mask buffer). FX blits are premultiplied (glow pixels alpha 0).
  `[lighting] self_lit`: Ignus (vfxlight `glow=`) is unlit at night (capture alpha). `[tbc] ground_fx`: Run/Withdraw
  rings, dust, smoke drawn soft at the floor point (`engine/soft_draw.h`), under the creatures.
- All-spells save: `tools\make_spell_save.ps1 -Keep` re-teaches from the save as it is (Nameless One, Ignus, Dak'kon all
  mage spells; Grace priest), rests and puts the hour back (`DayNight`). The save is at night (hour 1).
- Voice cuts (`[voice] cut`): a new examine or a dialogue opening clears NARRATIO (channel 0), a dialogue closing
  clears channel 7 (dialogue voice), via `CSoundMixer::ClearChannel` (win 140495FD0). Test: `tools\launch.ps1 -Save
  000000126 -Lua tests\ingame\voice_cut.lua -Env @{PSTF_DLG_TEST='1'; PSTF_TBC_NOFIGHT='1'} -Until 'voice_cut: ok|TEST FAIL'
  -Close` (`cut=0` fails with 2 voices on NARRATIO: the toggle check). Probes `PSTF_VoiceCount(channel)`,
  `PSTF_Examine(strref)` (plays through the examine path; 828/856 = voiced Mortuary lines). Log `voice: dialogue ...`.
  Engine fact: channels 0 and 7..13 are "clear on play" yet a fire-and-forget examine line still stacks on channel 0.
- Status-effect visuals (`[statusfx]`, `src/modules/statusfx/`, pure logic `statusfx_logic.h`): STATE bits -> our particle
  effects (panic = black burning skulls swirling round the head, stunned = stars + lightning, blind = growing glints,
  poisoned = acid sparks + green burst, silenced = the same white), timings/rates/sizes ported from the NWN models
  vdr_mindhit04/vim_mentstun/vdr_mindhit03/vim_blinddeaf/vim_shwng*01/vim_cntmantle (numbers only; art ours:
  `python -I tools/make_statusfx_art.py [--preview OUT.png]` -> `data/statusfx_atlas.bin` + `data/override/PFVBLANK.BAM`).
  Timed colour glows (op 8/9/51/52/61) end after `glow_seconds` (engine::ExpireEffectNow), at once for a replaced
  state's spell; a blinding spell's pending op 369 overlay is pointed at PFVBLANK. Magic resistance ("Spell resisted",
  CheckSave resist mark, symbols.h kRvaResistMark) plays the spell mantle + `sounds/spell_resist.wav`. Head height =
  the engine's overhead-text height (`engine::SpriteTextHeight`). Test: `tools\launch.ps1 -Save 000000126 -Lua
  tests\ingame\statusfx.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'statusfx: ok|TEST FAIL' -Close
  -Timeout 240`; look: `tests\ingame\statusfx_look.lua` (`-Until 'statusfx_look: ok|TEST FAIL'`) then
  `python -I tools/statusfx_sheet.py`. Probes: `PSTF_StatusFx()`, `PSTF_StatusFxOn(id)`, `PSTF_StatusFxPlay(id, fx|stop:fx)`,
  `PSTF_StatusFxApply(id, op, p1, p2, s)`, `PSTF_StatusFxEffects(id)` (op:type:parent:res:equipped:param1 list).
  `ApplySpellRES` skips the magic resistance roll: test MR with a real cast. In a fight a creature waiting for its turn
  re-evaluates its effects every tick but its expiry ticks are held, so a cut glow is also made colourless at once
  (`engine::QuietColorEffect`: param1 neutral + colour refresh flag sprite +0x48CC): `tests\ingame\statusfx_tbc.lua`
  (env `PSTF_TEST_TICK=1; PSTF_TBC_AUTOPARTY=1`, `-Until 'statusfx_tbc: ok|TEST FAIL'`, shots `logs\statusfx_tbc_{pulse,quiet}.bmp`).
- Saving-throw banner (`[tbc] save_banner`, rules `src/modules/tbc/save_banner.h`, art `python -I tools/make_save_bams.py
  [--preview OUT.png]` -> `data/override/PFSVOK.BAM`/`PFSVBAD.BAM`, reference clip youtube.com/shorts/WyovFSwwKvg): every save,
  made or failed, shows "[icon] Save Success: N (Death)" over the creature (UI menus PSTF_SAVE1..6, one each so each fades
  alone with menu `opacity lua`), sound at the icon's verdict (0.7 s), gone at 3 s. The engine's own "Save vs." line is
  patched out (`engine::SilenceSaveFeedback`, symbols.h kRvaSaveFeedbackRead). Saves are taken out of fights too. Test:
  `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\save_banner.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'}
  -Until 'save_banner: ok|TEST FAIL' -Close` (shots `logs\save_banner_NN.bmp`). Probe `PSTF_SaveBanner([i])`.
  Save stats in tests: `ChangeStat(Myself,SAVEVSSPELL,1,SET)` always saves, 30 never (-10 does not save).
- BG3 combat text (`[tbc] combat_text`, rules `src/modules/tbc/combat_text.h`, reference youtube.com/watch?v=mfZz9N57glw):
  nothing rises; a line sits at the world point where the creature's head was (slots from the top down, neighbours
  share rows), fades at 2.3-2.8 s. Damage = bare number coloured by damage type (white weapons), numbers within
  0.4 s share a row, heals green, "Miss!" xor "Critical Miss!", "Critical Hit!" red, a status's name on start and
  struck through (left to right) on end, casts show the spell name, "Trip: Missed/Failed/Success"; the save banner takes a slot.
  Engine side: every rising text goes through `CGameArea::AddToFloaterList` (`engine::InstallFloaterHook`; type 5 =
  a creature's own words, left to the engine), damage type from `CGameEffectDamage::ApplyEffect`'s param2. Test:
  `tools\launch.ps1 -Save 000000126 -Lua tests\ingame\combat_text.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'}
  -Until 'combat_text: ok|TEST FAIL' -Close -Timeout 400` (`combat_text=0` fails it at the first line: the toggle check).
  Probe `PSTF_CombatText(i)` -> on, ui x/y, alpha, strike, kind, n, text/colour x4, id, slot; `()` -> lines, floaters
  seen, taken, added. Log `tbc: floater "..."` (first 80) and `tbc: text over X: ...`. UI text widths: Lua
  `pstfTextWidth(t, pt)` (Infinity_GetContentHeight only wraps between words: one word needs the ' |' calibration).
  Shots of UI need `PSTF_SHOT_DIR` (`PSTF_LightShot` is the world pass only, no menus).
- Neutrals in fights (census.h): a neutral (EA 31..199) joins only while fighting a combatant (its attack/spell target
  is one, or a combatant targets it: sprite +0x4940 target id, `UpdateTarget`), on the other side; a combatant whose EA
  crosses a cutoff changes sides (`Director::SetSide`, log `tbc: X changes sides`). Test: `tools\launch.ps1 -Save 000000126
  -Lua tests\ingame\tbc_neutral.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'tbc_neutral: ok|TEST FAIL' -Close
  -Timeout 600`. Probe `PSTF_Side(id)` -> side (0/1 party/2 hostile), target, action, EA, script name. Hive thug 0x140014's AI
  does not run as a neutral (actions never start): stage with 0x220022. PST EA.IDS has no GOODCUTOFF/EVILCUTOFF (`[ENEMY]` works).
- Creature scripts: `python -I tools/bcs.py dump NAME | users NAME | who CRE | grep REGEX | audit` (BCS decompiler,
  which CRE/area actor runs a script, turn-based risk audit; index cache `build/bcs_index.json`, delete it after a mod
  changes scripts). Engine facts: INSTANT.IDS actions run at once; per-creature script timers (`StartTimer`) count down
  only inside the creature's own ProcessAI (CheckTimers every 15 of its AI updates), so held creatures' timers stand
  still; global timers run on world time. An AI turn ends at the first Wait after its action and ClearAI drops the
  rest of its queue: log `tbc: X: turn end drops queue [...] (script state lost)`, combat.log `QUEUE` / `TIMER` lines.
- Scenario tests in the player's walkthrough saves (`docs/SCENARIO_TEST_PLAN.md`: 50 hard fights/scripts, outcome per
  scenario): `tools\install_walk_saves.ps1` copies `Documents\...\all saves\save` as slot + 700000 (`000700162` = "144
  Spoke To Ravel"); `tools\scenario.ps1 -Id S14 [-Shots]` runs `tests\ingame\scenarios\S14_*.lua` behind `lib.lua`
  (header `-- scenario: save=.. env=.. timeout=..`; invariants held/stuck/endless; log scan; exit 0 ok, 1 ISSUE, 2 FAIL);
  `-Id S00 -Save PREFIX` lists a save's creatures. Probes: `PSTF_Queue(id)`, `PSTF_Timers(id)`, `PSTF_FindActor(name[, n])`,
  `PSTF_Actors([r])`, `PSTF_ActorRefs()`; read a variable with `PSTF_Trigger(who, 0x400F, value, '', 'AR0403MortalKombat')`
  (Lua `S.global`). Unattended fights need env `PSTF_TBC_AUTOPARTY=1`. Spawned CREs lack their area actor's script:
  `ChangeAIScript("SCRIPT",0)` (see `bcs.py who CRE`). `lib.lua` drivers: `S.stepCaster{name, cre, script, last}`
  (LOCALS counter, BCS `LOCALScd_int_0` = LOCALS + `cd_int_0`) and `S.chainCaster{name, casts, turns, done}`; S.find takes
  the shown name; one action per `PSTF_EvalAs` (C:Eval runs only the first); saves that load in a banter dialogue are
  stepped closed (`worldScreen:StepDialog()`). `[tbc] keep_queue`: AI queue kept across turns (combat.log `QUEUE ... keeps`),
  held creatures' `ProcessPendingTriggers` waits for their turn.
- Combat log: `<game>\pst_forever\logs\combat.log` (wall clock, world tick, round, ticks into the turn, event):
  read it first after a fight.
