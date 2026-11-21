# CLAUDE.md

Guidance for Claude Code in this repository.

## What this is

`pst_forever`: a native hook DLL (plus a `version.dll` proxy loader) that extends **Planescape: Torment:
Enhanced Edition** in place. Modules: turn-based combat, upscaled areas/sprites, dynamic lighting + depth,
examine voice-over, 8-member party, dev/test tooling. Architecture: `docs/ARCHITECTURE.md`.
Turn-based plan: `TURN_BASED_COMBAT_EXTENSION_PLAN.md`.

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
- `tools\ingame.ps1 -Suite NAME` runs an in-game test suite and returns its exit code.
- `python tools\pstf.py CMD` queries a running game over the dev pipe (`lua`, `actors`, `actor ID`, `hooks`, `shot`).
- RE tooling guards: `python -m pytest tests/tools -q` (exe hashes, dump completeness, key mappings).
- RE pipeline: `bash tools/re_pipeline.sh` (see `docs/ARCHITECTURE.md` §2); ~2.5 h, run in the background.
- Exist now: `check.ps1`, `deploy.ps1` (`-Remove` = back to vanilla). Not yet: `ingame.ps1`, `pstf.py`.
- Symbol manifest is compiled in: `src/engine/symbols.h` (shared by the DLL and `pstf_verify`). Unit tests use
  `tests/unit/mini_test.h` (no doctest dependency).
- Runtime log: `<game>\pst_forever\logs\pst_forever.log` (hook status, every examine line played).

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
- Probes so far: runtime log above (`voice: examine strref N (ms) played`).
- Lua probes (`[dev] lua=1`): `PSTF_Version/EngineMode/RoundLength/WorldTime/RoundsForSeconds/TbcSim/SelfTest`,
  callable from the in-game console (Ctrl+Space, needs `Cheats` or `Debug Mode` in Baldur.lua) or from a
  temporary `<game>\luaAuto.cfg` (runs at boot, before LoadOptions). Every call logs `lua: NAME -> value`.
  Details, engine facts and open questions for turn-based combat: `docs/TBC_RESEARCH.md`.
