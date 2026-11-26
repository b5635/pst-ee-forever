# Scenario test plan: the hardest fights and scripts, in the player's own saves

50 scenarios that exercise turn-based combat (and the modules around it) against the game's real creature scripts,
in the saves of a full playthrough. I (Claude) run each one, read the logs and screenshots, and record the outcome
here. Input: the creature-script audit (every one of the 793 creature scripts decompiled, `tools/bcs.py audit`),
the D_Simpson walkthrough (`Documents\...\all saves\walkthrough.txt`) and the 187 walkthrough saves.

## How to run

| Step | Command |
|---|---|
| Install the walkthrough saves (once; slot + 700000, never collides with the test saves) | `tools\install_walk_saves.ps1` (`-List` = names only) |
| List the creatures of a save's area (ids, script names, shown names, EA, hp, position) | `tools\scenario.ps1 -Id S00 -Save 000700083` |
| Run one scenario | `tools\scenario.ps1 -Id S14` (`-Shots` = screenshots every 3 s into `logs\scen_S14\`) |
| Read the fight afterwards | `<game>\pst_forever\logs\combat.log` (`QUEUE`, `TIMER`, `TURN`, `STOP`, `AI` lines), `pst_forever.log` (`tbc:` lines) |
| Look at one creature's script | `python -I tools/bcs.py dump 0610RAVL`, `who RAVEL`, `users 0610RAVL`, `grep REGEX` |

A scenario is `tests\ingame\scenarios\SNN_name.lua` with a header line
`-- scenario: save=000700162 env=PSTF_TBC_AUTOPARTY=1 timeout=400`; `tools\scenario.ps1` puts
`tests\ingame\scenarios\lib.lua` in front of it and runs it in the save. Exit 0 = ok, 1 = ISSUE lines, 2 = FAIL.

`lib.lua` gives every scenario: `S.steps[tick] = fn`, `S.tick(frame, cur)`, `S.find(script or shown name)`,
`S.spawn(CRE, dx, dy)`, `S.near(id, dx, dy)`, `S.global(name, scope, value[, who])`, `S.watch[id] = true` (queue and
timers at every turn change), `S.issue(text)`, `S.finish(why)`, `S.fail(text)`. It also checks on every tick:

- **held**: a combatant moves more than 6 px outside its own turn;
- **stuck**: one turn runs longer than `S.turnCap` ticks (900);
- **endless**: the fight passes `S.roundCap` rounds (40).

After the run, `scenario.ps1` scans the log for `script state lost`, `faulted`, `CRASH (` and Lua errors.

Probes added for this plan: `PSTF_Queue(id)` (current and queued actions, how many are script state),
`PSTF_Timers(id)` (script timers, id:seconds), `PSTF_FindActor(scriptName[, nth])`, `PSTF_Actors([radius])`,
`PSTF_ActorRefs()`. The log writes `tbc: X: turn end drops queue [...] (script state lost)` and the combat.log writes
`QUEUE` and `TIMER` lines at every AI turn end.

Forcing is allowed: `PSTF_MakeEnemy`, `PSTF_SetHp`, `PSTF_EvalAs(id, 'BCS action')` (`ChangeAIScript`,
`SetGlobal`, `CreateCreature`, `JumpToPoint`, `ApplySpell`), and spawning a CRE next to the party. Scripted area
travel does not work, so every scenario starts in an area a save is in. Spawned creatures get the script their
area actor entry gives them: `tools/bcs.py who CRE` shows it, and `ChangeAIScript("SCRIPT",0)` puts it on them.

## Run order (fixes cascade)

Scenarios are run grouped by the mechanism they share, so one fix is checked by the whole group at once:

1. **Queue across turns** (finding 1): S14 first, then the counter casters S04-S08, S13 (`S.stepCaster`), then the
   Spell/Wait chains S10, S11, S15, S16, S12 and the no-wait chain S09 (`S.chainCaster`).
2. **Timer casters** (script timers count only in their own turns): S01-S03, S17, S31.
3. **Plot state behind a Wait / held triggers** (section B): S18-S26 lean on group 1 and on held creatures' pending
   triggers (ProcessPendingTriggers waits for the turn).
4. **Dialogue inside a fight** (section D) S36-S40, then crowds and fleeing (section C), bosses (E), edge cases (F).

## Setup workarounds (think of these first when a scenario cannot start)

When a scenario's setup fails, it is usually the save or the forcing, not the feature. Try these before calling it
an ISSUE, and add new ones here:

- **Actor not in the save** (dead already, other area, spawned later): spawn its CRE next to the party
  (`cre = 'X', spawn = true`), give it the area actor's script (`script = 'BCS'`, `tools/bcs.py who CRE`), and find
  the spawn as the id that was not there before (`S.ids()` / `S.newIn(before)`), not by name.
- **Name lookups:** `S.find` takes the shown (TLK) name or the script name; the CRE name (`bcs.py users`) is
  neither. List a save's creatures with `-Id S00 -Save PREFIX`.
- **Several BCS actions:** one action per `PSTF_EvalAs` (C:Eval runs only the first); the first call clears its queue
  (takeover), later ones append.
- **Creature far away / its AI idle:** its own `JumpToPoint` may never run; jump the party to it instead (`go = {dx, dy}`).
- **Save loads inside a banter dialogue** (the world waits, every action stays queued): `lib.lua` steps it closed
  (`worldScreen:StepDialog()`) in the first ticks.
- **Script gated on another area's variable** (`Global("X","AR0209",..)` while the save is in AR0208): an unloaded
  area's variables do not read or write; pick a save in that area (`install_walk_saves.ps1 -List`).
- **Corpses:** a creature killed earlier keeps its script name; `PSTF_FindActor` skips the dead now (Ravel after
  "Spoke to Ravel" counts as dead too: spawn her).
- **Banter with several lines:** `lib.lua` steps any dialogue closed while `PSTF_InDialog()` is 1 (set `S.keepDialog`
  for scenarios that test a dialogue).
- **Variable names:** BCS `LOCALScd_int_0` = scope `LOCALS` + name `cd_int_0`.
- **The party kills it too fast:** HP 3000 on the scenario creature (`hp =`), party HP 400.
- **Turns lost to knockdowns** (party AI trips): count them before reading a step counter as stuck (combat.log
  `MOVE ... stands up and loses its action`).
- **Expected counts:** check what the creature really has (a spent `Spell` falls through: Shilandra knows 3 of 5).

## What "working well" means (every scenario)

1. **Same outcome as real time.** Script variables, deaths, hostility, dialogue and cutscenes end as they would
   in real time: the rule "exact on anything a script, dialogue, item or spell can observe".
2. **Nobody acts outside their own turn.** No movement, swing or cast by a waiting combatant (held invariant).
3. **The fight ends.** It ends when one side is gone, dead, fled out of range or no longer hostile. No turn
   hangs (stuck), and no fight goes on forever (endless).
4. **Nothing faults.** No `faulted`, `CRASH`, Lua error or feature turned off.
5. **Readable.** HUD cards, combat text and save banners show the right creature (also after a polymorph).

Outcome column: `-` not run yet; `ok`; `ISSUE: ...` (with the log line); `FIXED` (commit).

## A. Script sequences and caster AI (audit finding 1: the turn end drops the rest of the queue)

An AI turn ends at the first `Wait` after its action, and the clean stop (`ClearAI`) drops what follows. A `Wait`
before the first action is replaced by an ordered attack (the nudge). Real-time scripts that count steps after a
`Wait` stall.

| ID | Save (area) | Setup / forcing | Expected (correct behaviour) | Watch | Outcome |
|---|---|---|---|---|---|
| S01 | 000700162 Spoke To Ravel (AR0610) | Make Ravel hostile (`PSTF_MakeEnemy`); HP 400; party HP 400; watch Ravel | Spell steps advance (LOCALS `Scd_int_1` 1..N); `StartTimer(1,20)` self-spell recurs; at HP < 50 % casts 1601; at death the `0610cut2` cutscene plays and the fight ends cleanly | QUEUE lines (Spell, Wait, IncrementGlobal), TIMER `1:`, cutscene, fight end | ok: Ravel spawned (the save's counts as dead), cd_int_1 1 -> 8 in 9 turns. One earlier run crashed in `CGameArea::SortLists` (all sprites gone at once, dump empty); not reproduced |
| S02 | 000700204 Ready For Final Fight (AR1204) | Turn the Transcendent One hostile (1204TRNA then 1204TRNF); HP 2000 | Signature spells 2902, 2506, 2805, 2802 cast once each, about 30 s of its own time apart; melee between them (`TimerActive(2)`); self-heal 1601 under 51 %; at death `Quit_Game` | TIMER `2:` counts down across its turns; how many turns between big spells | FIXED: timer 2 (30 s) drained ~3 s per own turn (11 turns between big spells); now `[tbc] script_timers` pays each round's missing time at turn start (`CheckTimers(seconds)`): big spells 7 rounds apart, ok |
| S03 | 000700117 Exotic Shopping (AR0609) | Vrischika: set LOCALS `Scd_int_1` 14, make her hostile | Casts 2209 (close), 2405, 2207 (far), each gated by `StartTimer(1,1..4)` | TIMER `1:`; casts per turn | ok (after the MultiFireLeft null fix: the first run faulted the tick handler) |
| S04 | 000700136 Spoke to Splinter (AR0601) | Jumble (in the area), LOCALS `cd_int_0` 1, hostile, HP 3000 | Step 1 (2102 self), step 2 (`Wait(1)` then 2115), step 3 ... counter climbs once per turn | Wait-first steps | ok: 1 -> 7 in 9 turns, one spell each; the 2 turns without a step were knockdowns (stand up = the action) |
| S05 | 000700145 Extravagant Ones Complete (AR0603) | Lady Thorncombe (0603LADY), same as S04 | Same as S04 | Same | ok: one spell per turn (5 steps in 10 turns: 3 knockdowns, 1 step lost inside the engine's cinematic freeze, vanilla) |
| S06 | 000700148 Personal Experiences Done (AR0604) | Quell (0604QUEL), same as S04 | Same as S04 | Same | ok: 1 -> 9 in 8 turns |
| S07 | 000700128 Wizard is Dead (AR13WZ) | Spawn CBOSS, `ChangeAIScript("1300CWIZ",0)`, LOCALS `Scd_int_0` 1, hostile | Steps 2807 at Player1, 2504 self, 2301; past step 3, `WaitRandom(5,10)` then two ReallyForceSpell 2107 | Lead `WaitRandom`: is it nudged into melee? | ok: counter 1 -> 4 (2807 before the fight, then one step per turn) |
| S08 | 000700186 Carceri Return (AR0900) | Hezobol (0900HEZO): LOCALS `Scd_int_0` 1, hostile | Through all 12 steps, one spell per turn (2304, 2502, 2402 ...) | Counter progress per turn | ok: 1 -> 13 in 17 turns; the gaps are knockdowns and turns its `See()` gate failed (nudged attack) |
| S09 | 000700042 Stuck with the Box (AR0306A) | Shilandra (0306SHIL; spawn SHILAND if absent), hostile | `Spell` x5 then Attack with no waits: one spell per turn, none skipped; a spent spell fails without taking the action | Casts per turn (`PSTF_Casts`), ACTION lines | FIXED: was 4 casts in one turn, then ~200-tick turns on spent spells; now one cast per turn, spent spells fall through to Attack at once |
| S10 | 000700130 Inventory Management (AR0600) | Salabesh (0600SALA) and Elobrande (0600ELOB), hostile together | `Spell; Wait(2); Spell; ...` chains: one spell per turn through the list | After the first spell: melee forever = ISSUE | FIXED: a spent Spell took the action so the Wait behind it ended the turn; now it gives the action back: 3 casts (all he has) in turns 1, 3, 4, spent ones falling through, then his RunAwayFrom block |
| S11 | 000700027 Cleared Entrance (AR0208) | Spawn STRAHAN, `ChangeAIScript("0209NECR",0)`, `SetGlobal("0209_Attack","AR0209",1)`, hostile | Spell sequence 2408, 2213, 2207, 2201, 2309, one per turn | `SpellPoint` at a point; chain progress | OPEN (setup): neither the save's Strahan (a corpse) nor a spawned STRAHAN with 0209NECR fires any block (chose nothing every turn); script slot or area variable to check |
| S12 | 000700052 Got the Gem (AR3010A) | Spawn APPRENT..APPRENT5 with 3012APR1..5, LOCALS `Scd_int_0` 1, all hostile | Four casters each walk through their steps (ForceSpell 2107/2207/2202) | Four step counters at once; turn order | - |
| S13 | 000700046 Killed Rotten Williams (AR3006D) | Spawn AOLA (3006AOLA), LOCALS `cd_int_0` 1, hostile | 1106 self, 1103, 1204 ... one per turn | Counter | ok (first run, before the one-cast rule): 1 -> 4 |
| S14 | 000700083 Got The Key (AR0403) | `S14_starved_dog_mage.lua`: spawn SDMAGE + 0403MAGE, MortalKombat=1, hostile | MortalKombat climbs 1 to 5 | First run: counter stuck at 2 (`turn end drops queue [Wait, IncrementGlobal]`) | FIXED 49e4c6c: 1 -> 5, one step per turn |
| S15 | 000700061 Cleared Trash Warrens (AR0108) | Spawn SPELRAT, 0108SRAT, hostile | ReallyForceSpell chain 2207 x2, 2115, 2107 ..., then `AR0108Rat_Leader_Done`=1 | The same first spell every turn, the variable never set = ISSUE | FIXED: an instant ReallyForceSpell plus the lead-Wait skip gave 2 casts in a turn; otherwise one per turn and `Rat_Leader_Done` = 1 after the chain (reset to 0 first: the save has it set) |
| S16 | 000700077 Killed Many-As-One (AR1600) | Spawn SPELRAT, `ChangeAIScript("1600T2LD",0)`, hostile (`AR1600_PC_Enemy`=1) | Leader casts its Spell/Wait chain; `Leaving_2` global timer path works | Chain; global timer | - |
| S17 | 000700070 Spoke with Hargrimm (AR1500) | A Dead Nations skeleton (1500INIT, then 1500HARG) made hostile | HP-gated self-spells (1102, 1202) and timer-gated 1103, 1204, 1301 at the right ranges | TIMER `1:` | ok |

## B. Plot and state variables at risk (audit: script state behind a Wait)

These only matter while the creature is already a combatant, so each scenario starts a fight next to it first
(spawn or turn a hostile nearby).

| ID | Save (area) | Setup / forcing | Expected | Watch | Outcome |
|---|---|---|---|---|---|
| S18 | 000700070 Spoke with Hargrimm (AR1500) | Fight a hostile next to Soego, then attack Soego | Poof, wererat polymorph, `GLOBALSoego`=4, `Enemy()`, `LOCALS Scd_int_0`=1; attacks the party | `SmallWait` drops `GLOBALSoego`=4 and `Enemy()` = plot ISSUE | ok: Soego = 4, cd_int_0 = 1, polymorph 61460 -> 61512 |
| S19 | 000700195 In The Fortress (AR1200) | A hostile shadow next to the party, then attack Deionarra (DEIONS, 1200DEIO) | PlaySequence, then `GLOBALGame_Over`=3 and `DestroySelf` | She stays and gets nudged into melee = ISSUE | OPEN (setup): a spawned DEIONS with 1200DEIO is not attacked by a scripted Attack order |
| S20 | 000700106 It's Adahn Finally (AR0402) | Attack O in the Smoldering Corpse bar during a fight | `AR0402SC_Hostile`=1, fade, O destroyed; the Mercykiller patrons (0402MKGS) turn hostile; Ilquix polymorphs (0402ILQX) | Whole bar joins; fade-out lost = ISSUE; Ilquix card picture after the polymorph | - |
| S21 | 000700186 Carceri Return (AR0900) | Thokola slavers (0900SLV1-3) at `AR0900Curst_Slavers` 24/26/27, see the party | Counter steps 25, 27, 28 | Counter lost behind a Wait | - |
| S22 | 000700091 Got the Grimoire (AR0508) | Attack Lothar | `Kill(Player1)`: TNO dies, wakes in the Mortuary (area change) and the fight ends cleanly | No fault on death or area change; turns stop | ok: the fight ended cleanly after his response |
| S23 | 000700172 Got The Key (AR0704) | Berrog (0704BER1) sees the party during a fight | `ChangeAIScript("0704ber2")` happens | Script swap lost | n/a in this save: Berrog already runs his flee script. Found: a flight still finding its path was nudged into an attack at tick 15 (FIXED) |
| S24 | 000700178 Prison Exploration (AR0702) | Attack Vhailor (VHAIL, 0702VHAI) | Polymorph 61511, hostile, attacks | HUD card and name after the polymorph; save banner position | ok with `provoke` (attacked while neutral: a hostile Vhailor's See block outranks AttackedBy): animation changes |
| S25 | 000700202 Spoke With Practical One (AR1203) | Attack the Incarnation (INCAR2, 1203INC2) | Polymorph 61521, `SetAnimState(36)`, Help; the others answer the Help call (1203INC1/3) | Random `StartRandomTimer(1,15,45)` floats; timers | - |
| S26 | 000700161 I See Ravel (AR0610) | Ravel dies during the fight | `StartCutSceneMode` + `0610cut2`: the fight ends or pauses; after the cutscene nobody is held and the HUD is gone | Cutscene with turn-based on | ok: HP dropped to 1 mid-fight, she dies, the fight ends cleanly |

## C. Crowds, alarms, fleeing (audit finding 3)

| ID | Save (area) | Setup / forcing | Expected | Watch | Outcome |
|---|---|---|---|---|---|
| S27 | 000700165 Rescue Mission (AR0700) | Attack a Curst civilian (0704RUN-type: `Enemy(); Help(); RunAwayFrom`) | The Help call turns nearby civilians hostile and they flee. The fight should stay playable: fleers leave the fight once far, real fighters fight. | Joiners count, rounds, fight end; endless = ISSUE | - |
| S28 | 000700110 Starting One By One (AR0605) | Attack a brothel patron (0605FLEE) | `Brothel_Alarm`=1, `Brothel_Guards`=1, everyone flees; guards come | Number of fleeing combatants; does the fight end? | ok (playable): Brothel_Alarm = 1, Harmonium guards join, up to 19 combatants, every turn <= 160 ticks, no stuck/held; the fight did not end in 9000 ticks (party at 400 HP vs the guards) |
| S29 | 000700097 Inside the Foundry (AR0502) | Attack a Godsman worker (0503WORK/0502GENA) | `Alarm2`=1, `Alarm2_Timer` 14400 set; some fight, some flee | Global timer runs on world time; fight end | - |
| S30 | 000700088 Lower Ward (AR0500) | Attack Giltspur (GILTSP, 0500GILT) | `Call_MK`=1 once; Sarge + 4 patrols spawn at (175,2000), run to Player1 in real time, join on arrival; no second spawn | Spawn count; join timing; RunningAttack | - |
| S31 | 000700041 The Red Thugs (AR0400) | Hive thugs (0400THG8 `StartTimer(1,15)` random aggro) | Thugs pick fights on their timer; the turn-based fight runs | TIMER `1:` frozen while held (timer only counts in their own turns) | - |
| S32 | 000700083 Got The Key (AR0403) | Walk up to the 12 Starved Dogs (EA 255) | 4 party vs 12: initiative, every turn completes, the fight ends; frame rate stays fine | Turn caps, held, endless; combat.log size | - |
| S33 | 000700073 Found the Bronze Sphere (AR1700) | Ghouls in the Drowned Nations; hurt one (1700TRO9 flees at 50 %) | A cornered fleeing hostile within 1100 px: the fight still ends in a sensible way | endless | - |
| S34 | 000700036 Got Ingress Teeth (AR0400) | Hurt a Hive tout made hostile (0400TOUT `EscapeArea` when hurt) | Walks to the exit 31 ft a turn, leaves the area, leaves the fight | Attacks of opportunity on the way; fight end | - |
| S35 | 000700187 Got Ancient Scroll (AR0902) or 000700186 (AR0900) | Curst lynch mob (0900MOB) | `Lynch_Float` steps; the mob fights | Variable lost behind a Wait | - |

## D. Dialogue and cutscenes inside a fight

| ID | Save (area) | Setup / forcing | Expected | Watch | Outcome |
|---|---|---|---|---|---|
| S36 | 000700130 Inventory Management (AR0600) | Fight a hostile, then attack Aelwyn (0600AELN) | `AR0600Attack_Aelwyn`=1 and `Dialogue(Player1)`: she walks up (more than 31 ft?) and talks; after the dialogue the fight goes on | The dialogue never starts (walk capped, AttackedBy used up) = ISSUE | ok for turn-based: made hostile, her Dialogue runs in her own turn (walks up, then talks). Setup caveat: it came from another block (Attack_Aelwyn not set) and the conversation needs real replies, so the test cannot close it |
| S37 | 000700136 Spoke to Splinter (AR0601) | Attack Splinter | `Call_MK`=1, `GLOBALSplinter`=1, dialogue | Same as S36 | ok for turn-based: Splinter = 1, his dialogue opens in his own turn. Same caveat as S36: the reply-driven conversation is not closed by the test (the turn waits on the open dialogue, as the world does) |
| S38 | 000700103 Get Hamrys Senior Plans (AR0510) | Attack the Vault 9 guard (0510VALT) | `Vault_Attack`=1 + dialogue | Same | ok: Vault_Attack = 1, his dialogue opens in his own turn, closes, the fight goes on |
| S39 | 000700123 Exploring Maze (AR1342) / 000700128 (AR13WZ) | Modron constructs (CHIGH/CMED/CLOW, 1300CONS) near (686,498) | Help, Dialogue, Enemy, Attack in one response: dialogue first, then the fight | Dialogue inside the turn | - |
| S40 | 000700189 Trias Time (AR0901) | Trias fight as the walkthrough plays it | Dialogue, then the fight; Trias' sword variable (`Trias_Sword`) decides Game_Over vs fight | No early Game_Over | - |

## E. Bosses and special mechanics

| ID | Save (area) | Setup / forcing | Expected | Watch | Outcome |
|---|---|---|---|---|---|
| S41 | 000700104 Coaxmetal (AR0501) | Coaxmetal's arm (IGARM: `CreateCreatureImpassable`, sparks with `ForceSpellRESNoFeedback`) | Spawned arm joins; spark spells resolve on their owner's turn | Impassable creature in path previews | - |
| S42 | 000700184 Curst Again (AR0800) | Moridor's box demon fight | Boss fight to the end | Big HP; AoO; spells | - |
| S43 | 000700181 Spoke To Fjull (AR1101) / 000700182 Baator (AR1000) | The Pillar of Skulls (1001PILL) and Baator creatures | No movement for the Pillar; the fight is possible or vanilla | Immobile creatures: turns end, no stuck | - |
| S44 | 000700196 First Companion (AR1201) | Fortress shadows plus companion fights (Dak'kon/Annah shadows) | Shadows (many, weak) and the companion fight end; no endless spawns | endless, spawns | - |
| S45 | 000700120 Modron Maze (AR13FY) | Maze modrons, rogue modrons (12 hostile) | Big fight in tight corridors: paths, no push, no stuck | stuck, path preview vs engine | - |

## F. Engine-level edge cases (any save)

| ID | Save (area) | Setup / forcing | Expected | Watch | Outcome |
|---|---|---|---|---|---|
| S46 | 000700083 (AR0403) | Set TNO HP 1, let a thug kill him mid-fight | TNO's death: Mortuary area change; the fight ends; no held creature left over | Fight end reason; HUD gone | - |
| S47 | 000700083 (AR0403) | Kill Morte mid-fight (HP 1) | The dead leave the fight, turn order goes on; Morte's own script on death | Order, no stuck | - |
| S48 | 000000200 PSTF-8-Party (test save) | Spawn 6 Starved Dog thugs (CRE of the AR0403 thugs) next to the 8-member party | 8 party turns plus 6: party slots 7 and 8 get turns and cards | HUD cards, initiative | - |
| S49 | 000700083 (AR0403) | Charm a thug (`ApplySpell` with op 5) mid-fight, then let the charm end | Changes sides on charm (`tbc: X changes sides`), fights for the party, goes back when it ends | Sides, nudge target | - |
| S50 | 000700083 (AR0403) | Make a thug invisible (op 20) mid-fight | Party scripts cannot see it; its turns still run (nudge `Attack(PlayerN)` needs no sight) | No stuck turn; HUD | - |

Extra checks folded into every run (they need no scenario of their own): quick-save refused or allowed mid-fight
(`PSTF_CanSave`), opening the inventory, map and record screens during an enemy turn, the options screen, and
toggling `[tbc] enabled=0` (vanilla behaviour must come back, the toggle check).

## Findings so far

- **Finding 1 fixed (2026-11-26).** An AI turn end keeps the queued script actions for the creature's next turn
  (`engine::ClearAIKeepQueue`: ClearAI without ClearActions, the running response's markers kept), a script Wait
  before the first action is skipped when a non-move action follows (`SkipLeadWait`; a patrol route `Wait;
  MoveToPoint` is still nudged), script bookkeeping (IncrementGlobal, SetGlobal, Enemy, ...) is no action, a second
  action ends the turn and waits for the next one, and a finished turn that waits for its spell to land runs no more
  script (`freeze_ai`). Toggle `[tbc] keep_queue`. S14: MortalKombat 1 -> 5, one step per turn.
- **Held creatures ran scripts on triggers** (fixed): `CGameSprite::ProcessPendingTriggers` (vtable +0x1A8) runs the
  next queued action, instant actions and the script for pending triggers outside ProcessAI. It is now held like
  ProcessAI (dead creatures still run theirs).
- The BCS variable `LOCALScd_int_0` is scope `LOCALS` + name `cd_int_0` (not `Scd_int_0`).
- Walkthrough saves can load inside a party banter dialogue (the world waits): `lib.lua` steps it closed first.
- **A chained Spell list cast 4 spells in one turn** (S09): consecutive `Spell` actions look like one action to the
  economy. Now an AI's turn ends when its spell goes off (`spell being cast` -> none) and the next Spell waits for its
  next turn; a spent Spell fails at once (the casting aura is cleared while it has not cast this turn) so the list
  falls through to the next spell or Attack, like a priority list. Attacks of opportunity keep the reactor's queue.
- **Fault:** `MultiFireLeft` read sprite+0x5128 of a creature gone at its turn end (S03): the tick handler turned
  itself off. Fixed (null check).
- **Script timers ran at about half speed** (S02): they count only in the creature's own turn; now each round pays
  the missing time at its next turn start (`TimerClock`, `[tbc] script_timers`).
- Cosmetic and bookkeeping actions (ChangeColor, StickySinisterPoof, FaceObject, PlaySequence, CreateItem, ...) are
  no longer the turn's action (Soego's poof-and-recolour, Jumble's FaceObject).
- **Setup lessons:** attack a neutral as the player would (`provoke`: the protagonist's Attack order) when its
  See-enemy block would outrank AttackedBy (Vhailor); a scripted Attack order on some creatures never starts
  (Deionarra). Never auto-click dialogue replies outside dialogue scenarios: one returned the game to the main menu
  (S37 first run).
- Engine (vanilla): a cinematic spell freeze (`CPlanescape::SetGameFreeze`) pops and replaces queued actions of the
  sprites in the area; a step queued behind it can be lost the same way in real time (seen in S05).

- **S14 confirmed audit finding 1 in game.** The Starved Dog Mage's step counter stopped at 2; the log shows the
  `IncrementGlobal` behind `Wait(1)` dropped at its turn end. Proposed fix: when a `Wait` ends an AI turn, drop
  only the `Wait` and keep the rest of the queue for the creature's next turn. When a `Wait` comes before the
  first action and a real action follows, skip the `Wait` instead of nudging.
- `tools/bcs.py audit` counts 102 responses with script state behind a Wait, 33 Wait-first casters and 22
  timer-gated casts.
