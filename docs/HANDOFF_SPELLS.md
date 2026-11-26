# Handoff: spell / effect testing (2026-11-25)

Plan and findings: `docs/SPELL_TEST_PLAN.md` (per-spell table, F1-F12). Rule from the user: **spot checks only**
(`tools\spell_suite.ps1 -Only SPWI115,...`), the full run (`spell_suite.ps1` with no `-Only`, ~20 min) only when asked.

## State

- Committed on main: `97e8b9a` (plan, Desert Hell onsets, icon rounds 75 ticks), `7c52f34` (suite measures outcomes,
  `PSTF_Stat`, persistent effects stepped per tick, icons ignore glows).
- **Uncommitted, built and deployed** (check.ps1: build ok, 1 unit failure = stale `spell_results.h`, see below):
  - `status.h`: icon span keeps the length from the cast (no refill between turns); orphan effects take a parent
    only when new; fallback parent = the spell being cast (`OrphanMap::NoteCast`, `engine::CurrentSpell`, sprite +0x47F8).
  - `round_rules.h`: `LosesTurn` (helpless creatures lose their turn), `OnsetBook` (onset kinds no longer stick to
    reused effect memory), more presentation opcodes (7, 8, 138, 347, 353).
  - `combat.cpp`: `PulsePersistent`, per-tick persistent step, `OnsetBook`; `sprites.cpp`: `CurrentSpell`.
  - `tbc_module.cpp`: new persistent effect pulses at once; icons use `effect_round_ticks`; NoteCast per frame.
  - Tests: `tests/ingame/tbc_decay.lua` (passes: Blessing, Vilquar's Eye, Curse, Prayer, Submerge on in the cast
    turn, -1 icon round per owner turn start, gone on the right turn), new unit cases in `test_tbc.cpp` /
    `test_spells.cpp`, suite changes in `tbc_spells_all.lua` (measures the enemy the spell changed, Pacify aimed at
    the foe, levels set before the fight), `spell_list.py` (suite caster's header, regenerated `spell_table.h`).

## Numbers

- 93 player spells, all cast by the engine in a turn-based fight (verdict ok).
- Outcome checks (state bit, icon rounds, damage/heal in the cast turn) on the last complete results: 12 flagged
  of 93. Since then fixed + spot-checked: Blindness (r12), Swarm Curse (r3); Halo / Black-Barbed Shield icons are
  fixed in code (unit test), not yet confirmed in game. 8 listed as skips with reasons (`kOutcomeSkips`).
- Buff/debuff decay verified in game turn by turn: 5 spells (`tbc_decay.lua`).

## How to work (the user's rule)

Go **spell by spell** and check each one closely; do not batch-run all spells (full run only when asked).
For each spell (and each special ability):
1. Run it alone: `tools\spell_suite.ps1 -Only RES` (plus `-Saves pass` for the save branch), and a `tbc_decay.lua`-style
   turn-by-turn run when it has a duration.
2. Check it against its row in `docs/SPELL_TEST_PLAN.md` section 3 and its description (`python -I tools/ieres.py spl RES`,
   the engine's ApplyEffect via `re.py`): exact stat/state changes, damage within the dice at the caster's level, icon
   and rounds, starts in the cast turn, ends at the right owner turn start, the turn does not stall or wait too long,
   disabled targets lose their turns, protections block what they should.
3. Fix what is wrong (engine rule first, never a parallel rules engine).
4. Write a **unit test for that spell** with the exact values seen (a row in a new `tests/unit/spell_expect.h`:
   res, stat deltas, state bits, icon rounds, HP bounds, rounds until gone, notes; compared in `test_spells.cpp` with
   `spell_results.h`). `-Only` results must merge into `spell_results.h` (step 3 below) so each spot run refreshes it.
5. Mark the spell done in the plan table (a "checked" column) and commit.

## Next, in order

1. **Bug: the game hangs/exits in the full run after SPWI217** (next spell SPWI218 area with op25 poison onsets):
   the log fills with `tbc: enemy X: new persistent effect (0 -> 1): first pulse now` thousands of times. The new
   "first pulse" code in `tbc_module.cpp` (per-frame `persist_seen` loop) re-pulses every frame when a persistent
   effect appears and disappears (count 0 -> 1 -> 0). Fix: pulse each new persistent effect once (identity, not
   count), at most once per creature per frame/turn; then spot-check `-Only SPWI217,SPWI218,SPPR303,SPPR104`.
2. Spot-check the fixes not yet seen in game: `-Only SPPR105,SPWI203,SPWI117,SPWI212,SPWI115` (icons for Halo and
   Black-Barbed Shield; Seeking Flames showed 48 rounds for 30 s before the orphan fix).
3. `spell_results.h` is only rewritten by a full run, so `test_spells.cpp` outcome cases fail on stale rows. Make
   `tools/spell_results.py` merge `-Only` results into the existing header (keep other rows), so spot checks
   refresh their spells; then commit with check.ps1 green.
4. Then the per-spell pass above, from SPPR101 through SPWI914 and SPIN101-108; none has had it yet (the 5 in
   `tbc_decay.lua` have their timing checked, not every value). Known open ones on the way
   (docs/SPELL_TEST_PLAN.md section 4): Tasha's Laughter (laugh not visible, penalties after the turn),
   Celestial Host and Conflagration (nothing on the suite's foe), Howl of Pandemonium (stun without icon).
5. Extend `tbc_decay.lua` to an attached-object spell that ends within a few rounds and to a damage-over-time
   (Adder's Kiss / Conflagration), and add special abilities SPIN101-108 to `spell_list.py` and the suite.
6. Full run only when the user asks.

Test facts learned: `ChangeStat` is a queued action (set levels before the fight: a party turn start clears it);
`NearestEnemyOf` picks any thug; the rigged foe's saves cap at 30 (Curse is measured on HITBONUS, stat 151);
Bless/Curse in PST:EE change HITBONUS (151), not THAC0; a Lua C function may push only 20 values.
