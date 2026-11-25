---
name: setup
description: First-time developer setup for pst_forever - check the machine, report what is missing, build and verify. Use when someone runs /setup, has just cloned the repository, or asks how to start developing.
---

Set this machine up for developing pst_forever. The reference is `docs/DEVELOPING.md`; read it first.

1. Run `powershell -ExecutionPolicy Bypass -File tools\setup_check.ps1` (add `-Game DIR` if the game is not in
   the default folder; ask where it is if the check cannot find it).
2. Report what is missing, grouped as the script groups it. Ask what the person wants to do (build only, in-game
   tests, reverse engineering) and deal only with those groups.
3. Ask before each of these: installing software or Python packages, `tools\deploy.ps1` or `tools\launch.ps1`
   (both close a running game), `tools\install_test_saves.ps1` (replaces saves of the same name), editing
   `Baldur.lua`, and `tools/re_pipeline.sh` (hours; `fresh` deletes the IDA databases).
4. Never download the game, IDA or game binaries: the person supplies their own copies.
5. When the required group passes, run `tools\check.ps1` and report its summary and any failures as printed.

The permissions and git habits written in `CLAUDE.md` (installing tools without asking, committing directly on
`main`) are the maintainer's. Ask the person how they want those handled.
