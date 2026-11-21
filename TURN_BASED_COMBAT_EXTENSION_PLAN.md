# Planescape: Torment: Enhanced Edition — Turn-Based Combat Extension

**Implementation handoff for Claude Code** (rev. 2, 2026-11-20: updated with verified PST:EE facts; rev. 1 was written without access to the binary)  
**Reference implementation:** the GemRB fork at `D:\gemrb` ([`b5635/gemrb-pst-forever`](https://github.com/b5635/gemrb-pst-forever)): `TURN_BASED_PLAN.md` §1 (why freezing failed) and §10 (what works)  
**Target:** GOG PST:EE 3.2.2.0, `Torment.exe` x64, SHA-256 `99ae1586…a3c5` (see `docs/ARCHITECTURE.md` §1)  
**Shape:** the `tbc` module of `pst_forever.dll` (not a separate DLL). Loader, version guard, hooks, Lua bridge, logging and the test layers are shared core, described in `docs/ARCHITECTURE.md`.  
**Goal:** deliver the fork's turn-based combat experience as an optional native extension/mod that runs with PST:EE, without replacing the game engine or requiring players to run GemRB.

---

## 0. Mission and constraints

Adds a genuine turn-based combat mode to PST:EE. Preserve PST's AD&D 2e rules, scripts, dialogue, effects, items, spells, and existing game data. The extension should coordinate the existing engine rather than recreate the combat rules.

The extension is not a GemRB port at the engine-source level. GemRB is the behavioral and architectural reference. PST:EE remains the runtime and source of truth for all engine behavior.

### Hard requirements

1. **Keep the original game rules.** Use PST:EE's existing attack, THAC0/AC, damage, critical, save, resistance, spell, item, death, XP, morale, and trigger code wherever possible. Do not write a parallel rules engine.
2. **Take over combatants instead of freezing the whole world.** During a fight, only the director-owned creatures should stop using the real-time action/script/movement scheduler. Townsfolk, area scripts, ambient animations, music, timers, and unrelated NPCs must continue running.
3. **Use an explicit event-driven turn state machine.** A turn ends when its movement/action/animation/projectile work is complete, not because an arbitrary idle timer expired.
4. **Preserve observable script behavior.** Triggers, object selectors, variables, hostility changes, summons, help calls, barks, fleeing, and dialogue transitions must continue to behave as PST expects.
5. **Support safe handback.** On combat end, death, area transition, dialogue, or cutscene, the extension must return actors to the engine in a clean state.

---

## 1. Research findings from the GemRB fork

### 1.1 What the fork actually is

The repository describes itself as a heavily modified, Windows-focused GemRB fork for **classic PST**, not PST:EE. Its README explicitly says that the game engine/data being run is a reference implementation, not a drop-in DLL source tree.

The most important reference files are:

- `TURN_BASED_PLAN.md` — architecture, the abandoned first approach, combat rules, AI design, work packages, progress, known gaps.
- `gemrb/core/TurnBasedCombat/CombatDirector.cpp` and `.h` — actual director and combatant state.
- `gemrb/core/TurnBasedCombat/CombatAnalyzer.cpp` and `.h` — BCS action/trigger classification and script profiling.
- `gemrb/core/TurnBasedCombat/CombatHud.cpp` and `.h` — event-fed combat HUD/dice panel.

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

The current fork implements or documents the following behavior. Verify each item against the current source before porting; 

**Combat start and participation**

- Combat is started when the party becomes involved in a hostile encounter; the fork checks party/hostile state rather than treating every nearby neutral as an enemy.
- Living party members on the map become participants.
- Eligible hostiles are selected based on hostility, active schedule, targeting/visibility, and later join events.
- Creatures outside the encounter remain on the normal real-time update path.
- Non-combatants that attack a combatant, target one, or otherwise become involved are added mid-fight.
- Neutrals are not combatants merely because they are nearby. An NPC should first become genuinely hostile through the normal game action/state path.
- A newcomer inserted after its initiative slot has already passed waits until the next round.

**Initiative**

The current implementation rolls initiative once per combat, do not reroll per round. Lower values act first:

`initiative = d10 - DexterityReactionBonus

**Turn budget and attacks**

Everything is budgeted for one action

- Each creature receives its movement allowance and action/attack budget at the start of its turn/round. When attacking it uses up an action
- Preserve PST's original APR calculation and attack-round allocation. A creature with multiple attacks executes its native attack round swing by swing; do not convert it to a flat one-attack-per-turn system. If someone has 1 1/2 attacks per round, they can attack twice this round and next round they can only attack once etc.
- Party members sharing the active party group can be switched between during the group's turn; ending the last party member's slot passes control to the next initiative slot.

**Movement**

- A movement preview and the actual confirmed walk must use the same path.
- Compute a path using the engine's pathfinder, clip it to remaining movement, then walk it step by step.
- A turn must not end with the actor still walking or with a stale path queued.
- The fork's path implementation had to refresh/read the traversability cache before its synchronous path requests; otherwise stale cache data caused false no-path results and self-blocking. Find the PST:EE equivalent before implementing custom path requests.

**Spells, items, projectiles, and effects**

- Casting is an action inside the caster's turn. The director may hold the turn until the cast animation and the spell's first projectile impact/explosion have resolved.
- Do not advance to the next actor while a projectile/effect that is part of the current cast is still pending, unless the existing spell's semantics explicitly require ongoing world behavior.
- Buffs / debuffs / effects lasts in combat rounds. In PST, a combat round is 5 seconds. So if a debuff was applied for 15 seconds, then it will expire in 3 rounds.
- Effect durations, poison, regeneration, periodic damage/healing, and spell duration must advance in a way that corresponds to turns (considering a combat round is 5 seconds), then be rebased correctly to world time when an actor is handed back.
- Quick-use items, inventory items, weapon swaps, equipment changes, and spell scrolls require and consume an action
**Reactions and combat extras**

The fork has opportunity attacks, meaning characters moving out of melee range can be attacked by hostiles. A character can only opportunity attack once, but this limit resets when their turn is reached again.

**AI and scripts**

- PST's BCS scripts determine whether a creature engages, who it targets, when it flees, calls for help, summons, casts, changes behavior, or starts dialogue. The GemRB design adds turn-based movement/tactics around those decisions rather than replacing the scripts with generic combat AI.
- The analyzer classifies actions such as `Attack*`, `Spell*`, `ForceSpell*`, `UseItem*`, `RunAwayFrom*`, movement/escape, waits, dialogue, and instant state/variable actions. It also classifies triggers including `See`, `AttackedBy`, `Help`, `Die`, `HPPercentLT`, `HitBy`, `Heard`, `Allegiance`, and combat counters.

**UI and feedback**

- The fork exposes a typed combat event feed (`GetCombatEvents`) for combat start/end, round/turn start, swing, damage, death, intent, and reaction events.
- The timeline, turn banner, character/action bar, dice feedback, hover hit-chance breakdown, floating combat text, and world overlays consume that feed.
- Keep UI rendering separate from combat decisions. The native director should publish snapshots/events; Lua/UI should render and send commands back through a narrow bridge.
- `CombatHud.cpp` is a GemRB renderer implementation and is not directly portable to PST:EE. Port the UI behavior and event model, not GemRB's drawing calls.


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


