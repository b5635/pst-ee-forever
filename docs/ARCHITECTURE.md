# pst_forever: architecture

A native extension for **Planescape: Torment: Enhanced Edition** that hooks the running game and hosts
feature modules: turn-based combat, upscaled art, lighting/depth, examine voice-over, 8-member party,
and dev/test tooling. The game's own data and rules stay the truth; we coordinate the engine and do
not replace it.

## 1. Target (verified 2026-11-20)

| Item | Value |
|---|---|
| Install | `C:\GOG Games\Planescape Torment - Enhanced Edition` (GOG **3.2.2.0**, updated 2026-11-20) |
| Executable | `Torment.exe`, **PE32+ x64**, image base `0x140000000`, **no ASLR** (DllCharacteristics `0x8120`), no RTTI, no PDB shipped |
| SHA-256 | `99ae15860cfdbcdaad0f48e005eee21444046b21de848c1deab9367e8246a3c5` |
| Linux twin | `linux/Torment` from the same GOG 3.2.2 release (ELF x64, **not stripped**, 33,431 symbols), SHA-256 `e615763edb57427e3efded67296c357e2563ee40ebc2aef929e1f180e1b5846e`; identical `chitin.key` |
| Imports | KERNEL32, USER32, OPENGL32, OpenAL32, **VERSION** (3 functions), WS2_32, WINMM, IMM32, GDI32, ... |
| Engine | Lua 5.2 (internal state drives the EE UI `.menu` files), SDL2 static, OpenGL renderer, areas as PVRZ |
| Installed mods | PST-UB-Reloaded v2.5 (components 0 and 60), WeiDU in the game folder |

Only this exact binary is supported. Any other hash: the loader logs it and installs nothing.

## 2. Reverse-engineering material

| Path | What |
|---|---|
| `resources/re/lin/` | Linux 3.2.2 `Torment` (same release, x64, full symbol table): the named reference |
| `resources/re/lin/dump/`, `resources/re/win/dump/` | complete dumps: `all_Torment.c` (pseudocode, disassembly fallback on Hex-Rays failure), `all_Torment.asm`, `dec_all/`, `dec_all_named/`, `funcs/names/strings/calls/drefs/protos/imports/exports/segments`, `verify.json` (completeness proof) |
| `resources/re/win/` | Windows `Torment.exe` IDB, **the hook target** |
| `resources/re/win/dump/names_win.tsv` | Linux names ported onto Windows functions (method + score per row) |
| `resources/ext/InfinityLoader/` | Bubb's loader (design reference only). Its `LuaBindings-PSTEE` header is for 3.1.4 x86: field **names/order** are hints, its offsets are wrong for 3.2.2 x64 |
| `resources/iesdp-gh-pages/` | IESDP file formats, opcodes, script actions/triggers |
| `resources/re/notes/`, `resources/re/*.json`, `pssum.md`, `ee_pst_funcs.txt` | GemRB-era analysis of **3.1.4** (behaviour/data notes still useful; any addresses in them are stale) |

Rules:
- Read logic in the **Linux** pseudocode (named); take addresses, calling conventions and offsets from the
  **Windows** side. Both are x64 3.2.2, so struct offsets usually agree (GCC and MSVC lay out plain
  classes the same; vtables and destructors differ) but confirm each in the Windows code before use.
  Windows uses the Microsoft x64 convention (`this` in `rcx`).
- Ported names are evidence, not proof. Before hooking a function, confirm with `tools/re.py code X` on
  both sides (same strings/constants/calls) and record it in the symbol manifest.
- `tools/re.py` is the only way to search the dumps (see CLAUDE.md). Don't grep the 40 MB `.c` files by hand.
- When you identify a function by hand, add it to `resources/re/win/manual_names.tsv`
  (`win_ea<TAB>linux_name`); `match.py` treats those as seeds and propagates from them.

Pipeline: `bash tools/re_pipeline.sh` (~2.5 h machine time; `FROM=diff|match|dump` resumes a later stage).
1. `fixfuncs.py`: loose code becomes functions (Linux 26,005 -> 26,505, none left; Windows 16,829 -> 20,059,
   4 blocks left, dumped to `orphans.asm`).
2. `features.py` (strings, constants, member offsets, callees, function pointers, vtable slots, imports) and
   `vtscan.py` (Windows vtables; no RTTI).
3. Diaphora (`resources/ext/diaphora`, AGPL, dev tool only): export Linux + a fresh unnamed Windows IDB, diff.
4. `match.py`: our evidence first (unique strings/constants/offsets, vtables), then Diaphora candidates, then
   call-graph, neighbour, rare-token and sandwich propagation. Writes `names_win.tsv` and a custom-only copy.
5. `full_dump.py` on both, then `re.py build`.

**Diaphora on this pair (measured 2026-11-20):** GCC vs MSVC+LTCG defeats most of its heuristics. Against
string evidence its overall precision was 29%; "Related compilation unit" (7.3k results) was 2%, and the KOKA/
graph-shape hashes were near 0. Only heuristics measured at >= 85% are used (`DIA_OK` in `match.py`), and they
never override our string/vtable evidence. Re-measure if the build changes.

## 3. Runtime layout

```
<game>/version.dll            proxy loader (forwards the 3 VERSION imports to System32)
<game>/pst_forever.dll        core + built-in modules
<game>/pst_forever/
    pst_forever.ini           toggles and options (one section per module)
    Torment-3.2.2.syms        symbol manifest: name, RVA, expected bytes, prologue length, convention
    plugins/*.dll             third-party / optional plugins (C ABI, see 3.4)
    assets/                   upscaled art, lighting maps, voice files (large, not in git)
    logs/pst_forever.log      build fingerprint, hook status, errors
    tests/                    in-game test results (results.json)
```

### 3.1 Loader (`version.dll`)

`Torment.exe` imports `VERSION.dll` from the application folder first (it is not a KnownDLL), so a
proxy there loads with the game from GOG Galaxy, Steam or a shortcut, with no launcher and no file patching.
- DllMain: forward exports; patch one early main-thread point (found in our dump, in or right after
  WinMain's CRT start) with a 14-byte absolute jump after checking its expected bytes. Nothing else in DllMain.
- The stub runs on the main thread after the CRT is up: `LoadLibrary("pst_forever.dll")`, call
  `PSTF_Init()`, then resume the original instructions.
- If `pst_forever.dll` is missing or init fails, the game runs vanilla.

### 3.2 Core (`pst_forever.dll`)

| Part | Job |
|---|---|
| `VersionGuard` | SHA-256 of `Torment.exe`; every manifest entry's expected bytes checked before any patch |
| `Symbols` | loads `Torment-3.2.2.syms`; typed function pointers (Microsoft x64 convention) |
| `Hooks` | MinHook (BSD-2) inline hooks + IAT hooks; per-module ownership; all-or-nothing per module |
| `Engine` adapter | the only code that knows offsets: `engine/layout.h` (offsets + `static_assert` sizes), typed accessors (`Sprite`, `Area`, `Game`); every offset cites its evidence |
| `Events` | engine events modules subscribe to: `Tick`, `AreaLoaded`, `ActorCreated/Destroyed`, `RenderWorld`, `RenderUI`, `SwapBuffers`, `DialogStart/End`, `SaveLoaded` |
| `Lua` | registers a `PSTF` table in the engine's internal Lua 5.2 state (its `lua_*` functions are named in the Linux symbols and ported) so `.menu` UI and the console can call native code |
| `Config` | `pst_forever.ini`, live reload for dev |
| `Log` | file + optional console; off the hot path |

Threading: every engine access happens on the game thread, inside a hook. No worker thread touches
engine objects. Asset decoding may run on workers and hand results to the game thread.

### 3.3 Modules (built in, each with an ini toggle; disabled = no hooks installed)

| Module | What it hooks | Notes |
|---|---|---|
| `tbc` turn-based combat | AI update, action processing, path/step, attack round/swing, projectile, effects clock | plan: `TURN_BASED_COMBAT_EXTENSION_PLAN.md` |
| `hires` upscaled areas/sprites | PVRZ/texture upload, sprite frame upload, draw calls | GL-level texture substitution keeps engine coordinates; assets from the GemRB pipeline (`D:\gemrb\tools\upscale`, outputs in `C:\Games\GemRB\upscaled`) re-keyed to EE resrefs |
| `lighting` dynamic lights, depth, shadows, emissive/bloom, atmosphere | world pass (`CGameArea::Render`), the shared sprite draw (`sub_6873C0`), engine shader programs (`DrawInit_GL`) | plan and EE data checks: `docs/VISUALS_PLAN.md`. GemRB `LIGHTING_PLAN.md` data (`lights.txt`, height/normal/AO maps per WED) used as is + our own GLSL passes |
| `voice` examine voice-over | string display paths (infopoint/description/feedback) | plays WAV keyed by strref; voices from `D:\pst-voiceover-engine` (has EE CSVs). First check whether TLK sound resrefs alone already work |
| `smooth` sub-tick frame interpolation | `CBaldurChitin::AsynchronousUpdate` vtable slot, `SDL_Delay` in `CChitin::Update`, `CInfGame::SynchronousUpdate` inside the world draw pass, `CGameSprite::Render`/`RenderHealthBar`/`RenderMarkers`, `CGameTemporal::Render` | EE ticks the world once per loop iteration and caps the loop to 1000/fps ms, so draw rate == tick rate. The loop iterates at a whole division of the tick interval (`k >= 2` iterations per tick, `[smooth] frame_rate` as the target draw rate), so EE's tick rate — walk speed, effect durations, songs, day/night — stays exactly the same; iterations between ticks draw only (no logic, no game-sync). Creatures interpolate from the engine's own Q10 walk accumulators, projectiles from their previous position; a draw hook shifts pos (+0x0C/+0x10) for the duration of one engine draw call and restores it. Logic, scripts, saves and combat timing stay vanilla; movement is displayed one tick late. Math in `modules/smooth/smooth_logic.h` (GemRB `TickInterpolation`, ported in spirit) |
| `party8` 8 party members | every use of the 6-slot party arrays | **high risk**: `CInfGame::m_characters` is `Array<long,6>` (struct layout); needs relocation + patching of every `6` bound and the portrait UI |
| `dev` | named pipe console, in-game test runner, probes | dev builds only, see 4.2 |

### 3.4 Plugin ABI (attach our own things)

External plugins are DLLs in `pst_forever/plugins/` that export
`int PSTF_PluginInit(const PSTF_Api* api, PSTF_Plugin* self)`.
`PSTF_Api` is a versioned C struct: logging, config, event subscription, symbol lookup, hook
install/remove, Lua registration, and the engine adapter's stable handle API (actor IDs, never raw
pointers). C ABI so plugins can be built with any compiler. Built-in modules use the same API internally.

## 4. Testing (cheap first, game last)

| Layer | Command | Covers | Speed |
|---|---|---|---|
| L0 unit | `tools\check.ps1` (doctest, x64 host exe) | pure logic: initiative, path clipping, turn state machine against `FakeEngine`, ini parsing, manifest parsing | ms |
| L0 static hooks | `tools\check.ps1` runs `pstf_verify` | every manifest symbol: RVA in `.text`, expected bytes match the on-disk `Torment.exe`, prologue length is safe to patch | ms |
| L1 in-game | `tools\ingame.ps1 -Suite <name>` | game starts with `PSTF_TEST=<suite>`, loads a staged save, runs `PSTF_TEST_CASE`s on the real engine, writes `pst_forever/tests/results.json`, quits with the exit code | ~30 s |
| L2 live probe | `python tools\pstf.py <cmd>` | one-shot queries against a running game over `\\.\pipe\pst_forever`: `lua <code>`, `actors`, `actor <id>`, `hooks`, `shot` | instant |

Rules:
- Every change gets a test at the lowest layer that can catch it. Assert exact values from the evidence
  (EE pseudocode, data files). No mocks of engine *behaviour*: `FakeEngine` only feeds the director
  state the way the adapter would.
- When something is hard to check, build the probe first (a pipe command or an in-game assertion helper),
  then the test. List each new probe in CLAUDE.md.
- Scripts print only failures and a summary; full logs go to `build/logs/`.

## 5. Repository layout (planned)

```
CMakeLists.txt          x64 build (MSVC v142, -A x64)
src/loader/             version.dll proxy
src/core/               VersionGuard, Symbols, Hooks, Events, Lua, Config, Log
src/engine/             layout.h, adapter (the only place with offsets)
src/modules/<name>/     tbc, hires, lighting, voice, party8, dev
include/pstf_api.h      plugin C ABI
data/                   pst_forever.ini, Torment-3.2.2.syms (generated), Lua/.menu additions
tests/unit/             doctest
tests/ingame/           PSTF_TEST_CASE suites + staged saves
tools/                  re.py, check.ps1, deploy.ps1, ingame.ps1, pstf.py, gen_syms.py
resources/              RE material (large, not in git)
docs/                   this file, symbol map, feature plans
```

## 6. Decisions

- **License: GPL-2.0-or-later** (owner, 2026-11-20; text in `COPYING`). GemRB code (the fork's
  `CombatDirector`, `CombatAnalyzer`, rules, tests) may be ported directly; keep its copyright lines and add
  `SPDX-License-Identifier: GPL-2.0-or-later` to every source file. Third-party libraries must be
  GPL-compatible (MinHook BSD-2, doctest MIT: fine). InfinityLoader/EEex ship no license file: reference
  only, copy no code.

- **Frame rate is decoupled from the tick rate, not raised** (owner, 2026-11-21): raising EE's fps global
  would scale walk speed, animation rate and the `tick % 15` effect gate, because the engine treats one
  tick as one frame everywhere. Instead the smooth module keeps the tick rate and adds draw-only
  iterations, which costs exactly one tick of display latency (same trade GemRB makes). Known side
  effects, accepted as presentation: edge-scroll/keyboard panning accumulates per draw frame, so it moves
  proportionally faster; UI screen animations and the tbc camera easing run at the draw rate; floating
  damage and party-talk text (`CFloatMessage`/`CGameText`) keep the position they captured at spawn and
  do not follow an interpolated sprite; picking stays one tick behind the drawn sprite.

## 7. Open decisions

- Coexistence with EEex-PSTEE: it targets 3.1.4 x86 only and cannot run on 3.2.2; not a concern.
