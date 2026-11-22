# AI Agent Workflow — PST:EE Forever

> **Purpose:** A repeatable, evidence-led workflow for DeepSeek Harness, Qwen, and other coding agents working in `D:\pst-ee-forever`. Use this alongside the repository's `CLAUDE.md` and `docs/ARCHITECTURE.md`. This file supplements those project rules; it does not override them.

## 1. Non-negotiable rules

1. **Inspect before editing.** Read `CLAUDE.md`, the relevant architecture/feature docs, the target source files, and the current Git state before proposing or changing code.
2. **Preserve the user's working tree.** Start with `git status --short` and `git diff`. Never reset, clean, checkout over, or discard existing changes. Do not assume the public GitHub branch contains the user's latest local work.
3. **Prove engine behavior before hooking it.** A plausible function name or a successful compile is not evidence that a hook has the intended semantics. Trace callers, callees, and the surrounding frame/update/render flow.
4. **Make the smallest testable change.** Keep game-independent logic in pure helpers. Do not combine timing changes, render changes, hook changes, and unrelated cleanup in one unreviewable edit.
5. **Use the repository's tools.** Follow `CLAUDE.md`: `tools\re.py` for reverse-engineering dumps, `tools\check.ps1` for build/unit/static checks, and `tools\launch.ps1` for in-game Lua tests. Do not manually search giant decompiler dumps.
6. **Never claim a test passed from partial output.** Record the command, process exit code, explicit pass marker, and relevant log evidence. A suspected tooling race is not permission to call a failed command successful.
7. **No unsafe engine access.** Engine objects are accessed only on the game thread. Validate pointers with the project's `Plausible()`/`PlausiblePtr()` guard before dereferencing. Do not retain raw engine pointers across frames.
8. **Keep vanilla behavior intact when a feature is disabled.** Every new feature needs an INI toggle. With the toggle off, the feature must install no hooks or have no observable effect, consistent with the existing module design.
9. **Follow repository policy.** Work directly on `main`; do not create branches. Respect the GPL-2.0-or-later rule and preserve GemRB copyright notices when porting GemRB code. Do not copy code from unlicensed EEex/InfinityLoader material.
10. **Do not stop at “it builds.”** A runtime hook needs unit/static checks, targeted in-game regression tests, and a real gameplay/visual check appropriate to the feature.

## 2. Start-of-task checklist

Run these first, before touching files:

```powershell
Set-Location D:\pst-ee-forever
git status --short
git diff --stat
git diff --check
git log -1 --oneline
```

Then inspect:

- `CLAUDE.md`
- `docs/ARCHITECTURE.md`
- the relevant feature plan or module documentation
- the exact files and tests involved
- the current INI/config defaults and hook registration path

If the working tree already contains changes, identify which are user changes and which belong to the current task. Do not overwrite or reformat unrelated files. Before editing, write down the current behavior, desired behavior, constraints, and a small acceptance checklist.

### Tool discovery rules

- Use `python -P tools/re.py find|code|xref|str|grep|asm|data|addr` as appropriate. The `-P` is intentional because the script name shadows Python's standard-library `re` module.
- For function logic, start with the named Linux 3.2.2 pseudocode, then confirm the Windows 3.2.2 hook target, calling convention, bytes, and offsets. The Windows executable is the actual hook target.
- Use `python -I tools/vtable.py ...` for vtable questions and `python -I tools/exe_bytes.py ...` for exact on-disk bytes.
- Do not treat `resources/re/win/dump/names_win.tsv` matches as proof. Confirm a candidate function using independent evidence (strings, constants, callers/callees, vtable slot, or matching logic).
- Never use old 3.1.4 addresses/offsets as 3.2.2 values. GemRB-era notes may explain behavior, but their addresses are stale.

## 3. Before designing a hook

Answer these questions in notes before coding:

1. What exact engine function is being hooked, and what does it do on every call?
2. Is it an update/tick function, a render function, a message-pump function, a combined update-and-render function, or merely a callback inside one of those?
3. What calls it? What does it call? Which work must still happen on every outer-loop iteration (input, window messages, audio, UI, rendering, buffer swap)?
4. Which thread invokes it? Can it run during startup, loading, menus, pause, shutdown, or area transitions?
5. What state does the hook read or mutate? Which objects can be sprites, projectiles, effects, or non-creature renderables?
6. What exact bytes, RVA, signature, calling convention, prologue length, and target-build fingerprint protect the hook?
7. What is the safe fallback if the symbol check, pointer check, or runtime guard fails?

## 4. Implementation loop

Use this order for each small change:

1. **State the invariant.** Example: “World simulation advances at the engine's native 40 Hz; rendering may sample an interpolated pose between simulation updates; scripts, combat, timers, saves, and input semantics remain unchanged.”
2. **Identify the minimum engine evidence.** Record the Linux function name/lines, Windows function/address/bytes, call graph, relevant struct offsets, and why the hook point is safe.
3. **Write pure logic first.** Put arithmetic/state transitions in a helper with no engine pointers, Win32 calls, or renderer dependencies.
4. **Write deterministic tests before integration.** Include normal cadence, jitter, delayed frames, startup, clock edge cases, disabled mode, and any accumulator cap/catch-up policy. Hand-calculate expected values; do not write assertions based on an untraced intuition.
5. **Integrate one hook at a time.** Use typed function pointers and the symbol manifest. Keep the original call's required work intact. Add counters/logging only where they answer a specific question.
6. **Inspect the exact diff.** Check for accidental edits, wrong pointer indirection, stale declarations, missing includes, uninitialized state, and comments that no longer match behavior.
7. **Run L0 checks.** Build, unit tests, static symbol/patch verification.
8. **Run the smallest relevant in-game test.** Then run every regression test whose behavior could be affected.
9. **Run a manual gameplay check.** Check the actual visual/behavioral outcome, not only test counters.
10. **Report evidence and remaining uncertainty.** Do not claim completion while a required test or visual check remains outstanding.

Avoid broad string-replacement edits to large C++ files. Prefer a small contextual patch. After each edit, read back the complete affected function and run `git diff --check` plus the relevant test. If an edit fails, inspect the actual file before retrying; do not chain speculative replacements.

## 5. Build, deploy, and test protocol

### L0: build, unit tests, and static hook verification

Run by itself and require an exit code of zero:

```powershell
.\tools\check.ps1
if ($LASTEXITCODE -ne 0) { throw "check.ps1 failed: $LASTEXITCODE" }
```

`check.ps1` already performs the configured x64 build, unit tests, and static verification against the installed `Torment.exe`. Inspect `build\logs\unit.log` and `build\logs\verify.log` when anything fails. Do not deploy a build that failed this gate.

### Deploy only after L0 passes

```powershell
.\tools\deploy.ps1
if ($LASTEXITCODE -ne 0) { throw "deploy.ps1 failed: $LASTEXITCODE" }
```

Use `-ResetIni` only when intentionally resetting the user's installed configuration. Normally preserve the existing INI.

### In-game tests: run one at a time

Use a distinct command for each suite so one failure cannot hide or prevent the next test. Examples:

```powershell
.\tools\launch.ps1 -Lua tests\ingame\smoke.lua -Close -Timeout 120
if ($LASTEXITCODE -ne 0) { throw "smoke test failed: $LASTEXITCODE" }

.\tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_hive.lua `
  -Env @{ PSTF_TEST_TICK = '1' } -Until 'TEST PASS|TEST FAIL' -Close -Timeout 900
if ($LASTEXITCODE -ne 0) { throw "tbc_hive failed: $LASTEXITCODE" }

.\tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_rules.lua `
  -Env @{ PSTF_TEST_TICK = '1' } -Until 'TEST PASS|TEST FAIL' -Close -Timeout 500
if ($LASTEXITCODE -ne 0) { throw "tbc_rules failed: $LASTEXITCODE" }
```

Only run suites relevant to the current change, but run all affected regressions. `tbc_hive.lua` and `tbc_rules.lua` cover different behaviors; passing one does not imply the other passed. Use the timeout appropriate to the test's known duration.

For every run, record:

- exact command and save used;
- `launch:` terminal status (`done`, `failed`, `test failed`, `exited`, or `timeout`);
- process exit code;
- explicit test marker (`TEST PASS`, or the suite's documented success message);
- any `TEST FAIL`, Lua error, crash, or timeout lines;
- relevant runtime log excerpt.

If the process exits or is stopped while `-Close` is active, inspect the complete log and process state. Do not dismiss a nonzero exit as a `Stop-Process` race unless the test marker, script status, and exit behavior establish that the test itself completed successfully. Re-run the affected test independently if ambiguous.

### Manual validation

A test script can prove logic assertions, but it cannot fully prove smoothness or visual correctness. Launch the game normally and test the feature in motion. Compare enabled and disabled behavior under the same scene, save, resolution, and refresh rate. Use screenshots or a short recording when helpful. Record what was actually observed.

## 7. Efficient debugging rules

- When a test fails, inspect its exact failed assertion and the helper implementation. Trace the values by hand before editing.
- Change one cause at a time. Re-run the failing unit test before the entire suite.
- If a test's expectation is wrong, correct the test only after deriving the expected value independently; do not weaken a test to make it pass.
- If a runtime test is hard to observe, add a small probe/counter at the exact layer needed (tick, render, or present), document its semantics, and remove or disable noisy instrumentation when finished.
- If a script or harness command fails, distinguish test failure from test-runner failure. Preserve the original logs and report both.
- Do not run deploy/build concurrently with a game test that is using the deployed DLL. Finish the test, close the process, then rebuild/deploy.
- Do not launch several long in-game tests in one chained command. Run them sequentially with exit-code checks and explicit completion records.
- Avoid spending time on speculative cleanup or unrelated refactors while a core behavior is not yet proven.

## 8. Definition of done

A task is complete only when all applicable boxes are true:

- [ ] The change matches the requested behavior and documented invariants.
- [ ] Engine function, call path, thread, ABI, and memory assumptions are supported by evidence.
- [ ] The current diff contains only intentional changes; `git diff --check` is clean.
- [ ] Unit tests cover the pure logic and edge cases.
- [ ] `tools\check.ps1` passes (build + unit + static verification).
- [ ] The deployed DLL is the exact build that passed L0.
- [ ] Relevant in-game regression suites pass with exit code zero and explicit success markers.
- [ ] Manual gameplay/visual validation is complete where applicable.
- [ ] Disabled-feature behavior and existing modules remain correct.
- [ ] Documentation/config/tests are updated, and any temporary probes are removed or documented.
- [ ] The final report distinguishes proven facts, measured results, assumptions, and unresolved risks.

## 9. Final report format

Keep the handoff short and evidence-based:

```text
Implemented:
- ...

Evidence:
- Unit/static: command + result
- In-game: suite + save + exit code + pass marker
- Manual: scene and observed result
- Rates/measurements: what each counter measures, elapsed time, counts

Files changed:
- ...

Not verified / remaining risks:
- ...

Git:
- status / commit (if applicable)
```

Write the handoff file in the local_llm folder.

Never write “all tests pass” when a suite was not run, timed out, returned nonzero, or produced ambiguous output.