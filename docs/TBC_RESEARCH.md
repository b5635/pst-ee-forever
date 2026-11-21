# Turn-based combat: research notes, probes and tests

Companion to `TURN_BASED_COMBAT_EXTENSION_PLAN.md` (design) for the `tbc` module. This file records
**what the 3.2.2 engine actually does**, **how to find out more**, and **how each fact is asserted**.
Target: BG3-style turns (initiative order, one action + movement + one reaction per turn, explicit
End Turn, shared party initiative) on top of PST's own rules.

Status (2026-11-21, later): **turn-based combat runs in the game** (section 8). The notes below
were written first; section 8 describes what is now implemented and tested.

Status (2026-11-21, first pass):
- Done: pure core (`src/modules/tbc/`) with 12 unit cases; 12 new manifest entries checked statically;
  a dev Lua bridge (`[dev] lua=1`) verified in the running game.
- Not wired yet: the director does not take over creatures in game. Section 7 lists the hook points
  still to confirm.

---

## 1. The ways in (cheapest first)

| Layer | Tool | What it can tell you | Asserted by |
|---|---|---|---|
| L0 pure | `tools\check.ps1` -> `pstf_tests` | turn flow, budgets, APR, initiative, path clip, time maths | `tests/unit/test_tbc.cpp` |
| L0 static | `tools\check.ps1` -> `pstf_verify` | engine code bytes, RIP-relative targets (globals), static `.data` values | `src/engine/symbols.h`, `tools/pstf_verify.cpp` |
| RE lookup | `python -P tools/re.py ...`, `python -I tools/exe_bytes.py VA N [--rel32|--u32]` | logic (Linux), addresses/bytes (Windows) | becomes a manifest entry + test |
| **L1 scripted** | `tools\launch.ps1 -Lua tests\ingame\X.lua -Close` | loads a save with no input, runs X.lua in the engine state when the world is up; exit code = result | log lines; `error()` in X.lua fails the run |
| L1 startup Lua | `luaAuto.cfg` in the game folder (vanilla mechanism, §2.3) | runs our `PSTF_*` probes at boot (too early for options) | `pst_forever\logs\pst_forever.log` |
| L1 console | Ctrl+Space Lua console (§2) | anything during play: `PSTF_*`, `C:Eval(...)`, engine bindings | log lines `lua: NAME -> value` |
| L2 pipe (planned) | `tools\pstf.py` over `\\.\pipe\pst_forever` | same as console, but scriptable without a keyboard | not built yet |

Agent loop for an engine fact: find it with `re.py` (Linux logic, then Windows asm), dump the bytes
with `exe_bytes.py`, add a manifest entry plus a `pstf_verify` cross-check, read it at runtime
through `src/engine/` and expose it as a `PSTF_*` Lua probe. Then assert it in `PSTF_SelfTest`.

---

## 2. The in-game Lua console (vanilla engine feature)

### 2.1 Turning it on
`CScreenWorld::OnKeyDown` (lin 4337F0) opens the `cheatConsole` menu (key code 32, space) only when
`CInfGame` option `Cheats` **or** `Debug Mode` is set (CInfGame +0xB94C / +0xB960 on Linux:
`*((_DWORD*)v3 + 11859 / 11864)`). Both are read from
`Documents\Planescape Torment - Enhanced Edition\Baldur.lua`:

```lua
SetPrivateProfileString('Game Options','Cheats','1')         -- CInfGame::LoadOptions
SetPrivateProfileString('Program Options','Debug Mode','1')  -- CBaldurChitin::LoadOptions (also sets a debug flag on the world screen)
SetPrivateProfileString('Program Options','Logging On','1')  -- CChitin::m_bUseLogs (engine log)
```

On this machine `Baldur.lua` has neither (checked 2026-11-21); not changed without the owner's OK.
The usual EE binding is **Ctrl+Space**; confirm the modifier on first use.
The console text box is `luaEditArea`; input goes to `Infinity_LuaConsoleInput`.

### 2.2 What the console can do
- **Raw Lua 5.2** in the engine's single global state (the same state the `.menu` UI uses).
  The standard libs are `_G, table, string, bit32, math, debug` (no `io`, `os`, `package`; see IESDP
  `file_formats/ie_formats/lua_globals.htm`).
- **`C:Eval('<BCS action>')`** (`CLUAConsole::Eval(CString,int)`, lin 27BFE0): parses the text with
  `CAIScriptFile::ParseResponseString` and queues the actions on a creature through its
  `InsertAction` vtable slot. Parse errors show as "Action Errors:" overhead text. This is the
  cheapest way to **stage combat**: `C:Eval('Attack(NearestEnemyOf(Myself))')`,
  `C:Eval('ChangeEnemyAlly(Myself,ENEMY)')`, `C:Eval('CreateCreature("...",[x.y],0)')`.
- Other `C:` methods (Linux names): `CreateCreature, CreateItem, AddGold, SetGlobal, GetGlobal,
  MoveToArea, ExploreArea, AdvanceRealTime(ticks), SetHour, EnableCheatKeys, PlaySound,
  DisplayText, WriteScript, TestAllDialog, StrrefOn/Off, LogMessages`. Cheats: `CLUACheats::*`
  (`CowKill, DrizztAttacks, DrizztDefends, Hans, Midas, FirstAid, CriticalItems`).
- Engine objects from the tolua chunk: `game`, `timer = game:GetWorldTimer()`, `worldScreen`, ...
  (IESDP lua_globals; BG:EE-derived, confirm per method on PST:EE).
- Output: `Infinity_DisplayString(tostring(x))` (overhead/log text) and `Infinity_Log(...)` both exist
  in PST:EE's `uiLoadFunctions`. Our probes also log every result, so the console never has to be read.

### 2.3 `luaAuto.cfg` / `luaAutoS.cfg`
`CLUAConsole::LuaInit` (lin 27AAB0, win 140226220) runs `luaL_loadfilex("luaAuto.cfg")` + `pcall`
right after the engine bindings and `C` exist; `LuaUninit` runs `luaAutoS.cfg` at shutdown. Both are
read from the working directory (the game folder). A missing file is silently ignored.
- **Order fact (measured in game 2026-11-21):** `luaAuto.cfg` runs **before**
  `CInfGame::LoadOptions`. `ROUND_LENGTH` is still its static 100 at that point, so round-length
  checks must run later (console or a later hook). The game object already exists.
- Test use: drop a `luaAuto.cfg` that calls `PSTF_*` probes, launch, read our log, delete the file.
  That is how the 2026-11-21 check below ran.

### 2.4 Loading a save with no input (`tools\launch.ps1`)
- `CScreenStart::OnContinueGame` (lin 40F4C0, win 1403459A0) loads the save Baldur.lua names in
  `'Game Options','Last Save PST'` (`GetContinueSaveGameName`: `"Last Save %s"` + campaign "PST").
- `launch.ps1` writes that key, starts the game with `PSTF_AUTOLOAD=1`; the `dev` module ticks every frame
  (redirected `call CChitin::SynchronousUpdate` in `CBaldurChitin::SynchronousUpdate`, win 140144254), waits
  30 frames on the start screen, calls `OnContinueGame`, waits 60 frames on the world screen, then runs
  `PSTF_RUN_LUA` through `luaL_loadfilex` + `lua_pcallk` on `g_lua`. Logic: `autoload_logic.h` (unit-tested).
- Active screen = `chitin+0x260` compared with start `+0x1138`, world `+0x1148`, load `+0x1110`.
- Measured 2026-11-21: start screen at frame 1 (movies off), Continue at frame 30, world at frame 31,
  script at frame 90, about 3 s after launch. Engine switches `+loadgame`/`+newgame` exist in
  `CChitin::ParseCommandLine` but only set flags (multiplayer path); not used.
- Movies: `'Program Options','Disable Movies'` makes `CBaldurProjector::PlayMovie` return at once (all movies).

### 2.5 Our probes (`[dev] lua=1`, `src/modules/dev/lua_bridge.cpp`)
Registered at the `luaAuto.cfg` call site (win 14022627A, redirected through a near trampoline).
They only read engine state, on the game thread.

| Function | Returns | Verified |
|---|---|---|
| `PSTF_Version()` | build string | in game |
| `PSTF_EngineMode()` | `CChitin::ENGINE_MODE` (3 = PST:EE) | in game: 3 |
| `PSTF_RoundLength()` | `CGameAnimationType::ROUND_LENGTH` ticks | in game: 100 at luaAuto time, **51 with a save loaded** |
| `PSTF_Screen()` | active screen: start/load/world/other/none | in game: world |
| `PSTF_WorldTime()` | `ticks, running` (`CInfGame::m_worldTime`) | static only |
| `PSTF_RoundsForSeconds(s)` | effect rounds at the live round length | in game: 15 s -> 3 at 100 ticks |
| `PSTF_TbcSim(seed)` | director fight summary at the live round length | in game: `winner=party rounds=3 swings=10 clock=200` |
| `PSTF_SelfTest()` | `"ok"` or a list of failed engine-fact checks | in game: `ok` with a save loaded |

Console recipe (once Cheats is on): `Infinity_DisplayString(PSTF_SelfTest())`, then
`Infinity_DisplayString(PSTF_RoundLength())` -> expect `ok` / `51`.
Measure ticks per real second: call `PSTF_WorldTime()` twice about 10 s apart and compare the log
lines (expected 15 per second at normal speed, unpaused).

---

## 3. Engine facts for turn-based combat (3.2.2)

`lin` = Linux EA (logic), `win` = Windows EA (hook/bytes). "Static" = asserted by `pstf_verify`.

### 3.1 Time
| Fact | Evidence | Asserted |
|---|---|---|
| 15 game ticks per game second | `CGameAIBase::Wait` (lin 60E9B0) `*15`; effect `AddEffect` paths `/0xF`; IESDP "7 AI updates = 7/15 s" | unit (`kTicksPerSecond`) |
| Day = 108000 ticks (24 h x 300 s x 15) | `CTimerWorld::UpdateTime` (lin 345D00, win 1402B3490) `% 0x1A5E0` | unit |
| **PST round = 51 ticks (3.4 s)**, other EE = 100 | `CInfGame::LoadOptions` (lin 3679E0 L326-331; win 1402E775C/776A/777D): `"Round Length"` default `ENGINE_MODE==3 ? 51 : 100` | static (bytes + RIP targets + `.data`=100) |
| `ROUND_LENGTH` global | win 14071F5FC (Linux E6A174); user-overridable in Baldur.lua | static + `PSTF_RoundLength` |
| `ENGINE_MODE` | win 1407CB718 (bss), set by `initEngineSettings` from `engine.lua` `engine_mode=3` | static + in game (3) |
| World clock | `[[g_pBaldurChitin 14072C278] + 0x10C8] + 0x7820` = `CTimerWorld` {+0 u32 ticks, +4 u8 running} | static (28-byte chain at 14035432E) |
| Real-time render rate | `CChitin::TIMER_UPDATES_PER_SECOND` = "Maximum Frame Rate" (10..90, default 30; this machine 40) | not yet |

> **Plan correction.** `TURN_BASED_COMBAT_EXTENSION_PLAN.md` says "a combat round is 5 seconds".
> The engine's PST:EE round is 51 ticks = 3.4 game seconds. A 15 s debuff is 225 ticks, which is
> `RoundsForSeconds(15, 51)` = 5 rounds (4.41 rounded up). The plan's "3 rounds" only holds for a
> 5 s round. Decide with the owner which one the turn clock uses; the code takes the engine value.

> **Layout warning.** The Windows `CBaldurChitin` has the game pointer at +0x10C8; Linux has it at
> +0x1118 (`g_pBaldurChitin[547]`). Linux offsets in this class are **not** valid on Windows.
> `CInfGame` offsets checked so far agree (+0x7820 timer, +0xA178/+0xA180 area list).

### 3.2 Attacks, initiative, rounds
- APR is stored as a short: 0..5 attacks, 6..10 = 0.5..4.5 (`v - 5.5`), >10 = 0
  (`CDerivedStats::NumAttacksShortToFloat`, lin 17EA10). Unit-tested in `AprHalvesFromStat`.
- The engine already has **per-swing initiative** inside the attack round: `CGameSprite::Swing`
  (lin 4EAC10 L377-403) sets a delay = weapon speed (ability byte 18) minus a stat, plus
  `rand(INITIATIVE_RANGE) - INITIATIVE_RANGE/2`, clamped 0..10, stored at sprite +0x484C (`this+9254*2`).
  The director must let this play out (or zero it) inside our turn. **Find out:**
  `re.py data "CGameSprite::INITIATIVE_RANGE"`, then `re.py code CGameSprite::Swing --lines 370-410 --win`.
- `Swing` raises `AttackedBy`/`HitBy` triggers itself (L230-231): a turn that drives the engine's
  own `Attack` action keeps the script-visible side effects for free. Prefer issuing real actions
  (`C:Eval`-style `InsertAction`) and gating *when* they run over re-implementing swings.
- Callers of `Swing`: `Attack`, `AttackReevaluate`, `RunAwayFrom`, `ProtectPoint`, `ProtectObject`.

### 3.3 "Are we in combat?" (combat start/end signals)
- Area battle music: `CGameArea::AIUpdate` (lin 1B8840) decrements a counter at area +0xFC4
  (`this+1009*4`, Linux) and drops song 3 (battle) when it reaches 0. `CGameSprite::Attack` /
  `AttackReevaluate` and `CMessageStartCombatMusic::Run` call `PlaySong(3)`. Candidate start/end
  signal that matches what the player hears. **Confirm the Windows offset** in `re.py code CGameArea::AIUpdate --win`.
- `CInfGame::CanRestParty` / `CGameArea::PSTCanRestParty` scan for nearby hostiles ("cannot rest").
  That is the engine's own definition of "enemies near".
- Hostility: `CAIObjectType::IsEnemyOf` (lin 5E4CC0, win 14012B7A0), `GetEnemyOf`. EA lives in the
  sprite's `CAIObjectType` at sprite +0x30 (`this+48`).

### 3.4 Taking a creature over
- Per-sprite update: `CGameSprite::AIUpdate` (lin 53A510, win 1403B4330, vtable): effects
  (`ProcessEffectList`), walking (`AIUpdateWalk`), animation, bounce, markers.
  Script and action processing: `CGameSprite::ProcessAI` (lin 4FA830, win 140421830, vtable).
  **Plan:** gate `ProcessAI` and `AIUpdateWalk` for director-owned ids only; keep animation running.
  **Find out:** the vtable slot of each on Windows (`re.py xref CGameSprite::ProcessAI --win`; it has no
  direct callers: it is called through the vtable from `CGameArea::AIUpdate`).
- `CGameSprite::SetActiveAI(int)` (lin 522DE0) is the engine's own deactivation: it frees the path and
  marks the pending `CSearchRequest` stale. It is too heavy for takeover (it also leaves the search
  map) but shows what a clean stop has to drop.
- **Pathfinding runs on its own thread** (`SearchThreadMain`, lin 341660, win 1402AEAB0). Requests are
  `CSearchRequest` with states WAITING/PROCESSING/STALE. A path preview is therefore asynchronous:
  queue a request on the game thread, wait for the result event, then `ClipPath`. Never touch the
  request from our code off the game thread. (This is the PST:EE form of the fork's "stale cache" lesson.)
- Pause: `CScreenWorld::TogglePauseGame` (lin 42F250, win 1403678A0), `CTimerWorld::StopTime/StartTime`.
  Pausing stops the whole world, so a "pause between turns" design breaks hard requirement 2. Keep it
  only as a fallback prototype.

### 3.5 Names the port got wrong (fix in `manual_names.tsv` when confirmed)
- `lua_createtable` -> win 1400E49E0 is really **`lua_setfield`** (strlen's a key at r8).
- `lua_setfield` -> win 1400E47D0 is something else (takes two ints and runs a GC step). Unknown.
- `isoc23_sscanf` = win 1400E3D00 is **`lua_type`** (index2addr, nil object -> -1, `type & 0xF`); the ported `lua_type` twin 140459E00 is wrong.
- `luaL_loadfilex` = win 1400E6CA0 (unported; proved by the `luaAuto.cfg` call site).
- The Windows `CInfGame::LoadOptions` pseudocode is cut short by `__noreturn`: read `re.py asm ... --all`.

---

## 4. How the BG3 turn maps onto PST (implemented in the director)

| BG3 | Here | Code |
|---|---|---|
| Initiative once per fight | `d10 - dex reaction bonus`, lower first; ties: party, roll, id | `Initiative`, `InitBefore` |
| Shared initiative for adjacent party members | consecutive party slots form a group; switch freely, budgets kept | `Director::SwitchTo` |
| Action | one action: attack round **or** cast **or** use item/swap | `BeginSwing`, `BeginAction` |
| Extra Attack | native APR: 3/2 = 2 swings, then 1, alternating per own round | `AttacksThisRound` |
| Movement | pixel budget; path clipped once and reused for preview and walk | `ClipPath`, `BeginMove` |
| Reaction | one opportunity attack, refreshed at its own turn start | `Reaction`, `LeavesReach` |
| End Turn | explicit, or automatic when nothing is left; never while moving/swinging/casting | `EndTurn`, `MaybeAutoEnd` |
| Late joiners | wait until next round if their slot already passed | `Director::Join` |
| Bonus action | **PST has none.** Open decision: none (default), or quick-slot items | - |

---

## 5. Tests

### 5.1 Exist (run by `tools\check.ps1`)
- `tests/unit/test_tbc.cpp`: time maths, APR encoding, initiative order/ties, RNG determinism, path
  clipping, opportunity check, director turn flow, 3/2 APR over two rounds, group switching, groups
  split by a hostile, reactions, late joiners, removal/death/end, deterministic sim.
- `pstf_verify`: Lua API bytes; the `luaAuto.cfg` call goes to `luaL_loadfilex`; LoadOptions
  round-length bytes plus RIP targets (ENGINE_MODE, ROUND_LENGTH); static ROUND_LENGTH = 100;
  world-clock chain: chitin global, +0x10C8, +0x7820, call to `UpdateTime`.

### 5.2 To build next (cheapest layer that can catch it)
1. ~~Console run of `PSTF_SelfTest`~~ done via `launch.ps1 -Lua tests/ingame/smoke.lua`: `ok`, round length 51.
2. **Real ticks per second**: two `PSTF_WorldTime()` samples; assert ~15/s unpaused and 0 paused.
3. **Effect duration census** (static/RE): every `ROUND_LENGTH` user (39 refs:
   `re.py data CGameAnimationType::ROUND_LENGTH`) and every `m_worldTime` reader on sprites, so the
   combat clock knows what to rebase on handback.
4. **Trigger parity** (in game, plan §2.4): stage one attack, one cast and one kill with `C:Eval`,
   in real time and under the director, and compare `LastAttackerOf`, `AttackedBy`, `HitBy`, `Die`
   and the kill variables. Needs a variable dump probe (`PSTF_Global(name)`).
5. **Takeover probe**: `PSTF_Own(id)` / `PSTF_Release(id)` gating `ProcessAI`/`AIUpdateWalk` for one
   creature. Assert it stops (no position change across N ticks) while a townsperson keeps walking.
6. **Pipe + `tools\pstf.py`** for live queries during a session (scripted runs already work through `launch.ps1`).

---

## 6. Open questions and how to answer each

| Question | How to find out |
|---|---|
| Which vtable slots hold `ProcessAI` / `AIUpdate` on Windows? | `re.py asm` on `CGameArea::AIUpdate --win`, look for `call qword ptr [rax+N]` on sprites; cross-check with `vtscan.py` output |
| Windows offset of the area battle counter (Linux +0xFC4) | `re.py code CGameArea::AIUpdate --win --lines ...` near `PlaySong(..,256,4,1)` |
| Sprite action queue / current action offsets | `re.py code CGameAIBase::SetCurrAction --win`, `ClearActions`, `InsertAction` (vtable +280 on Linux, per `CLUAConsole::Eval`) |
| Does the console need Ctrl? | try in game with Cheats=1; or `re.py code CScreenWorld::OnKeyDown --lines 3180-3300` |
| How does `C:Eval` pick the creature (`a3` slot vs selection)? | `re.py code CLUAConsole::Eval --lines 60-140` |
| Weapon-speed delay inside the director turn | `re.py code CGameSprite::Swing --lines 370-410`; observe a 2-APR swing in game |
| Exact real-time rate of AI ticks vs "Maximum Frame Rate" | `PSTF_WorldTime()` samples at 30 and 40 fps |

---

## 7. Next implementation steps
1. Confirm `ProcessAI` and `AIUpdateWalk` hook points (section 6), add them to the manifest, and add a
   `tbc` module with `[tbc] enabled=0`. Install no hooks when it is off.
2. Ownership set (actor ids, never pointers) -> gate both functions for owned ids only. Probes:
   `PSTF_Own/Release`.
3. Combat start: area battle counter or the first hostile `Attack` on a party member -> `Director::Start`.
4. Drive turns by issuing the engine's own actions (`InsertAction`, as `C:Eval` does) and completing
   them on action-done events. Feed `MoveFinished`/`SwingResolved`/`ActionResolved`.
5. Combat clock + handback rebase (census from 5.2.3).
6. HUD: Lua `.menu` reading `PSTF_*` snapshots (turn order, budgets), End Turn button.

---

## 8. Implemented: turn-based combat in the game (module `tbc`, `[tbc] enabled=1`)

### 8.1 Model
- **Start**: a hostile (EA >= 200) within `start_radius` (450) of a living party member, measured
  with the engine's own isometric ellipse (`dx^2 + 16*dy^2/9 <= r^2`, from `CInfGame::CanRestParty`).
  Party members, allies (EA <= 30) and hostiles within `join_radius` (650) of the party join.
  Initiative is rolled once per fight (d10, lower first, party wins ties).
- **Hold**: only the creature whose turn it is runs `CGameSprite::ProcessAI` (scripts + actions) and
  `AIUpdateWalk`. For every other combatant the hooks return at once. Animation, effects and incoming
  triggers keep running, and creatures outside the fight are untouched.
  - Hooks: vtable slot 35 of `CGameSprite` (win 140649440 -> our function) and the two
    `call AIUpdateWalk` sites in `CGameSprite::AIUpdate` (1403B6211, 1403B677F).
- **A turn** is one engine round of the creature's own activity (`turn_clock.h`): `ROUND_LENGTH`
  (51) ticks of *busy* time, meaning it has a current or queued action.
  - The engine does the rules and the timing: attack rounds with native APR and weapon speed,
    casting, walking speed. So movement plus attacks share one round, as in AD&D.
  - Party turns wait for orders (thinking costs nothing) and end on the budget or the **End** key
    (`end_turn_key`).
  - AI turns end when the creature goes idle (after acting, or after 2 s without picking anything),
    or at a hard cap.
- **Turn end**: `CGameSprite::ClearAI(0)` + `DropPath`, so no action, path or queue leaks into other
  turns. Triggers raised while it waits (being hit) arrive afterwards and are read on its next turn.
- **Join/leave** every frame: newcomers near the party join (they wait for the next round if their
  slot passed); the dead, those who leave the area and hostiles beyond `leave_radius` (1100) leave.
  The fight ends when one side is empty, on an area change, or when the world screen goes away.
- **Feedback**: `Infinity_DisplayString` messages ("Combat!", "Round N: X's turn", "Combat over")
  and `tbc:` log lines with HP after every turn.
- **Party AI** (`auto_party=1` or env `PSTF_TBC_AUTOPARTY=1`): party turns queue
  `Attack(NearestEnemyOf(Myself))` through `C:Eval(text, slot)` and end when idle. Note that
  `NearestEnemyOf` only finds enemies the creature can see.

### 8.2 Verified in game (2026-11-21)
`tools\launch.ps1 -Lua tests\ingame\tbc_fight.lua -Env @{PSTF_TBC_AUTOPARTY='1'} -Until 'tbc: fight 1 over' -Close`
-> exit 0. The log shows:
1. fight 1 starts, 3 combatants, order "party member 1, enemy 50005, party member 2";
2. each turn ends on its 51-tick budget, or on idle for the party member with no target in sight;
3. "enemy 50005 leaves the fight (dead, hp 0)" in round 2, then "Combat over".

Screenshots (`PSTF_SHOT_DIR`) showed the active creature walking while the others stood still.
An earlier run without staging started when the hostile walked up on its own, and ran 12+ rounds.

### 8.3 Known gaps (next work)
- Effects and timers run on world time. In a fight with N combatants a round takes about N x 51
  ticks of world time, so durations elapse up to N times faster per round. The combat clock and
  handback rebase (section 5.2.3 census) are not done.
- No opportunity attacks or reaction prompts yet (`Director::Reaction` exists, not wired).
- ~~No on-screen turn order bar or movement preview~~: done in section 9. No hit-chance preview yet.
- Dialogue/cutscenes during a fight are not special-cased. Dialogue pauses world time, so turns
  simply wait, but this is untested.
- Initiative ignores Dexterity (bonus 0) until the DEX stat offset is confirmed on Windows.
- The active party member is not auto-selected; click its portrait.
- Neutral creatures that turn hostile join only once they are within `join_radius`.

---

## 9. Implemented: BG3 action economy + HUD (`[tbc] move_feet=31`, `hud=1`)

### 9.1 Economy (`src/modules/tbc/economy.h`, replaces the 51-busy-tick turn for both sides)
| Budget | Rule | Engine fact |
|---|---|---|
| Movement 31 ft | feet = sqrt((dx/16)^2 + (dy/12)^2) of the per-frame position delta; 0 left -> skip `AIUpdateWalk`, cancel a path that wants more | cell 16x12 px (`GRID_SQUARE_SIZEX/Y` .rdata 14062E588) |
| One action | a non-move action (ACTION.IDS, move set in `IsMoveAction`) running with no walk path | current action u16 sprite +0x3F0, path +0x4470 |
| Attack | one round of ticks from the first non-walking tick, then `ClearAI` | `ROUND_LENGTH` 51 |
| Cast / item | ends when the engine finishes the action | action id back to 0 |
| End | party: action used + 0 ft, or End Turn (key / HUD button / `PSTF_EndTurn`); AI: also idle or cap | |

### 9.2 HUD (`src/modules/tbc/hud.cpp`, `src/engine/menu.cpp`)
- Menus are added in memory to the UI.MENU text (`uiLoadMenu` win 140459E00 copies it with
  `call 1404842C0` at 140459EC3; we return UI.MENU + our text and keep a reference alive).
- Each fight frame the module runs `pstfTbc = {...}` (BuildHudLua) + `pstfTbcLayout()`, which places
  every element with `Infinity_SetArea`. The menu is pushed on fight start and popped on fight end.
- Card picture: animation type (sprite +0x3950, vtable 1406470B0 = PST character) +0x1D0C bestiary
  index -> Lua `bestiary[k].index` -> `imageKnown` (or the `global`/`iglobal` variant, as
  `getBestiaryImage`). Name: strref sprite +0x558 via `Infinity_FetchString`. Max HP: derived stats +4.
- Movement line: path cells from index +0x44F0 to count +0x4478, decoded as `AIUpdateWalk`;
  world -> UI = (p - view + viewport rect) / zoom, as `CGameAreaNotes::UpdateButtonPositions`.

### 9.3 Hover preview, line, turn control (done)
- Line: `CVidMode::DrawLine2d` (win 14049D870) after `CGameArea::Render` inside `RenderZoomed` (zoom-scaled);
  args (vid, x1, y1, x2, y2, CRect* clip = area+0x650, uint 0xBBGGRR (alpha >= 0xAA), half_width, start_trim,
  end_trim, dash (-1 solid), gap, textured); draw space = world - (area+0x698 view - rect left/top).
- Preview: A* (`path_preview.h`) on `CSearchBitmap::GetCost` (area+0xA38; terrain = sprite vtbl+0x148();
  personal space as `SetTarget`), cursor = area+0x25C - rect + view. Not `CPathSearch`: the search thread
  uses snapshots; ours reads the live bitmap on the game thread (read-only).
- Selection lock: sprite +0x3914 selected flag; `UnselectAll`/`SelectCharacter(id,0)`/`SelectToolbar`.
- Party AI off during fights: Lua `game:GetPartyAI()` / `game:ToggleAI()`; party turn start = `ClearAI`.

### 9.4 Open
- Camera follow on turn start (`CInfinity::SetViewPosition`, win 140309630, unconfirmed).

---

## 10. Implemented: round clock, spells in turns, AI orchestration, BG3 HUD (2026-11-21, later)

### 10.1 Effect time in rounds (`engine/combat.cpp`, `round_rules.h`)
| Engine fact | Evidence | Use |
|---|---|---|
| `ProcessEffectList` runs per tick from `ProcessAI` | lin 4FA830 L686/845/972 | held creatures: `HeldEffectsTick` (+0x3A0 tick counter, then the call) |
| Persistent effects only while the timer runs | win 140425C36 `cmp byte [game+7824h],0` | call stub: also "skip" for combatants |
| `HandlePersistantEffects(int ticks)` catch-up | lin 4A46D0 (CompressTime), win 140413BF0 | 75 ticks at the owner's turn start |
| Timed/delayed effects compare world time | ResolveEffect lin 1DFD20: +0x24 type, +0x28 tick | shifted by every frame's dt; -75 at turn start |
| Natural regeneration | ProcessEffectList win 140423487: (now - [+0x4B6C]) / HPCONBON rate; TNO column REGENERATION_RATE_NAMELESS (ticks; CON 20 = 75) | stamp shifted like effects |
| Effects re-evaluated at once | +0x48BC = 1 (AIUpdate 1403B43D1) | after the step |
Rule: a delayed effect seen with `left` ticks: `OnsetResolvesNow(left)` (not on a 75-tick step and < 112)
runs on world time inside the caster's turn; otherwise round-clocked. After a step, 1..3 ticks left
become 4 (no expiry during someone else's turn).

### 10.2 Spells and projectiles
- Projectiles are objects of type 0 with the caster at +0x68 (DeliverEffects win 140282F3F); AIUpdate is
  vtable slot 3 for every class. `WatchProjectiles` hooks each class met (slot 1 must be
  `CGameObject::GetObjectType` 140173050); a projectile of a waiting combatant does not move.
- Turn waits (cap 120 ticks) while the caster has projectiles, while resolving onsets are pending, and
  25 ticks after its action ends (the projectile appears up to a second after the cast animation).
- PST:EE `ForceSpell` fails silently below the SPL header's minimum caster level (Ax 5, Cloudkill and
  Cone of Cold 9); `ReallyForceSpellLevel` casts at a given level. Cloudkill/Ax/CoC hit only enemies of
  the caster (PST hardcoded projectiles).

### 10.3 AI orchestration
- Free actions (`IsIdleAction`): 0, 17, 30, 33, 36, 61, 63, 83, 84, 86, 130, 151, 262, 269, 292, 311, 342, 346, 368.
- Nudge after 15 ticks with nothing chosen: `ClearAI`, `EvalAs(id, "Attack(PlayerN)")` (party slot 0 holds
  the creature for that synchronous `C:Eval`), SetInterrupt(FALSE) set directly (sprite +0x560 bit 31,
  ExecuteAction case 86 win 14040CDA9; ProcessAI clears it when the queue is empty). Gives up after 20
  idle ticks.

### 10.4 HUD
- Hotbar: `const.ACTIONBAR_STATE_{WEAPONS,SPELLS,ITEMS,SPECIAL_ABILITES}` (115/116/117/...), slot =
  CInfGame+0x79C0 + 0x2A0 i, icon +0x128, disabled +0x29C; click = SetState + OnLButtonPressed(slot).
- Hit chance: needs = THAC0 - AC (derived stats +0x10 / +0x06, Hit win 140414888), 5..95 %.
- Dice: attack = FeedBack(this, 4, total, hit, roll) at 140415759; target from the three Swing -> Hit calls;
  saves = CheckSave inline hook (flags effect +0x40, bonus +0x44; rolls in the per-type bytes).
