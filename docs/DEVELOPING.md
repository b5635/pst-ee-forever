# Developing PST Forever

First-time setup, for people and for coding agents. With Claude Code, run `/setup` in the repository. With
another agent, ask it to follow this file.

Run all commands from the repository root in PowerShell.

## 0. See what you have

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_check.ps1
```

It changes nothing. Only the first group is needed to build.

## 1. Build and verify

You need:

- Windows x64.
- _Planescape: Torment: Enhanced Edition_, **GOG 3.2.2.0**. Other `Torment.exe` builds are not supported.
- Visual Studio 2019 Build Tools (v142) with "Desktop development with C++".
- CMake, installed in `C:\Program Files\CMake`.

```powershell
tools\check.ps1
```

This builds, runs the unit tests and checks every hook against your `Torment.exe`. If the game is somewhere
else, pass `-Exe 'D:\path\Torment.exe'`.

The scripts default to `C:\GOG Games\Planescape Torment - Enhanced Edition`. `check.ps1` takes `-Exe`;
`setup_check.ps1`, `deploy.ps1` and `launch.ps1` take `-Game`. Some Python tools and tests have the path
written in them.

## 2. Run it in the game

Back up your saves first. `deploy.ps1` and `launch.ps1` **close a running game without asking**.

```powershell
tools\deploy.ps1
```

`tools\deploy.ps1 -Remove` takes the mod out again.

For the in-game tests:

1. Start the game once, so that `Documents\Planescape Torment - Enhanced Edition\Baldur.lua` exists.
2. Install the test saves. This replaces saves with the same folder names.
   ```powershell
   tools\install_test_saves.ps1
   ```
3. For tests that set `PSTF_TEST_TICK=1`, set `Disable Movies` to `1` in `Baldur.lua` while the game is closed.
   This skips all movies, story ones included.
4. Run the smoke test:
   ```powershell
   tools\launch.ps1 -Lua tests\ingame\smoke.lua -Close
   ```

`launch.ps1` rewrites the `Last Save PST` line of `Baldur.lua` and deletes the mod's previous log.

Many tests use a save in the Hive named `000000126`, which is **not in the repository**. To run them, make a
save of your own in the Hive with thugs nearby and pass its folder prefix to `-Save`. Tests that depend on exact
positions or objects may fail on a different save.

## 3. Python tools

```powershell
python -m pip install pytest capstone Pillow numpy scipy
```

Regenerating sprite depth also needs `torch` and `transformers`.

## 4. Reverse engineering (only for new hooks or offsets)

The dumps are derived from the game's binaries, so they are not in git and `tools\re.py` does nothing on a fresh
clone. To build them you need:

- IDA Professional 9.1 with the x64 Hex-Rays decompiler.
- [Diaphora](https://github.com/joxeankoret/diaphora) cloned into `resources\ext\diaphora`.
- Your own `Torment.exe` in `resources\re\win\`.
- The Linux `Torment` binary of the same GOG release in `resources\re\lin\`.
- Python and Git Bash.

Then, in Git Bash (about 2.5 hours):

```bash
bash tools/re_pipeline.sh fresh
python -m pytest tests/tools -q
```

[ARCHITECTURE.md](ARCHITECTURE.md) section 2 explains the pipeline.

## Making a change

- Read [ARCHITECTURE.md](ARCHITECTURE.md) and the section of [CLAUDE.md](../CLAUDE.md) for the feature: it lists
  the tests and probes. Parts of that file describe the maintainer's machine and habits.
- Every change gets a test at the cheapest layer that can catch it: unit, then static, then in-game.
- Every hook goes through `src/engine/symbols.h` with expected bytes; on a mismatch the module turns itself off.
- Every new feature gets an ini toggle. With all toggles off the game behaves like vanilla.
- New source files start with `// SPDX-License-Identifier: GPL-2.0-or-later` (or the comment form of the
  language). Don't copy EEex or InfinityLoader code: it has no license.
- Don't commit game files, binaries or IDA databases.
