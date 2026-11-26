# Spell and ability test plan

Goal: every player spell and special ability does what the game says it does, and does it the right way in
turn-based combat (tbc): it costs the caster's action, it lands in the caster's turn (or on the round clock when
it is a damage-over-time step), its buff or debuff shows as a status icon with the right rounds, it ends on the
owner's turn start the right number of rounds later, and a disabled creature really loses its turns.

Today `test_spells.cpp` only proves each spell **is cast** (engine "casts" feedback, turn closes). Nothing yet
proves **what it did**. This plan adds that, spell by spell.

Sources: `tests/unit/spell_table.h` (header 0 only), `python -I tools/ieres.py spl RES` (every header), the SPL
descriptions in dialog.tlk, `docs/TBC_RESEARCH.md` §10-11, `src/modules/tbc/round_rules.h`, `status.h`.
Scope: 93 spells (75 mage SPWI, 18 priest SPPR) + 6 named special abilities (SPIN101-104, 107, 108) + SPIN106
(unnamed Horror, a creature ability) + SPIN001-015 (script plumbing: give/remove an ability, hiccups, curse).

## 1. What "works" means in turn-based combat

Effect time runs on the round clock: **1 round = 75 ticks = 5 game seconds** of effect time, stepped at the
owner's turn start (`ShiftEffectClock`). So a 30 s buff = 6 rounds, 10 s per level at level 14 = 28 rounds.

| Class | Rule (what must be true) | Where it lives |
|---|---|---|
| **INSTANT** damage / heal / death (op 12, 13, 17, 383) | lands inside the caster's turn; HP change within the dice bounds (half on a 1/2 save); the turn waits for the projectile | projectile wait, `OnsetResolvesNow` |
| **ONSET** delayed hit < 112 ticks, not on a 75 step (Cone of Cold 100, Black-Barbed Curse 35, Vampiric Touch 60) | resolves now, in the caster's turn | `OnsetKind` = 1 |
| **DOT** delayed hit on a 75-tick step (Adder's Kiss series, Desert Hell?) | one step per round, at the **victim's** turn start | `OnsetKind` = 0 |
| **SYNC** delayed hit timed with its own visuals (Stygian Ice Storm 75) | resolves with the visuals, now | `SyncSpellOnsets` |
| **PRES** delayed glow / sound / BAM (9, 50-52, 61, 140, 141, 174, 215, 369-373) | world time, nobody waits | `IsPresentationOpcode` |
| **BUFF / DEBUFF** timed stat (0, 1, 6, 22, 33-37, 54, 73, 129, 130, 278, 301, 378, 379) | exact stat change; icon (buff, or debuff = hostile flag first); rounds = ceil(left / round); gone at the owner's turn start when the rounds run out | `CollectStatus` |
| **DISABLE** state (24 horror, 39 sleep, 45 stun, 74 blind, 109 paralyse, 128 confusion, 185 hold, 356 stop actions, 358 berserk) | save rolled (CheckSave line); on a fail the creature's turns are lost (sleep, paralyse, hold, stun, stop actions) or AI-driven (horror = flee, confusion = random, berserk = attack nearest); icon debuff; on its end the creature acts again | director + engine AI |
| **ATTACHED** op 375 spell objects (Armor, Shield, Submerge, Pain Mirror, Cloaks, Mantles...) | ends on its own world stamp (+0x3FC), shifted by the round clock; effects are parentless: icon via `AttributeOrphans`; "until destroyed" kinds keep an icon with no rounds | `ShiftEffectClock`, status.h |
| **HARDCODED** PST projectile with no SPL effects (Strength, Blood Bridge, Shroud, Acid Storm, Blade Storm, Ignus' Fury, PW Blind, Fire and Ice, Abyssal Fury, Skull Mob, Spiritual Hammer, Swarm Curse, clouds) | effects come from engine code: only the outcome can be checked (HP, stats, state), not the table | in-game only |
| **LINGER** area / swarm that pulses (Swarm Curse, Acid Storm, Conflagration, Pacify, Tasha's) | one pulse per caster round (`PulseBudget`) | `SetProjectileGate` |
| **PROTECT** immunity (102 Globe, 324, 89, Antimagic Shell, Guardian Mantle) | a spell or attack aimed at the creature is refused while it lasts, works after | engine |
| **OOC** utility (Identify, Knock, Remove Curse, Detect Evil, Speak with Dead, Stories-Bones-Tell, Raise Dead, Embalming, Friends) | out of a fight: the outcome; in a fight: costs the action, nothing stalls | engine |

## 2. Test layers (cheapest first) and what each can catch

| Layer | Viable for | How |
|---|---|---|
| **U1 table rules** (L0, no game) | every spell with SPL effects (89 of 93 + 7 abilities) | `spell_table.h` + a new expectations table `tests/unit/spell_expect.h` (below): class, icon yes/no, rounds at the suite's caster level, HP bounds, onset kind of each delayed effect. Pure: `OnsetKind`, `CollectStatus`, dice bounds. Catches rule regressions and mod changes. |
| **U2 suite results** (L0, from the game) | every spell | `spell_results.h` grows columns the suite measures (HP delta, stat deltas, state bits, icon + rounds shown, rounds until gone, CheckSave seen, damage turn = caster's or victim's). The unit test compares them with `spell_expect.h`. One 17-min run refreshes all of them. |
| **S static** | only hooks (already covered by `pstf_verify`) | nothing per spell |
| **G in-game focused** | spells U2 cannot judge from numbers: AI-driven disables, protections, summons/duplicates, OOC outcomes, cursor spells | a small Lua test each (like `tbc_ice`, `tbc_mop`) |
| **Manual / visual** | glow, screen tint, camera moves (347), BAM plays | `lighting_spellfx.lua` sheets; no assertion beyond "drawn" |

Not unit-testable at all: hardcoded projectiles' amounts (engine code), AI choices under confusion/horror/
berserk (random), Missile of Patience's cursor. Those are G tests with tolerant assertions (bounds, "did not act
on its own turn", "moved away").

### 2.1 Suite changes needed (one-time, then every spell gets checked)

1. `tools/spell_list.py`: also write the **header the suite's caster uses** (TNO 20, Grace 14), not only header 0:
   level-scaled spells (Aid, Magic Missile, Luck, Hold Undead, Deathbolt...) differ per header.
2. `tbc_spells_all.lua`: before/after each cast record target and caster HP, `PSTF_Stats` (AC, THAC0, saves,
   STR, CHR, luck, APR, crit), STATE bits, `PSTF_Status` icons (resref, rounds, debuff), CheckSave lines seen;
   then **End Turn until the icon is gone** (cap 40 rounds) and record the round it went and whether the stats
   came back. For disables: record whether the target acted on its next turn (`PSTF_TbcCurrent` used/acting).
3. `tools/spell_results.py`: write those columns into `spell_results.h`.
4. `tests/unit/spell_expect.h` (hand-written, one row per spell, values from the table + descriptions below) and
   new `test_spells.cpp` cases comparing U2 results with it.
5. A target dummy that never saves (set its saves to 20 via `ChangeStat`) and a second run where it always saves
   (saves 1), so both branches are measured; `PSTF_RollProbe` / `PSTF_RollForce` where a roll must be forced.

## 3. Per spell

Columns: **Intended** = what the engine/description does (at the suite caster's level). **TBC** = class(es) from
§1 and the expected turn behaviour. **Icon** = status icon expected (B buff, D debuff, - none) and rounds.
**Check** = what the suite asserts. **Layer** = U1/U2/G (all get U2 unless "G"). **Vetted** = checked one by one by
hand against the SPL, the description and the engine's ApplyEffect, in game (values, cast turn, decay, save branch);
its row in `tests/unit/spell_expect.h` holds what was seen.

### Priest

| Res | Spell | Intended | TBC | Icon | Check | Layer | Vetted |
|---|---|---|---|---|---|---|---|
| SPPR101 | Blessing | allies in 70 ft: +1 hit, +1 saves, 30 s | BUFF | B 6 | THAC0 -1, saves +1 on every ally in radius, not on foes; gone after 6 rounds | U1 U2 | yes 2026-11-25 |
| SPPR102 | Cure Light Wounds | heal 8 | INSTANT | - | HP +8 (capped at max) | U1 U2 | yes 2026-11-25 |
| SPPR103 | Curse | foes in 50 ft: -1 hit, -1 saves, 30 s, non-cumulative | DEBUFF | D 6 | THAC0 +1, saves -1 on foes only; a 2nd cast does not stack | U1 U2 | yes 2026-11-25 |
| SPPR104 | Detect Evil | reveals evil creatures (op 376) for a time | OOC | B? | engine highlight on; does not stall the turn | U2 G | yes 2026-11-25 |
| SPPR105 | Halo of Lesser Revelation | foes in area outlined, easier to hit (hardcoded projectile 259) | HARDCODED DEBUFF | D (20 s/lvl) | foes' AC worse by the engine's value; icon via the projectile's effects | U2 G | yes 2026-11-25 |
| SPPR106 | Protection from Evil | +2 AC/saves vs evil, 10 s/lvl (op 375 PROT) | ATTACHED | B | AC/saves change; ends on its stamp shifted by rounds | U2 | yes 2026-11-25 |
| SPPR201 | Aid | +1 hit, +1 saves, + HP (only if not already fighting), 10 s + 10 s/lvl | BUFF | B | THAC0/saves/max HP; check the "not in combat" condition in a tbc fight (is a tbc fight "in combat"?) | U1 U2 | yes 2026-11-25 |
| SPPR202 | Cure Moderate Wounds | heal 11 | INSTANT | - | HP +11 | U1 U2 | yes 2026-11-25 |
| SPPR204 | Spiritual Hammer | hammer hits 4-10 per 3 levels, save 1/2 (projectile 260) | HARDCODED, INSTANT | - | HP loss within bounds; lands in the caster's turn (long: 200 ticks today) | U2 | yes 2026-11-25 |
| SPPR301 | Call Lightning | 2d8 + 1d8/lvl electric, save 1/2, 10 ft | INSTANT | - | HP loss within 27d8 bounds (half on save) | U1 U2 | yes 2026-11-25 |
| SPPR302 | Flamewalk | +2 saves vs fire, fire resistance (op 375 FTRL) | ATTACHED | B | fire resistance stat; Ignus' fire deals less | U2 | yes 2026-11-25 |
| SPPR303 | Prayer | allies +1 hit/dmg/saves, foes -1, 5 s/lvl, 60 ft | BUFF+DEBUFF | B on allies, D on foes | both sides change, opposite signs | U1 U2 | yes 2026-11-25 |
| SPPR304 | Speak with Dead | talk to a corpse (projectile 205) | OOC | - | dialogue opens on a corpse | G |  |
| SPPR307 | Remove Curse | lets cursed items be removed | OOC | - | cursed item unequips after | G |  |
| SPPR401 | Cure Serious Wounds | heal 17 | INSTANT | - | HP +17 | U1 U2 | yes 2026-11-25 |
| SPPR501 | Cure Critical Wounds | heal 27 | INSTANT | - | HP +27 | U1 U2 | yes 2026-11-25 |
| SPPR502 | Raise Dead | revive a dead ally, HP set 50 % | INSTANT | - | dead ally alive; joins the fight order at its next round | G |  |
| SPPR601 | Heal | full HP, cure blind and disease | INSTANT | - | HP = max; blindness (Blindness first) gone; its icon gone | U2 G | yes 2026-11-25 |

### Mage, levels 1-3

| Res | Spell | Intended | TBC | Icon | Check | Layer | Vetted |
|---|---|---|---|---|---|---|---|
| SPWI101 | Chromatic Orb | colour by level: damage 4d10 + paralyse 10-40 s etc., save neg | INSTANT + DISABLE | D | at the suite's level: HP and paralyse; paralysed target loses its turns for N rounds | U1 U2 | yes 2026-11-25 |
| SPWI102 | Armor | AC 6 until destroyed by damage (op 375) | ATTACHED | B (no rounds) | AC; still there after rounds pass; gone after enough damage | U2 G | yes 2026-11-25 |
| SPWI103 | Minor Embalming | TNO only: AC/HP bonus, 30 min (op 380) | BUFF | B (360 rounds) | TNO stats change; refused on others | U1 U2 | yes 2026-11-25 |
| SPWI104 | Fist of Iron | TNO fists hit harder, 5 s + 5 s/lvl; 100 % spell failure while on | BUFF | B | punch damage up; **op 60: later casts fail** (the suite already removes it) | U1 U2 | yes 2026-11-25 |
| SPWI105 | Identify | identify an item | OOC | - | item identified; no-cast-time cast spends the action | G (exists) | yes 2026-11-25 |
| SPWI107 | Magic Missile | 1-5 missiles 1d4+1 | INSTANT | - | HP loss = n x (2..5); all missiles land in the turn | U1 U2 | yes 2026-11-25 |
| SPWI108 | Pacify | 2-8 non-undead stop fighting (paralyse + sleep-like), 40 s + 5 s/lvl; broken by damage (SPWI108A) | DISABLE, LINGER | D | targets lose their turns; damage ends it (they act next turn) | U2 G |  |
| SPWI111 | Shield | AC 3 vs most, MM immunity (op 375) | ATTACHED | B | AC; Magic Missile refused | U2 G | yes 2026-11-25 |
| SPWI113 | Missile of Patience | hovers, fires when the caster clicks (cursor 20), 3 shots | special | - | exists: `tbc_mop.lua` | G (exists) | yes 2026-11-25 |
| SPWI114 | Friends | +2d4 CHR | BUFF | B | CHR +2..8; ends | U1 U2 | yes 2026-11-25 |
| SPWI115 | Blindness | blind 60 s, save neg | DISABLE | D 12 | to-hit / AC penalties of op 74; ends after 12 rounds | U1 U2 | yes 2026-11-25 |
| SPWI116 | Tongues of Flame | fire missiles 1d4+1 each | INSTANT | - | HP bounds | U1 U2 | yes 2026-11-25 |
| SPWI117 | Seeking Flames | foes -1 hit/saves 30 s (op 379) | DEBUFF | D 6 | as Curse | U1 U2 | yes 2026-11-25 |
| SPWI118 | Scripture of Steel | allies +1 hit/saves 30 s (op 130) | BUFF | B 6 | as Blessing | U1 U2 | yes 2026-11-25 |
| SPWI119 | Submerge the Will | AC + saves, 12 s/lvl (op 375, parentless effects) | ATTACHED | B | icon via orphans (exists); ends on time | U2 (exists partly) | yes 2026-11-25 |
| SPWI120 | Vilquar's Eye | blind 30 s, save -1 | DISABLE | D 6 | as Blindness | U1 U2 | yes 2026-11-25 |
| SPWI121 | Reign of Anger | missiles 1d4+2 | INSTANT | - | HP bounds | U1 U2 | yes 2026-11-25 |
| SPWI201 | Adder's Kiss | acid hit, lingers 5 s / 3 lvls (op 374) | INSTANT + DOT | D | first hit in the cast turn, then one per victim round (exists: `tbc_hive`) | U2 (exists) | yes 2026-11-25 |
| SPWI202 | Black-Barbed Curse | barbs 4d4 after 35 ticks | ONSET | - | HP within 4..16, in the caster's turn | U1 U2 | yes 2026-11-25 |
| SPWI203 | Black-Barbed Shield | barbs hurt attackers, 10-30 s (op 375) | ATTACHED | B | melee attacker loses HP | U2 G | yes 2026-11-25 |
| SPWI204 | Blood Bridge | caster gives HP to target (projectile 226) | HARDCODED INSTANT | - | caster HP down, target up, equal | U2 | yes 2026-11-25 |
| SPWI205 | Greater Embalming | TNO stronger embalm, 60 min | BUFF | B | as Minor, bigger | U1 U2 | yes 2026-11-25 |
| SPWI207 | Ice Knife | 2d4 cold + area, needs a hit roll | INSTANT | - | HP bounds; a miss does nothing | U1 U2 | yes 2026-11-25 |
| SPWI209 | Pain Mirror | damage taken reflected in 10 ft (op 375) | ATTACHED | B | hit the caster: nearby foes lose HP | G | yes 2026-11-25 |
| SPWI211 | Strength | STR up (projectile 223) | HARDCODED BUFF | B | STR up; icon via orphans | U2 | yes 2026-11-25 |
| SPWI212 | Swarm Curse | insects 15 s, spell failure | LINGER, DEBUFF | D | one pulse per caster round (exists); target's casts fail meanwhile | U2 (exists) | yes 2026-11-25 |
| SPWI213 | Horror | non-undead in 30 ft flee 30 s, save neg | DISABLE (AI) | D 6 | target runs on its turns, does not attack | U1 U2 G | yes 2026-11-25 |
| SPWI214 | Luck | luck +2..8, 5 s/lvl (op 22, one random row) | BUFF | B | luck +2..8 | U1 U2 | yes 2026-11-25 |
| SPWI215 | Knock | unlock locks in area | OOC | - | `PSTF_Locks` locked -> 0 | G |  |
| SPWI216 | Blur | AC +3, saves +1, 15 s + 5 s/lvl | BUFF | B | exact AC/saves; ends (exists in `tbc_rules`) | U1 U2 | yes 2026-11-25 |
| SPWI217 | Ignus' Terror | as Horror, save -1 | DISABLE (AI) | D 6 | as Horror | U1 U2 G | yes 2026-11-25 |
| SPWI218 | Infernal Orb | 1d8 + 1d4 fire, attack roll +3, slows | INSTANT | - | HP bounds | U1 U2 | yes 2026-11-25 |
| SPWI219 | Power of One | STR up (projectile 287, parentless) | HARDCODED BUFF | B | STR; icon via orphans (exists) | U2 | yes 2026-11-25 |

### Mage, levels 4-6 (spell levels 3-6 in the files)

| Res | Spell | Intended | TBC | Icon | Check | Layer | Vetted |
|---|---|---|---|---|---|---|---|
| SPWI301 | Ball Lightning | up to 4 spheres hit nearest foes, save 1/2 (op 374) | INSTANT | - | HP loss on up to 4 foes, all in the turn | U2 | yes 2026-11-25 |
| SPWI304 | Cloak of Warding | absorbs 3d4 + lvl damage (op 375) | ATTACHED | B | damage absorbed, then gone | G | yes 2026-11-25 |
| SPWI305 | Elysium's Tears | meteor stream at one target | HARDCODED INSTANT | - | HP bounds; turn long (241 = cap: check it is the stream, not a stall) | U2 | yes 2026-11-25 |
| SPWI307 | Hold Undead | 1d3 undead held | DISABLE | D | undead lose turns; living unaffected | G (needs undead: Mortuary save) |  |
| SPWI308 | Tasha's Laughter | foes laugh (stop actions 45 ticks), then -2 AC/dmg/hit | DISABLE, LINGER | D | targets lose their next turn, then penalties | U1 U2 | yes 2026-11-25 |
| SPWI309 | Vampiric Touch | drain 1d6 / 2 lvls to caster | ONSET | - | target down, caster up, same amount, in turn | U1 U2 | yes 2026-11-25 |
| SPWI310 | Ax of Torment | ax, 1d8 + growing, save neg | HARDCODED INSTANT | - | HP; 241 = cap: check its repeated swings follow the caster's rounds | U2 G | yes 2026-11-25 |
| SPWI311 | Zerthimon's Focus | crit chance +3 | BUFF | B | crit stat +3 | U1 U2 | yes 2026-11-25 |
| SPWI312 | Infernal Shield | fire heals instead (op 375) | ATTACHED | B | Ignus' fire heals the caster | G | yes 2026-11-25 |
| SPWI313 | Fiery Rain | fire meteors 2d4 each, area | INSTANT | - | HP bounds | U1 U2 | yes 2026-11-25 |
| SPWI314 | Balance in All Things | reflect after N hits (op 375) | ATTACHED | B | as Pain Mirror | G | yes 2026-11-25 |
| SPWI401 | Blacksphere | force burst damages + imprisons (sleep op 39) | INSTANT + DISABLE | D | HP + targets lose turns | U2 | yes 2026-11-25 |
| SPWI402 | Confusion | 1-4 + lvl creatures confused 10 s + 1 s/lvl | DISABLE (AI) | D | targets do not act normally; icon ends | U1 U2 G | yes 2026-11-25 |
| SPWI405 | Force Missiles | globes 2d4 each, unerring | INSTANT | - | HP bounds | U1 U2 | yes 2026-11-25 |
| SPWI406 | Improved Strength | STR to 18/xx etc. (projectile 228) | HARDCODED BUFF | B | STR | U2 | yes 2026-11-25 |
| SPWI408 | Shroud of Shadows | hidden + AC (projectile 229) | HARDCODED BUFF | B | AC; foes lose sight? | U2 G | yes 2026-11-25 |
| SPWI409 | Remove Curse | as SPPR307 | OOC | - | as SPPR307 | G | yes 2026-11-25 |
| SPWI410 | Elemental Strike | globes 2d4 fire + blast | INSTANT | - | HP bounds | U1 U2 | yes 2026-11-25 |
| SPWI501 | Cloudkill | kills low HD (by save/dice), else 1d10 poison; foes only | INSTANT | - | low-HD thugs die, high-HD take damage; party unhurt | U1 U2 | yes 2026-11-25 |
| SPWI502 | Cone of Cold | 10d4 + 10 cold after 100 ticks + brief paralyse | ONSET | - | HP in the caster's turn (exists) | U1 U2 | yes 2026-11-25 |
| SPWI504 | Enoll Eva's Duplication | 2x attacks per round, 60 s (op 1, 357, 375) | BUFF | B 12 | APR doubled; **check tbc swings** (`PSTF_TbcAttacks` 2x) | U1 U2 G | yes 2026-11-25 |
| SPWI505 | Desert Hell | heat 1d10 x 4 hits at 30/60/90/120 ticks + stun | ONSET (+ DOT?) | - | see finding F1 | U1 U2 | yes 2026-11-25 |
| SPWI506 | Fire and Ice | two spheres, fire then cold, 30 ft | HARDCODED INSTANT | - | HP bounds; both land in the turn | U2 | yes 2026-11-25 |
| SPWI601 | Antimagic Shell | caster immune to magic, 5 min (op 375) | ATTACHED, PROTECT | B 60 | a spell at the caster refused; the caster can't cast either | G | yes 2026-11-25 |
| SPWI602 | Globe of Invulnerability | immune to spell levels 1-4 (3 s re-applied while inside) | PROTECT | B | level-4 spell refused, level-5 lands; one steady icon (exists: quarter-round rule) | U2 G | yes 2026-11-25 |
| SPWI603 | Howl of Pandemonium | stun foes / deafen | HARDCODED DISABLE | D | targets lose turns | U2 | yes 2026-11-25 |
| SPWI604 | Chain Lightning Storm | 7d10 electric chained, save 1/2 | INSTANT | - | HP bounds | U1 U2 | yes 2026-11-25 |

### Mage, levels 7-9

| Res | Spell | Intended | TBC | Icon | Check | Layer | Vetted |
|---|---|---|---|---|---|---|---|
| SPWI701 | Acid Storm | 1d6/lvl acid then lingering 30 s | HARDCODED LINGER | D? | one pulse per caster round | U2 | yes 2026-11-25 |
| SPWI702 | Blade Storm | ice shards on all foes in area, save 1/2 | HARDCODED INSTANT | - | HP on every foe in range | U2 | yes 2026-11-25 |
| SPWI704 | Guardian Mantle | deflects all physical attacks 50 s + 5 s/lvl (op 375) | ATTACHED, PROTECT | B | attacks miss/blocked; ends | G | yes 2026-11-25 |
| SPWI705 | Stygian Ice Storm | 8d8 cold at 75 ticks + paralyse | SYNC | - | HP in the caster's turn (exists `tbc_ice`) | U2 (exists) | yes 2026-11-25 |
| SPWI802 | Meteor Storm Bombardment | 10d12 to all foes, save 1/2 | INSTANT | - | HP bounds on every foe | U1 U2 | yes 2026-11-25 |
| SPWI803 | Deathbolt | death (save) else 10d6 | ONSET | - | dead, or HP within bounds | U1 U2 | yes 2026-11-25 |
| SPWI804 | Ignus' Fury | 10-100 + lvl fire per cube | HARDCODED INSTANT | - | HP on foes; turn length | U2 | yes 2026-11-25 |
| SPWI805 | Power Word, Blind | blind up to 100 HP of creatures | HARDCODED DISABLE | D | blindness on targets by HP order | U2 | yes 2026-11-25 |
| SPWI807 | Mechanus Cannon | 20d4 + 10 energy | INSTANT | - | HP bounds | U1 U2 | yes 2026-11-25 |
| SPWI901 | Celestial Host | 40d3 to foes | INSTANT | - | HP bounds | U1 U2 | yes 2026-11-25 |
| SPWI902 | Conflagration | burns 2d6 per 5 s, 50 s, spreads to 10 ft (op 375) | ATTACHED, DOT | D 10 | one burn per victim round, not per world second | U2 G | yes 2026-11-25 |
| SPWI905 | Elysium's Fires | 5d4 fire bursts in area | INSTANT | - | HP bounds | U1 U2 | yes 2026-11-25 |
| SPWI909 | Power Word, Kill | kills up to 120 HP | ONSET | - | target <= 120 HP dies, > 120 not | U1 U2 | yes 2026-11-25 |
| SPWI912 | Abyssal Fury | ground swallows the target (projectile 234) | HARDCODED INSTANT | - | HP / death outcome | U2 | yes 2026-11-25 |
| SPWI914 | Rune of Torment | 14d6 + 16 to all in area, long cast (speed 9) | INSTANT | - | HP bounds; long cast not cut (exists) | U1 U2 | yes 2026-11-25 |

### Special abilities

| Res | Ability (who) | Intended | TBC | Icon | Check | Layer |
|---|---|---|---|---|---|---|---|
| SPIN101 | Litany of Curses (Morte) | target berserks at Morte: -2 AC/hit/dmg, 10 s/lvl, save neg; 2 s stun | DISABLE (AI) + DEBUFF | D | stats change; target attacks Morte on its turns | U1 U2 G |  |
| SPIN102 | Skull Mob (Morte) | skulls bite, no save, no MR (projectile 216) | HARDCODED INSTANT | - | HP loss in Morte's turn | U2 |  |
| SPIN103 | Stories-Bones-Tell (TNO) | talk to undead/corpses (EFF SPIN103/103B, 300-tick tail) | OOC | - | dialogue opens; the delayed EFF does not hold a fight turn | G |  |
| SPIN104 | Sensory Touch (Grace) | takes 1d10 of the target's wounds onto Grace | ONSET | - | target +n, Grace -n | U1 U2 |  |
| SPIN106 | Horror (creature) | fear 50 s, save neg | DISABLE (AI) | D 10 | as Horror; from an enemy caster | U1 G |  |
| SPIN107 | Kiss (Grace) | drains 2d4 to Grace, ignores weapon immunity | ONSET | - | target -n, Grace +n | U1 U2 |  |
| SPIN108 | Raise Dead (TNO) | revive, 3/day | INSTANT | - | as SPPR502; uses left drop | G |  |
| SPIN001-015 | script plumbing | give/remove the abilities above, hiccups (381), uncurse | none | - | U1 only: give/remove pairs match (001/002 -> SPIN101 ...) | U1 |

Special abilities are not in `spell_list.py` today: add SPIN1xx with a name (and SPIN106) to the table and to the
suite's caster list (Morte, Grace, TNO need the ability: `AddSpell` / the SPIN00x give spells).

## 4. Findings to settle (found while building this plan)

- **F1 Desert Hell's 4th hit (fixed: `SyncSpellOnsets` chains off-step hits of a series resolving now; test_spells.cpp).** Its heat hits are delayed 30/60/90/120 ticks. `OnsetResolvesNow`: 30, 60, 90 run
  now; 120 (>= 112) is round-clocked to the victim's next turn, and `SyncSpellOnsets` does not pull it in (30 ticks
  from its sibling > 12). So 3 hits land now and 1 a round later. The description says Instant: probably all four
  should land now. U1 can assert this today from the table.
- **F2 Icon rounds vs the round clock (fixed: icons count `effect_round_ticks`; test_spells.cpp).** `tbc_module.cpp:1544` passes `RoundLengthTicks()` (51) to
  `CollectStatus`, but effect time steps 75 ticks per turn. A 30 s Blessing (450 ticks) shows 9 rounds and really
  lasts 6. Decide: icons should count 75-tick rounds. U1 catches it once the expected rounds are in the table.
- **F3 Disabled creatures' turns (fixed: `LosesTurn`, round_rules.h: sleeping/stunned/helpless/frozen/stone end the turn at its start).** No code in `tbc/` checks sleep/paralyse/hold on turn start; the turn ends by
  the idle timeout (15-20 idle ticks). Works, but slow and not asserted: add a direct "helpless: skip" (STATE
  bits, as `maneuvers.h` does for reactors) and the U2 column "acted on its next turn".
- **F4 Aid's "not engaged in combat" condition** in a tbc fight: measure whether it ever applies in one.
- **F5 Header 0 only (fixed: spell_list.py takes the suite caster's header).** `spell_table.h` stores header 0; most damage/duration values scale with level. The
  expectations must use the header the suite caster gets (2.1 step 1).
- **F6 Long turns at the 241-tick cap** (Elysium's Tears, Ax, Cloudkill, Desert Hell, Antimagic, ...): today
  they pass as "ok". U2 should tell "waited for its projectile" from "hit the cap" (log the wait reason).

- **F7 Persistent effects stepped once per turn (fixed).** Their period counters count calls; the turn-start step made
  one call with 75 ticks: Prayer pulsed 75x too slowly (once, a round late). Now one call per tick, and a new
  persistent effect pulses in its cast turn.
- **F8 Onset kinds stuck to reused memory (fixed: `OnsetBook`).** Delayed effects were classified by address and never
  forgotten in a fight: a new effect at a freed effect's address got the old kind (Submerge the Will's shield
  round-clocked to the next turn, so the suite saw it "sometimes").
- **F9 Icon frame refilled between turns (fixed).** The span used "now + left" as the end; the round clock freezes
  `left` while the world clock runs, so every frame looked like a recast. Spans now follow `left` only.
- **F10 Icons for engine-built effects (fixed: Halo).** An orphan effect takes the spell whose parented effect arrived
  with it, not only a delayed one.
- **F11 Glow outlasting a buff (fixed).** Presentation opcodes (glow, tint, camera, animation) never count toward an
  icon's rounds.
- **F12 Test harness facts.** `ChangeStat` is a queued action: set levels before the fight (a party turn start clears
  the queue; the suite had Grace at 21, not 14). `NearestEnemyOf` picks any thug: the suite measures the enemy the
  spell changed. Saves cap at the rigged 30 (use HITBONUS for Curse).
- **Open: Tasha's Laughter** (the laugh itself shows no state; penalties land ~160 ticks later, after the turn), **Seeking
  Flames** icon 48 rounds for a 30 s spell, **Stygian Ice Storm** adds confusion (projectile).

## 5. Order of work

1. ~~F1 and F2 as U1 tests, then the fixes.~~ Done.
2. Suite measurement columns + `spell_expect.h` + compare tests (2.1): covers ~70 spells at once (INSTANT, BUFF,
   DEBUFF, simple DISABLE).
3. Special abilities into the table and suite.
4. F3 (helpless skip) with its U2 column.
5. G tests, cheapest first: protections (Shield vs MM, Globe, Antimagic, Guardian Mantle), reflect/absorb
   (Pain Mirror, Balance, Cloak of Warding, Black-Barbed Shield, Infernal Shield), AI disables (Horror,
   Confusion, Litany), Enoll Eva (APR in tbc), Conflagration DOT, OOC (Knock, Remove Curse, Raise Dead,
   Speak with Dead / Stories-Bones-Tell), Hold Undead (needs an undead save).
6. Item abilities (charms, tattoos, Whispering Flask...) reuse the same machinery: a follow-up list from
   `ieres.py itm` of every usable item with op 146 / its own effects.
