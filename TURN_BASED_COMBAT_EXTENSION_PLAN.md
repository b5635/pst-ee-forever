# Planescape: Torment: Enhanced Edition — Turn-Based Combat Extension

**Implementation handoff for Claude Code** (rev. 2, 2026-11-20: updated with verified PST:EE facts; rev. 1 was written without access to the binary)  
**Reference implementation:** the GemRB fork at `D:\gemrb` ([`b5635/gemrb-pst-forever`](https://github.com/b5635/gemrb-pst-forever)): `TURN_BASED_PLAN.md` §1 (why freezing failed) and §10 (what works)  
**Target:** GOG PST:EE 3.2.2.0, `Torment.exe` x64, SHA-256 `99ae1586…a3c5` (see `docs/ARCHITECTURE.md` §1)  
**Shape:** the `tbc` module of `pst_forever.dll` (not a separate DLL). Loader, version guard, hooks, Lua bridge, logging and the test layers are shared core, described in `docs/ARCHITECTURE.md`.  
**Goal:** deliver the fork's turn-based combat experience as an optional native extension/mod that runs with PST:EE, without replacing the game engine or requiring players to run GemRB.

---

## 0. Mission and constraints

Build an in-process native extension (DLL plus a small loader/bootstrap if required) that adds a genuine turn-based combat mode to PST:EE. Preserve PST's AD&D 2e rules, scripts, dialogue, effects, items, spells, and existing game data. The extension should coordinate the existing engine rather than recreate the combat rules.

The extension is not a GemRB port at the engine-source level. GemRB is the behavioral and architectural reference. PST:EE remains the runtime and source of truth for all engine behavior.

### Hard requirements

1. **Keep the original game rules.** Use PST:EE's existing attack, THAC0/AC, damage, critical, save, resistance, spell, item, death, XP, morale, and trigger code wherever possible. Do not write a parallel rules engine.
2. **Take over combatants instead of freezing the whole world.** During a fight, only the director-owned creatures should stop using the real-time action/script/movement scheduler. Townsfolk, area scripts, ambient animations, music, timers, and unrelated NPCs must continue running.
3. **Use an explicit event-driven turn state machine.** A turn ends when its movement/action/animation/projectile work is complete, not because an arbitrary idle timer expired.
4. **Preserve observable script behavior.** Triggers, object selectors, variables, hostility changes, summons, help calls, barks, fleeing, and dialogue transitions must continue to behave as PST expects.
5. **Support safe handback.** On combat end, death, area transition, dialogue, or cutscene, the extension must return actors to the engine in a clean state.
6. **Fail closed on unknown executables.** Never install hooks against a binary whose identity and hook signatures have not been validated.
7. **Do not depend on EEex.** Its master branch does not support PST:EE; its experimental `PSTEE` branch targets 3.1.4 x86 only and does not run on 3.2.2. Bubb's InfinityLoader is a design reference; it ships no license, so copy no code.

### Product shape (decided after inspecting the exe)

- The `tbc` module inside `pst_forever.dll`, loaded by the `version.dll` proxy (`Torment.exe` imports
  VERSION.dll; not a KnownDLL). In-memory patching only; `Torment.exe` is never modified on disk.
- Hooks: MinHook inline hooks resolved from the symbol manifest (`Torment-3.2.2.syms`: RVA + expected bytes).
  The exe has no ASLR, so RVAs are stable for this build; expected bytes still guard every patch.
- UI: the engine's own Lua 5.2 `.menu` system, through the `PSTF` Lua table the core registers.
- Config in `pst_forever.ini` `[tbc]`; WeiDU only for data files (sounds, BAMs, `.menu` additions) if needed.

---

## 1. Research findings from the GemRB fork

### 1.1 What the fork actually is

The repository describes itself as a heavily modified, Windows-focused GemRB fork for **classic PST**, not PST:EE. Its README explicitly says that the game engine/data being run is classic PST, that EE art/layouts are read from a separate EE install, and that running on EE data alone is not supported. It is therefore a reference implementation, not a drop-in DLL source tree.

The most important reference files are:

- `TURN_BASED_PLAN.md` — architecture, the abandoned first approach, combat rules, AI design, work packages, progress, known gaps.
- `gemrb/core/TurnBasedCombat/CombatDirector.cpp` and `.h` — actual director and combatant state.
- `gemrb/core/TurnBasedCombat/CombatAnalyzer.cpp` and `.h` — BCS action/trigger classification and script profiling.
- `gemrb/core/TurnBasedCombat/CombatHud.cpp` and `.h` — event-fed combat HUD/dice panel.
- `gemrb/core/Scriptable/Actor.cpp` — existing rule execution, attack resolution, animation, actor/effect behavior.
- `gemrb/core/Map.cpp` — actor scheduling, world update, movement/effects integration.
- `gemrb/core/Game.cpp` — game update loop and director tick placement.
- `CLAUDE.md` — local build/test commands and the PST:EE reverse-engineering/parity workflow.
- `PARITY_PLAN.md`, `dev/parity/HANDOFF.md`, `dev/parity/findings.md`, and `tools/parity/` — existing PST:EE-first reverse-engineering evidence and tooling. Use the actual paths present in the checkout; do not assume every generated handoff file is committed.

### 1.2 The critical architectural lesson: the first approach was the wrong one

`TURN_BASED_PLAN.md`, section 1, explains why the earlier `TurnManager` approach glitched. It froze all actors except the active creature and then tried to make GemRB's real-time action queue, scripts, pathfinding, movement, and attack timers behave like a turn system. That produced:

- actors left in walk stance with a path while frozen, appearing to run in place;
- scripts/actions repeatedly re-queuing movement after a movement budget was spent;
- bump-away paths being assigned to frozen creatures that could never walk them;
- attack swings paced by a fixed tick constant rather than the attack animation;
- turn completion guessed from idle/stuck counters and timeouts;
- AI actions looping or consuming a turn in real-time waits;
- guards failing to join because frozen actors no longer ran scripts and saw `Help`/combat triggers.

**Do not port that model.** The rev. 3 design replaces it with a Combat Director that owns participating creatures completely, and gives them back when the fight ends.

### 1.3 The working model in rev. 3

The fork's core model is:

`real-time engine -> combat starts -> director owns combatants -> director runs turns -> combat ends -> engine receives clean actors`

The director owns presentation and flow:

- initiative and turn order;
- per-creature movement/action/attack budgets;
- active actor selection and turn advancement;
- movement requests, clipped paths, and step-by-step walking;
- when attack/cast/use sequences are started and when they are considered complete;
- reactions and combat-specific sequencing;
- AI turn orchestration;
- combat events for the HUD.

The underlying game engine should continue to own:

- attack rolls and defense/THAC0 calculations;
- weapon headers, APR, damage, criticals, and resistances;
- saving throws and spell/effect behavior;
- item/spell charges and inventory side effects;
- death processing, XP, morale, and game variables;
- BCS trigger evaluation, selectors, response weights, and action semantics;
- animation playback, sounds, projectile creation, and other native presentation where possible.

The rule from the fork's plan is the right porting rule: be flexible about presentation, but exact about anything a script, dialogue, item, or spell can observe.

### 1.4 Specific mechanics and behavior to preserve

The current fork implements or documents the following behavior. Verify each item against the current source before porting; the plan's progress section is dated and some AI/UI work remains partial.

**Combat start and participation**

- Combat is started when the party becomes involved in a hostile encounter; the fork checks party/hostile state rather than treating every nearby neutral as an enemy.
- Living party members on the map become participants.
- Eligible hostiles are selected based on hostility, active schedule, targeting/visibility, and later join events.
- Creatures outside the encounter remain on the normal real-time update path.
- Non-combatants that attack a combatant, target one, or otherwise become involved are added mid-fight.
- Neutrals are not combatants merely because they are nearby. An NPC should first become genuinely hostile through the normal game action/state path.
- A newcomer inserted after its initiative slot has already passed waits until the next round.

**Initiative**

The current implementation rolls initiative once per combat. Lower values act first:

`initiative = d10 - DexterityReactionBonus + weaponSpeed - 2 if hasted + 2 if slowed`

Weapon speed is optional via a setting. Ties are resolved by higher Dexterity, then party member before non-party actor, then stable actor ID. Do not substitute D&D 5e initiative or reroll every round.

**Turn budget and attacks**

- Each creature receives its movement allowance and action/attack budget at the start of its turn/round.
- Preserve PST's original APR calculation and attack-round allocation. A creature with multiple attacks executes its native attack round swing by swing; do not convert it to a flat one-attack-per-turn system.
- The current fork uses PST/GemRB's `InitRound` behavior for APR and exposes the upcoming swing count in the HUD.
- Party members sharing the active party group can be switched between during the group's turn; ending the last party member's slot passes control to the next initiative slot.

**Movement**

- A movement preview and the actual confirmed walk must use the same path.
- Compute a path using the engine's pathfinder, clip it to remaining movement, then walk it step by step.
- A turn must not end with the actor still walking or with a stale path queued.
- Director-owned creatures must not receive ordinary bump-away/bump-back movement paths. Treat them as fixed obstacles to bystanders; do not let the normal collision system push a combatant out of position.
- If the player cancels a walk, retain the movement already spent and refund only the unwalked portion.
- The fork's path implementation had to refresh/read the traversability cache before its synchronous path requests; otherwise stale cache data caused false no-path results and self-blocking. Find the PST:EE equivalent before implementing custom path requests.

**Attack animation and resolution**

- A swing is a sequence, not a fixed delay.
- Play the relevant attack animation once, resolve a melee hit at the appropriate attack/hit frame, and wait for the animation to finish before the next swing.
- Ranged attacks must preserve the engine's projectile launch timing and wait for the projectile to reach its first impact/explosion before the turn can safely finish.
- The fork split the rules half of `PerformAttack` into `Actor::ResolveSwing()` and used the director to decide when that resolution happened. In PST:EE, find the native attack/action code and its underlying resolution routine; use that routine rather than copying GemRB's function.
- Preserve `LastTarget`, `AttackedBy`, `HitBy`, `LastAttacker`, attack sounds, hit/death sounds, hostility changes, combat music, death triggers, kill variables, XP, and morale side effects.

**Spells, items, projectiles, and effects**

- Casting is an action inside the caster's turn. The director may hold the turn until the cast animation and the spell's first projectile impact/explosion have resolved.
- Do not advance to the next actor while a projectile/effect that is part of the current cast is still pending, unless the existing spell's semantics explicitly require ongoing world behavior.
- Delayed effects and repeating/area effects need deliberate timing rules. The fork distinguishes delayed onsets, multi-pulse instantaneous areas, and lingering areas that advance on the caster's combat clock.
- Effect durations, poison, regeneration, periodic damage/healing, and spell duration must advance in a way that corresponds to turns, then be rebased correctly to world time when an actor is handed back.
- PST's nominal round duration is read from the game time data. The fork's PST build uses a five-second round (75 game ticks), but the extension must derive/verify the value from the target PST:EE executable/data rather than hard-code 75 without evidence.
- Quick-use items, inventory items, weapon swaps, equipment changes, and spell scrolls need action-cost classifications. Preserve the current implementation's behavior only after confirming it matches the intended rules.

**Reactions and combat extras**

The fork has opportunity attacks, surprise handling, cancel-move behavior, and 2e-style maneuvers such as Run, Withdraw, Block, Trip, and Knockdown. Opportunity attacks interrupt a mover at the edge of an enemy's melee reach, let eligible reactors resolve one reaction for the round, then resume the original movement. Withdraw is intended to avoid opportunity attacks. Delay was later removed from the fork's feature set; do not add it by default.

**AI and scripts**

- PST's BCS scripts determine whether a creature engages, who it targets, when it flees, calls for help, summons, casts, changes behavior, or starts dialogue. The GemRB design adds turn-based movement/tactics around those decisions rather than replacing the scripts with generic combat AI.
- The analyzer classifies actions such as `Attack*`, `Spell*`, `ForceSpell*`, `UseItem*`, `RunAwayFrom*`, movement/escape, waits, dialogue, and instant state/variable actions. It also classifies triggers including `See`, `AttackedBy`, `Help`, `Die`, `HPPercentLT`, `HitBy`, `Heard`, `Allegiance`, and combat counters.
- The fork uses profiles such as guard, brute, coward, fight-then-flee, caster, and caller/summoner.
- The desired turn model is one script decision per AI turn, with the first time-taking action becoming the turn intent and instant actions/variables/triggers retaining their native semantics.
- **Important status caveat:** the current README/progress notes say some AI behavior is still partial. The fork's current progress says scripts run for real once per AI turn; a full dry-run decision/response split and some multi-turn flee/dialogue behavior remain unfinished. Do not claim the whole planned analyzer/AI is complete. Inspect the actual current code and tests.

**UI and feedback**

- The fork exposes a typed combat event feed (`GetCombatEvents`) for combat start/end, round/turn start, swing, damage, death, intent, and reaction events.
- The timeline, turn banner, character/action bar, dice feedback, hover hit-chance breakdown, floating combat text, and world overlays consume that feed.
- Keep UI rendering separate from combat decisions. The native director should publish snapshots/events; Lua/UI should render and send commands back through a narrow bridge.
- `CombatHud.cpp` is a GemRB renderer implementation and is not directly portable to PST:EE. Port the UI behavior and event model, not GemRB's drawing calls.

### 1.5 What is complete versus not complete in the reference

The repository README marks the director/takeover, movement/attack sequencing, combat clock, reactions, event feed, dice panel, hover data, HUD/hotbar and some regression tooling as implemented. It also marks parts of AI, dry-run script decisions, some script-driven encounter cases, some visual overlays, and UX polish as partial/not done.

Claude must treat the latest C++ source and test harness as authoritative, not infer completion from the plan's aspirational sections. The local full git history must also be reviewed as part of Phase 0 (see below).

---

## 2. PST:EE facts (verified 2026-11-20)

- Target: GOG 3.2.2.0 `Torment.exe`, PE32+ x64, no ASLR, SHA-256 `99ae1586…a3c5`. Any other build is a
  separate port (new manifest, new tests); never mix symbols across builds.
- RE material is in place (`docs/ARCHITECTURE.md` §2): the same release's Linux `Torment` with its full symbol
  table, fully decompiled; the Windows IDB with those names ported. Search with `python tools/re.py`.
- PST:EE has its own PST-specific engine code (`CPlanescape::*`, `CPsSpell*` hardcoded spells,
  `CGameSprite::SpellPST`, `CProjectilePST*`) on top of the BG:EE 2.x base. `resources/re/ee_pst_funcs.txt`
  lists those functions.
- PST rounds: check `CTimerWorld`/`CInfGame` constants and the data files for the round length before using 75 ticks.

### 2.1 Engine map: first candidates (from the dumps; confirm each before hooking)

`lin` = Linux EA (read the logic there), `win` = ported Windows EA (hook target).

| Capability | Function (Linux name) | lin | win (method, score) |
|---|---|---|---|
| Main sync tick | `CInfGame::SynchronousUpdate` | 39FE30 | 1402F8C40 (str1 3.9) |
| World screen tick | `CScreenWorld::TimerSynchronousUpdate` | 42D330 | 140367740 (vtable 1.9) |
| World screen async | `CScreenWorld::TimerAsynchronousUpdate` | 43EDF0 | 140367730 (vtable 1.0) |
| Area update | `CGameArea::AIUpdate` | 1B8840 | 14018E0E0 (caller 2.7) |
| Game clock | `CTimerWorld::AdvanceCurrentTime` | 345890 | ? |
| Game clock | `CTimerWorld::AddCurrentTime` | 345AE0 | 1402B2D90 (neighbour 2.6) |
| Game clock | `CTimerWorld::StartTime` | 3460F0 | 1402B3360 (callee 2.4) |
| Game clock | `CTimerWorld::StopTime` | 346150 | 1402B33C0 (strset 5.4) |
| Game freeze (PST) | `CPlanescape::SetGameFreeze` | 5A0BE0 | ? |
| Actor update | `CGameSprite::AIUpdate` | 53A510 | 1403B4330 (strset 4.2) |
| Walking | `CGameSprite::AIUpdateWalk` | 51FFA0 | 1403B6D20 (caller 1.8) |
| Walking | `CGameSprite::SetPath` | 50AC60 | ? |
| Walking | `CGameSprite::DropPath` | 505F80 | 1403C4280 (callee 1.0) |
| Walking | `CGameSprite::DropSearchRequest` | 505FB0 | 1403C42B0 (callee 0.8) |
| Pathfinder | `CPathSearch::FindPath` | 2B8AB0 | 14024DE80 (caller 2.5) |
| Pathfinder | `CPathSearch::GetPath` | 2B9470 | 14024E3C0 (neighbour 6.4) |
| Action engine | `CGameAIBase::ExecuteAction` | 197460 | 14016C8E0 (str1 4.4) |
| Action engine | `CGameAIBase::SetCurrAction` | 183120 | 140175AB0 (vtable 1.6) |
| Action engine | `CGameAIBase::InsertAction` | 19F5D0 | 14028E600 (vtable 0.8) |
| Action engine | `CGameAIBase::ClearActions` | 183060, 613F60 | ?, ? |
| Actor action entry | `CGameSprite::DoAction` | 4A26A0 | 1404079D0 (strset 4.6) |
| Actor action entry | `CGameSprite::SetCurrAction` | 4A54C0 | 14042B610 (vtable 1.6) |
| Scripts/triggers | `CGameAIBase::SetScript` | 182270 | 140175BB0 (vtable 2.0) |
| Scripts/triggers | `CGameSprite::ProcessPendingTriggers` | 4A24B0 | 140425D30 (strset 4.5) |
| Attack | `CGameSprite::Attack` | 4ED490 | ? |
| Attack | `CGameSprite::AttackReevaluate` | 4EE1F0 | ? |
| Attack | `CGameSprite::Swing` | 4EAC10 | 1404334E0 (caller 1.6) |
| Animation | `CGameSprite::SetSequence` | 50A270 | 1403E8800 (callee 1.7) |
| Animation | `CGameSprite::SetFacing` | 50A220 | 1403E7080 (callee 1.0) |
| Projectiles | `CProjectile::DeliverEffects` | 2BAB20 | 140282EC0 (vtable 1.7) |
| Projectiles | `CProjectile::OnArrival` | 2BEDA0 | 14028F3E0 (vtable 1.0) |
| Projectiles (PST) | `CProjectilePSTMissile::Fire` | 2C5FA0 | 140287340 (callee 1.6) |
| Effects/time | `CGameSprite::ProcessEffectList` | 4E21F0 | 1404231E0 (callee 4.0) |
| Effects/time | `CGameEffectList::HandleList` | 1F7750 | ? |
| Party/save | `CInfGame::AddCharacterToParty` | 375A70 | 1402D0A70 (callee 1.5) |
| Party/save | `CInfGame::RemoveCharacterFromParty` | 375DC0 | 1402EB570 (caller 2.1) |
| Party/save | `CInfGame::SaveGame` | 39E400 | 1402EF480 (str1 4.3) |
| Party/save | `CInfGame::LoadGame` | 397170 | 1402E5690 (str1 4.0) |
| Input | `CScreenWorld::OnKeyDown` | 4337F0 | 14035C6B8 (str1 4.5) |
| Input | `CScreenWorld::OnLButtonDown` | 4231C0 | 1403618D0 (vtable 1.6) |
| Input | `CScreenWorld::OnRButtonDown` | 421C80 | 140362150 (vtable 2.2) |
| Dialog | `CScreenWorld::StartDialog` | 43B7F0 | 140365230 (strset 4.7) |
| Dialog | `CScreenWorld::EndDialog` | 4236A0 | 140357830 (strset 5.1) |
| Render | `CGameArea::Render` | 1BB560 | 14019E500 (caller 2.8) |
| Lua bridge | `lua_pushcclosure` | 6A1A10 | 1400E44C0 (diaphora 4.7) |
| Lua bridge | `lua_setglobal` | 6A2240 | 1400E4920 (callee 2.3) |
| Lua bridge | `luaL_loadstring` | 6A58A0 | 1400E7380 (diaphora 4.5) |

Generated from the 3.2.2 index (2026-11-20). `?` = not ported: find it with `re.py xref`/`re.py str` from a ported
neighbour and add it to `manual_names.tsv`. Low scores (< 1.0) and `vtable`/`callee`/`caller` ports are the likeliest
to be wrong: confirm them first.

## 3. Phase 0: feasibility (no feature code yet)

The rev. 1 "inventory the RE material" and "symbol map" steps are done by the dump pipeline. What stays:

### 3.1 GemRB lessons (bounded)

Read `D:\gemrb\TURN_BASED_PLAN.md` §1 and §10 and `git -C D:\gemrb log --stat -- gemrb/core/TurnBasedCombat
TURN_BASED_PLAN.md`. Write `docs/GEMRB_TBC_LESSONS.md` (one page): each hard-won fix (path-cache refresh, bump
suppression, hit-frame resolution, wait-action loops, spell-in-flight hold, combat clock and rebasing,
neutral handling, LastAttacker quirk) with the GemRB commit and the PST:EE function it maps to. Do not
review the other ~28k upstream GemRB commits.

### 3.2 Symbol manifest

`docs/PSTEE_SYMBOL_MAP.md` + `data/Torment-3.2.2.syms` generated by `tools/gen_syms.py` from the index:
name, RVA, convention, expected bytes, prologue length, evidence (Linux EA + why), confidence
(`confirmed` = probe passed in game, `strong` = same strings/constants/calls on both sides, `tentative`).
Every row in table 2.1 that a phase needs must reach `strong` before its hook is written and `confirmed`
before the phase is accepted.

### 3.3 Prove the five critical points (each one is an L1 in-game test)

1. **Loader:** `version.dll` loads `pst_forever.dll` on the main thread; vanilla launch unchanged.
2. **Tick hook:** a diagnostic callback once per `CInfGame::SynchronousUpdate`; log cadence and thread.
3. **Actor ownership:** skip `CGameSprite::AIUpdate`'s action/script/walk work for one actor (animation
   and effects still run) while the rest of the area keeps moving.
4. **Native swing:** one director-issued swing through the engine's own `Swing`/attack code (real roll,
   damage, triggers).
5. **Completion:** detect the swing's hit frame / animation end and the first projectile arrival
   (`OnArrival`/`DeliverEffects`) without fixed delays.

Then the separate high-risk gate: can effect processing (`ProcessEffectList`) run against an actor-local
clock (read sites of `CTimerWorld` time inside it)? Document the answer before Phase 5.

**Phase 0 deliverables:** `docs/GEMRB_TBC_LESSONS.md`, `docs/PSTEE_SYMBOL_MAP.md`,
`docs/PSTEE_HOOK_FEASIBILITY.md`, and the five L1 tests passing.

---

## 4. Target architecture

Keep the engine-specific code very thin. The director should operate on an adapter API and never scatter raw addresses/object offsets throughout the combat logic.

```text
PST:EE process
  |
  +-- version.dll (proxy loader)
        |
        +-- pst_forever.dll
              |
              +-- core: VersionGuard + Symbols + Hooks + Events + Lua + Config + Log   (shared)
              |
              +-- tbc module
              |
              +-- PSTEEEngineAdapter
              |     +-- actor/map/game handles
              |     +-- action/script adapter
              |     +-- movement/path adapter
              |     +-- attack/spell/item adapter
              |     +-- animation/projectile adapter
              |     +-- effect/time adapter
              |     +-- UI/input adapter
              |
              +-- CombatDirector
              |     +-- Combatant sidecar state
              |     +-- initiative/order
              |     +-- turn state machine
              |     +-- movement budget/walker
              |     +-- reactions
              |     +-- AI intent scheduler
              |     +-- combat clock
              |     +-- event queue
              |
              +-- Lua/UI bridge + configuration
```

### 4.1 Engine adapter

Create a narrow adapter layer with named operations, e.g.:

- `GetCurrentArea()` / `EnumerateActors(area)`;
- `GetActorHandle(actor)` / `IsActorHandleValid(handle)`;
- `GetActorStats(handle)` / `GetActorPosition(handle)`;
- `GetInitiativeInputs(handle)` / `GetMovementAllowance(handle)` / `GetAttacksPerRound(handle)`;
- `StopActorActions(handle)` / `SetActorFacing(handle, target)`;
- `PlanPath(handle, destination)` / `WalkPathStep(handle)` / `GetPathCost(path)`;
- `RunActorScriptPass(handle)` / `ConsumeActorTriggers(handle)`;
- `ResolveNativeAttack(attacker, target)`;
- `BeginNativeCastOrItemUse(actor, action)`;
- `GetAnimationSequenceState(handle)` / `GetProjectileState(id)`;
- `ApplyActorUpkeep(handle, localTime)`;
- `SetSelection(handle)` / `PostCombatEvent(event)`.

All addresses, offsets, vtable slots, and calling-convention assumptions belong inside the adapter/hook layer. The director and tests should call stable C++ interfaces.

**Testability rule (the main token saver):** the director is engine-agnostic C++ behind an `ITbcEngine`
interface (the operations above, on actor handles). It builds twice: into `pst_forever.dll` with the real
adapter, and into the host unit-test exe with `FakeEngine`, a scripted world (actors, positions, HP, APR,
animation frames, projectile arrivals) that the tests step tick by tick. Initiative, budgets, path
clipping, the turn state machine, joins, reactions, handback and the combat clock are all tested there
in milliseconds; the game is only needed for adapter behaviour (L1 tests). Port the GemRB director's logic
into this core (both are GPL-2.0-or-later), keeping its tests' expected values.

### 4.2 Hook system and safety (shared core; see `docs/ARCHITECTURE.md` §3)

- Resolve all functions from symbols where possible. Use signature scanning as a version-specific fallback, not as a substitute for understanding the code.
- Prefer RVA + masked signature + surrounding validation over absolute virtual addresses.
- Verify expected bytes before patching. If any required hook fails validation, disable the feature and leave the game running normally.
- Keep the original function trampoline and define whether each hook runs before, after, or instead of the original.
- Add recursion/reentrancy guards and thread assertions. Engine object access should normally stay on the game's simulation/UI thread.
- Do not allocate, start threads, or install hooks inside `DllMain` beyond minimal bookkeeping. Defer initialization to a known safe point after the engine is initialized.
- Keep logs off the hot path where possible. Include build fingerprint, hook status, actor IDs, turn state, and crash/hang diagnostics.
- Use a side table for extension state instead of writing new fields into engine objects. Key entries by a stable engine actor ID if one exists; otherwise use pointer plus a generation/lifetime token and aggressively invalidate on actor destruction/area change.
- Never retain raw actor pointers across frames without revalidating them.

### 4.3 Combatant ownership

GemRB adds an internal `IF_COMBAT_OWNED` flag. PST:EE cannot be assumed to have a spare flag or the same object layout. Implement the equivalent as an extension-owned sidecar set.

For each owned actor, suppress only the normal real-time systems that the director now controls:

- script/action processing;
- ordinary action queue execution;
- ordinary movement step/path advancement;
- ordinary attack timing/attack action re-queueing;
- bump-away/bump-back path assignment.

Continue the systems that must remain alive:

- sprite/stance animation update;
- projectile update where required by the active action;
- visual and audio effects;
- state/death observation;
- UI/rendering;
- world simulation for actors not owned by the director.

At takeover, stop the actor cleanly: cancel/clear the old path and action queue, settle stance to ready unless a status forbids it, clear movement/backoff state, face a nearby enemy, and preserve pending triggers needed by the next AI decision.

At handback, clear extension ownership, stop director movement, restore a stable ready/idle state, restore ordinary actor speed/running flags, rebase any local timers/effects, and let the actor's script react to combat outcomes.

### 4.4 CombatDirector state machine

Use explicit states. Suggested top-level states:

- `Inactive`
- `Starting`
- `TurnActive`
- `ResolvingReaction`
- `SuspendedForDialogOrCutscene`
- `Ending`

Suggested active-turn steps:

`Begin -> Upkeep -> Decide -> Move -> Act -> WaitForNativeSequence -> Resolve/Record -> (Move/Act again if allowed) -> End`

Requirements:

- Every transition is driven by a known engine event/state change.
- Use timeouts only as emergency watchdogs with diagnostic output; never as the ordinary turn-completion rule.
- The state machine must be restartable after dialog/cutscene interruption without rerolling initiative or losing the current round/turn order.
- A turn cannot finish mid-walk, halfway through an attack animation, or before required projectile impact resolution.
- If the user toggles turn-based mode off mid-fight, perform a controlled handback rather than simply removing hooks.

---

## 5. Implementation phases

### Phase 1 — Core and observability (shared with every module)

Delivered by the `pst_forever` core: loader, version guard, manifest-driven hooks, log, `pst_forever.ini`
`[tbc] Enabled=0/1`, dev pipe commands `tbc.state`, `actors`, `actor ID`, `hooks`.

**Acceptance:** the game starts and plays normally with the DLL installed; the log shows the build and the
validated hooks; `Enabled=0` installs no `tbc` hooks; an altered exe copy is refused (L0 + L1 test).

### Phase 2 — Actor model, combat detection, takeover, and handback

1. Enumerate party and area actors reliably. Determine stable IDs and lifetime rules.
2. Determine how PST:EE represents party membership, hostility/EA, schedules, actor life/death, and current targets.
3. Add `Combatant` sidecar state keyed by stable actor handle.
4. Implement combat detection from the real engine events: party attack/hostile action, hostile actor engaging/seeing party, or an equivalent confirmed combat-state signal.
5. Build the initial participant list. Keep passive neutrals and uninvolved NPCs out.
6. Take over combatants, suppress their real-time actions/movement, and verify that their animations still update.
7. Add mid-fight join handling for actors that attack/target a combatant, are made hostile, or respond through help/shout behavior.
8. Implement handback for normal combat end, death, map/area change, and dialog/cutscene.

**Acceptance:** in a staged fight, only combatants stop using the real-time action scheduler; unrelated NPCs continue moving and scripting; no actor runs in place; after combat every surviving actor returns to normal behavior.

### Phase 3 — Initiative, turn order, and basic player control

Implement the reference rules, using PST:EE native values:

- one initiative roll per combat;
- d10 + weapon speed - Dexterity reaction bonus, with the fork's haste/slow adjustments;
- stable tie-breakers;
- per-turn movement/action/attack budget;
- round advancement and death removal;
- party member switching during an active party group;
- End Turn keyboard/button action.

Do not build the complete HUD yet. A debug timeline/log is enough for this milestone.

**Acceptance:** deterministic staged encounter produces a stable initiative order; End Turn advances exactly once; dead actors are removed; party members can hand off the active turn without losing the round.

### Phase 4 — Movement and path accuracy

1. Find the native pathfinder and traversability cache update lifecycle.
2. Add director-requested path planning and path-cost calculation.
3. Clip the path to the actor's current movement budget.
4. Walk it through native actor movement/animation updates, but prevent the ordinary scheduler from also stepping the actor.
5. Keep preview path and committed path identical.
6. Block ordinary bump behavior for combatants; ensure bystanders route around owned actors.
7. Support cancel-move with the correct spent-movement accounting.

Do not implement a separate grid/pathfinder unless the existing pathfinder cannot be safely invoked. The GemRB fork's custom walker exists to control *when* movement occurs, not to throw away the engine's navigation rules.

**Acceptance:** repeatedly click varied reachable/unreachable destinations; every actor ends at the promised path endpoint or correct partial endpoint; movement never exceeds its budget; there are no stuck walking animations, no stale path requests, and no unintended bumping.

### Phase 5 — Native attack resolution, swing sequencing, spells, items, and local time

This is the most important mechanics phase.

**5A. Native attack resolution**

1. Trace the entire PST:EE attack path from an attack action through attack-round setup, animation, roll, damage, and target side effects.
2. Identify the narrowest callable native routine that performs the actual swing/roll/damage without independently scheduling the next real-time attack.
3. Have the director request one native swing at a time.
4. Synchronize resolution with the actual animation hit frame or the same engine event that launches the hit/projectile.
5. Wait for the animation to complete before scheduling the next swing.
6. Preserve native APR and attack-round bookkeeping; do not duplicate `InitRound` or consume attacks twice.

**5B. Side-effect audit**

For every director-issued attack, compare vanilla and turn-based behavior for:

- attacker/target IDs and last-target fields;
- `AttackedBy`, `HitBy`, and `LastAttacker` triggers;
- hostility / Enemy-Ally changes and retaliation;
- help/shout propagation;
- attack, hit, damage, and death sounds/voice lines;
- spell and melee combat log messages;
- kill variables, death scripts, XP, morale, and corpse handling;
- item charges, weapon use, and ammunition/projectiles.

Build a parity checklist and a runtime event trace. If the chosen native resolution routine does not perform a side effect, invoke the corresponding native engine operation rather than silently omitting it.

**5C. Ranged and spell sequences**

- Detect projectile launch and first hit/explosion.
- Keep the caster's turn until the initial spell resolution has completed.
- Distinguish instantaneous effects, delayed onsets, repeated pulses, and lingering area effects.
- Avoid applying the same effect twice when the director observes the projectile and the engine also applies it.
- Ensure End Turn during a pending spell has a defined behavior (the fork waits until the spell has landed).

**5D. Combat clock / effect timing**

Do an explicit audit of every actor/effect timer and every read of world `GameTime` before implementing this.

Reference behavior:

- world time continues outside combat and for unowned creatures;
- each owned combatant has a local clock that advances during its own upkeep/turns;
- its effects, regeneration, poison, and per-round limits advance on its own turn;
- the director evaluates its effect queue in a per-actor clock scope;
- on handback, effect expiries are rebased to world time, preserving the remaining durations.

Implement a PST:EE equivalent only after proving where the engine reads time during effect processing and nested callbacks. A simple global time freeze is specifically not acceptable. If a robust actor-local time is impossible, document the exact subset of effects that cannot be preserved and design an explicit fallback/setting rather than silently breaking them.

**Acceptance:** compare vanilla and turn-based attack outcomes with a recorded random stream; test multiple APR; test melee and ranged swings; test a spell with delayed projectile impact; test repeating and lingering areas; test poison/regeneration/duration timing across multiple other creatures' turns and after handback.

### Phase 6 — BCS/AI integration

1. Determine whether PST:EE exposes parsed BCS/script objects and native trigger/action functions to the extension. Prefer native execution over a second BCS interpreter.
2. Inventory the PST:EE BCS corpus and IDS/action/trigger tables. Re-run the fork's analyzer against the target EE resource set; do not reuse old counts as if they describe EE.
3. Classify combat behavior: engage, attack/reevaluate, spell/use item, flee/panic, help/shout, summon, turn hostile, wait, movement/escape, dialogue/surrender, and state/variable changes.
4. Execute AI decision logic once per actor turn rather than every simulation tick.
5. Preserve the original script-slot ordering, response selection, weighted responses, object selectors, `Continue` behavior, and variables.
6. Split instant actions from time-taking intent only if the native engine exposes a safe way to preserve queue semantics. Otherwise first implement one native script pass per turn and keep the original queue semantics as much as possible.
7. Carry unfinished `Attack`/chase and other multi-turn intents forward; don't make a script restart from scratch in a way that changes target selection or variables.
8. Implement a tactical fallback for hostiles with no useful combat script only after the script-driven baseline works.
9. Add special handling for `Help`, `AttackedBy`, `Heard`, summons, fleeing, and combat-start/stop actions.

The fork's planned archetypes are useful for diagnostics: guard, brute, coward, fight-then-flee, caster, and caller/summoner. They should not replace exact script execution.

**Acceptance scenarios:**

- a scripted mage casts its intended spell sequence, one decision/turn at a time, then melees;
- a coward/fleeing actor flees at the correct health/trigger threshold;
- a guard joins/responds to help;
- a caller/summoner summons and those summons join the same encounter;
- dialogue/surrender interrupts combat safely;
- scriptless hostiles have a bounded fallback behavior.

### Phase 7 — Reactions and 2e-specific maneuvers

Add as independent, testable features/toggles:

- opportunity attacks when movement leaves melee reach;
- one eligible reaction per reactor per round;
- interruption at the reach boundary followed by continuation of the original path;
- surprise round based on visibility/awareness at combat start;
- Run, Withdraw, Block, Trip, Knockdown, prone and stand-up sequences where appropriate to PST characters/creatures;
- turn/action constraints for equipment changes, quick items, spells, scrolls, thief skills, and modal abilities.

Do not add Delay by default; the GemRB fork's current progress says it was removed. Do not assume D&D 5e action economy. Preserve the fork's final rules only where they are intentional and tested.

**Acceptance:** isolated tests for every maneuver and reaction, including status-disabled reactors, multiple reactors, actor death mid-reaction, and cancellation of the mover.

### Phase 8 — UI and player input

Keep the combat event model independent of the UI.

**Native-to-UI bridge**

Expose a minimal read-only snapshot and event queue, plus a small command API:

- get active combat state/round/order;
- get actor handles, initiative, HP, movement remaining, actions/attacks remaining, and status flags;
- poll combat events;
- end turn;
- switch to an eligible party member;
- request/cancel move;
- select an action/target.

Do not expose raw engine pointers to Lua or UI code. Use stable actor handles and validate all commands on the native side.

**UI**

- Start with a simple timeline, active-turn indicator, movement remaining, action/attack budget, and End Turn button.
- Add enemy/player cards, attack feedback, combat log, floating damage/miss/save text, and hotbar after the basic UI works.
- Reuse PST:EE's existing interface assets and supported UI/Lua extension mechanisms. Investigate `UI.MENU`, Lua hooks, and existing PST:EE UI mods (for example Book of Torment) as evidence of the available UI surface; do not assume a Lua-only mod can control core combat scheduling.
- Draw world path/reach/AoE overlays through an existing native rendering hook if needed. Keep rendering passive; it should not mutate combat state.
- Support the target UI scale/resolution and avoid stealing focus from the game view for keyboard controls.

**Acceptance:** the full basic combat loop is playable from the UI; every button and key action produces a validated native command; opening inventory, journal, dialog, and other full windows does not corrupt or hide the turn state.

### Phase 9 — Regression automation and compatibility

Use the shared test layers (`docs/ARCHITECTURE.md` §4): FakeEngine unit tests for every director rule, L1
in-game suites (`tools\ingame.ps1 -Suite tbc_*`) for the scenarios below, dev-pipe probes for state.
Create a repeatable test harness. The GemRB fork's useful pattern is staged saves plus a scriptable driver that can start the game, send input, capture screenshots/logs, and fail on a crash or a stalled turn.

Minimum scenario matrix:

| Scenario | What it proves |
|---|---|
| Mortuary ghoul encounter | Basic start, initiative, player turn, enemy turn, end. |
| Creature with 3 APR | Attack-round/APR preservation and one-swing-at-a-time animation. |
| Ranged attacker | Native missile timing and first impact handling. |
| Spell caster | Cast action, projectile/effect resolution, spell log and charges. |
| Delayed/repeating/lingering spell | Effect onset, area pulses, duration, local clock. |
| TNO regeneration and poison | Per-combatant upkeep and time correctness. |
| Fight starts from sight/hostile script | Correct combat-start detection. |
| Mid-fight guard/help/summon | Dynamic participants and trigger handling. |
| Coward/flee/fight-then-flee | BCS turn decisions and movement intent. |
| Neutral NPC nearby | Neutral is not accidentally pulled into combat. |
| Neutral becomes hostile | Native Enemy/EA transition causes appropriate joining. |
| Opportunity attack | Movement interruption, reaction limit, resume path. |
| Death during own/other turn | Actor removal, native death processing, no stale handle. |
| Dialog/cutscene during combat | Suspend, handback, resume without rerolling. |
| Area transition | Combat teardown and full actor handback. |
| Save/load | No serialized stale pointers or orphaned director state. |
| Unknown executable build | Version guard refuses hooks safely. |

For rule parity, run identical staged fights in vanilla real-time and the extension with a recorded/replayable random stream if the engine allows it. Compare state changes and events, not just screenshots.

Add a structured test log with one record per transition/event: build ID, encounter ID, round, active actor, step, action/intent, movement budget, attack count, native event, and actor state delta. Add a watchdog for stalled turns and main-thread hangs.

### Phase 10 — Packaging and release

- Installer checks supported PST:EE version and architecture before writing files.
- Loader displays a clear unsupported-build message and leaves the vanilla game intact.
- Provide one configuration toggle to enable/disable turn-based combat and granular options for initiative weapon speed, surprise, opportunity attacks, combat speed, and HUD features.
- Do not require GemRB, source builds, or a developer toolchain for end users.
- Document the exact tested executable build, distribution (Steam/GOG), installation order, known limitations, and crash-log location.
- Add a clean uninstall path that removes only extension-owned files.

---

## 6. Porting matrix — take from GemRB versus rewrite for PST:EE

| GemRB reference | Take/adapt | Rewrite for PST:EE |
|---|---|---|
| `CombatDirector` architecture | Ownership, initiative, turn state machine, joining, handback, combat event model. | Native game tick placement, actor identity, engine hooks, lifecycle, sidecar storage. |
| `Combatant` struct | Budget/flags/initiative/local clock concepts. | Actor handles and status inputs from PST:EE; do not assume identical fields/stats. |
| `TakeOver` / `HandBack` | Clear stale actions/path, settle stance, preserve triggers, rebase time. | Exact PST:EE action queue and actor state transitions. |
| `RequestPath` / `PlanPath` / `ClipPath` | Same-path preview/commit, movement-budget clipping, fixed obstacles. | PST:EE pathfinder ABI, traversability cache, coordinate scale, movement step hooks. |
| `RequestAttackRound` / `ResolveSwing` | Swing-by-swing scheduling while delegating rules. | PST:EE native attack-round and hit-resolution functions. |
| `CombatClockScope` | Per-actor clock concept and handback rebasing. | PST:EE clock/effect internals; must be proven with symbols and dynamic tests. |
| `CombatAnalyzer` | Action/trigger classification, archetype diagnostics, unmapped-action reporting. | Analyze the installed PST:EE BCS/IDS corpus and use EE's native script runtime. |
| `CombatHud` / `GetCombatEvents` | Event-fed HUD and separation of presentation from decisions. | PST:EE rendering/UI/Lua bridge and UI scaling. |
| `tb_autotest.ps1` and staged fights | Automated staged encounters, crash/stall detection, screenshot/log comparison. | PST:EE launcher/automation, target saves, native debug commands. |
| Earlier `TurnManager` freeze approach | Only its bug list as a warning. | Do not port its freeze-everyone-but-active-actor architecture or fixed swing-tick pacing. |

**Licensing (decided 2026-11-20):** pst_forever is GPL-2.0-or-later, like GemRB, so the fork's director, analyzer, rule functions and tests may be ported directly. Keep GemRB's copyright lines in ported files, mark every file with `SPDX-License-Identifier: GPL-2.0-or-later`, and ship the source with releases.

---

## 7. Target build and version policy

The only target is GOG 3.2.2.0 `Torment.exe` x64 (SHA-256 `99ae1586…a3c5`), matched by the same release's
non-stripped Linux binary. The manifest is `data/Torment-3.2.2.syms`. A Steam 3.2.2 exe with a different hash,
or any later patch, gets its own manifest after re-running `tools/re_pipeline.sh` on it.

## 8. Definition of done

A first public release is not done merely because it displays a turn order or disables real-time attacks. It is done when:

- the correct PST:EE executable is version-validated and loaded safely;
- combat starts from the normal game encounter flow;
- only the actual participants are director-owned;
- the rest of the world continues running;
- each participant gets a stable turn with correct initiative, movement, actions, and original APR;
- the native attack/spell/item code resolves outcomes and side effects;
- attack animations, projectiles, and delayed spell events are synchronized;
- effect duration, regeneration, poison, and handback time are correct for the supported test cases;
- native scripts and important combat triggers still drive enemy behavior;
- deaths, summons, joiners, neutral hostility, dialogue, cutscenes, and area transitions are handled;
- the player can control turns through a usable HUD/input path;
- automation can complete representative staged fights without crashes, softlocks, or orphaned actors;
- unsupported game builds are rejected safely.

If the engine adapter cannot reliably intercept actor scheduling, native swing resolution, animation completion, and actor-local effect timing, stop and document that blocker. Do not hide it with a global time freeze or a fixed-delay imitation of turn-based combat.

---

## 9. Source references reviewed

- [`gemrb-pst-forever` README](https://github.com/b5635/gemrb-pst-forever) — project scope, implementation status, current compatibility caveat.
- [`TURN_BASED_PLAN.md`](https://github.com/b5635/gemrb-pst-forever/blob/master/TURN_BASED_PLAN.md) — rev. 3 design, failure analysis, work packages, progress.
- [`CombatDirector.cpp`](https://github.com/b5635/gemrb-pst-forever/blob/master/gemrb/core/TurnBasedCombat/CombatDirector.cpp) and [`CombatDirector.h`](https://github.com/b5635/gemrb-pst-forever/blob/master/gemrb/core/TurnBasedCombat/CombatDirector.h) — live implementation of initiative, ownership, turn sequencing, movement, reactions, spell waits, and event generation.
- [`CombatAnalyzer.cpp`](https://github.com/b5635/gemrb-pst-forever/blob/master/gemrb/core/TurnBasedCombat/CombatAnalyzer.cpp) — action/trigger mapping and combat archetype classification.
- [`CombatHud.cpp`](https://github.com/b5635/gemrb-pst-forever/blob/master/gemrb/core/TurnBasedCombat/CombatHud.cpp) — UI/event presentation reference.
- [`Actor.cpp`](https://github.com/b5635/gemrb-pst-forever/blob/master/gemrb/core/Scriptable/Actor.cpp), [`Map.cpp`](https://github.com/b5635/gemrb-pst-forever/blob/master/gemrb/core/Map.cpp), and [`Game.cpp`](https://github.com/b5635/gemrb-pst-forever/blob/master/gemrb/core/Game.cpp) — GemRB integration points used by the director.
- [`CLAUDE.md`](https://github.com/b5635/gemrb-pst-forever/blob/master/CLAUDE.md) — local project architecture, test commands, and PST:EE parity/reverse-engineering references.
- [EEex overview](https://eeex-docs.readthedocs.io/en/latest/Introduction/overview.html) and [EEex getting started](https://eeex-docs.readthedocs.io/en/engine-x86/Introduction/getting%20started.html) — extender architecture and explicit PST:EE compatibility warning.
- [Beamdog PST:EE 3.2 update](https://www.beamdog.com/news/32-update-planescape-torment-enhanced-edition/) and [SteamDB PST:EE depots](https://steamdb.info/app/466300/depots/) — current-version context; re-check the user's installed executable before choosing the target.

