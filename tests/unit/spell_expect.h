// SPDX-License-Identifier: GPL-2.0-or-later
// Spells checked one by one by hand (docs/SPELL_TEST_PLAN.md section 3, column "Vetted"): what each one does in a
// turn-based fight, as measured in game and compared with its SPL, its description and the engine's ApplyEffect.
// test_spells.cpp compares every row with the suite's latest measurement (spell_results.h, foe fails its saves).
// Add a row only after checking the spell yourself; the note says what was checked and where.
#pragma once

namespace pstf::spellexpect {
struct Expect {
  const char* res;
  const char* stats;  // exact derived stat changes on the measured creature, as spell_results.h writes them; "*" random
  unsigned state;     // STATE bits set (~0U: random, not compared)
  const char* icon;   // status icon at the end of the cast turn ("rN:buff", "rN:debuff", "none"; "r*:..." any rounds)
  int hp_min, hp_max; // HP change of the measured creature in the cast turn (heals +, damage -)
  int rounds;         // owner turn starts until it is gone (0: instant / not timed)
  const char* note;
};

inline constexpr Expect kExpect[] = {
    {"SPPR101", "9:-1,10:-1,11:-1,12:-1,13:-1,50:+1,151:+1", 0x800000U, "r6:buff", 0, 0, 6,
     "Blessing (op 130, 30 s): +1 hit, +1 all saves; +1 damage is the engine's own "
     "(CGameEffectNon_CumulativeBless); not on the foe; gone at the 7th owner turn start (tbc_decay.lua)"},
    {"SPPR102", "-", 0x0U, "none", 8, 8, 0, "Cure Light Wounds: op 17 +8 in the cast turn, nothing left over"},
    {"SPPR103", "50:-1,151:-1", 0x0U, "r6:debuff", 0, 0, 6,
     "Curse (op 379, 30 s): -1 hit/damage (save -1 sits at the rigged foe's cap); not on the caster; gone at the "
     "7th owner turn start (tbc_decay.lua)"},
    {"SPPR202", "-", 0x0U, "none", 11, 11, 0, "Cure Moderate Wounds: op 17 +11 in the cast turn"},
    {"SPPR401", "-", 0x0U, "none", 17, 17, 0,
     "Cure Serious Wounds: op 17 +17 in the cast turn (an r2 icon seen in a full run was the previous spell's)"},
    {"SPPR104", "-", 0x0U, "r60:buff", 0, 0, 60,
     "Detect Evil (op 376): 50 s + 25 s/level capped at 5 min by the engine (r60 at 14); the turn ends at once (45 "
     "ticks). The evil highlight itself was not looked at"},
    {"SPPR501", "-", 0x0U, "none", 27, 27, 0, "Cure Critical Wounds: op 17 +27 in the cast turn (speed 8: 134 ticks)"},
    {"SPPR601", "-", 0x0U, "none", 66, 66, 0,
     "Heal: op 17 to full HP (Grace 30 -> 96) in the cast turn; its blindness/disease cures (op 75/79, instant) were "
     "not exercised"},
    {"SPPR105", "2:+2", 0x0U, "r56:debuff", 0, 0, 56,
     "Halo of Lesser Revelation (projectile 259): hostiles AC +2, no save, 20 s/level (4200 ticks at 14); the caster "
     "untouched; icon via the spell being cast (CurrentSpell read the CResHelper's inline resref, fixed 2026-11-25)"},
    {"SPPR106", "2:-2,9:-1,10:-2,11:-2,12:-2,13:-2", 0x0U, "r28:buff", 0, 0, 28,
     "Protection from Evil (op 375 PROT): the engine's AC -2 and saves +2 (not only vs evil, vanilla), 10 s/level; "
     "save vs death 1 -> 0 floors at 0"},
    {"SPPR201", "1:+8,7:-1,9:-1,10:-1,11:-1,12:-1,13:-1,50:+1", 0x8000000U, "r30:buff", 8, 8, 30,
     "Aid (op 129): +1 hit/damage/saves, +8 max HP and HP (CGameEffectNon_CumulativeAid: 8 x param, not 1-8 as the "
     "text says; vanilla); 10 s + 10 s/level"},
    {"SPPR204", "-", 0x0U, "none", -65, -20, 0,
     "Spiritual Hammer (projectile 260): (level + 2) / 3 hammers (5 at 14), each its own to-hit roll and 4-10 "
     "(+enchantment), save 1/2 (52-54 failed, 24 saved); the turn waits for the last one (~200 ticks)"},
    {"SPPR301", "-", 0x0U, "none", -128, -16, 0,
     "Call Lightning: 16d8 at level 14 (2d8 + 1d8/level), 10 ft, save 1/2 (76-87 failed, 38 saved)"},
    {"SPPR302", "-", 0x0U, "r30:buff", 0, 0, 30,
     "Flamewalk (op 375 FTRL): +50% fire resistance and op 84 for 10 s + 10 s/level (Morte 0 -> 50, spell_follow); "
     "the suite's Grace already has 100 so the engine adds none (it skips when > 49)"},
    {"SPPR303", "9:-1,10:-1,11:-1,12:-1,13:-1,50:+1,151:+1", 0x800000U, "r14:buff", 0, 0, 14,
     "Prayer (op 378): friends +1 hit/damage/saves, foes -1 hit/damage every round for 5 s/level (14 rounds at 14, "
     "spell_follow on the foe); the aura pulses once per owner round (StepEffectClock's extra 0-tick call)"},
    {"SPWI101", "-", 0x20U, "r*:debuff", -40, -4, 0,
     "Chromatic Orb at level 20 (black, header 8): 4d10 and paralysis 10/20/30/40 s (one of four rows), save negates; "
     "the paralysed foe loses its turns (log: cannot act: turn lost)"},
    {"SPWI102", "-", 0x0U, "r0:buff", 0, 0, 0,
     "Armor (op 375 type 7): base AC 6 until destroyed (no change on the Nameless One, AC 2 already); the icon has no "
     "countdown (its effects run ~INT_MAX ticks: was r-28633114, fixed 2026-11-25)"},
    {"SPWI103", "1:+8,2:-1", 0x0U, "r30:buff", 2, 8, 30,
     "Minor Embalming (op 380): 2d4 HP through UtilRandInt with the target's luck (the lucky Nameless One rolls 8), "
     "AC +1, 150 s (the SPL's, not the text's half hour)"},
    {"SPWI104", "51:+100,52:+100", 0x0U, "r21:buff", 0, 0, 21,
     "Fist of Iron (op 382): 5 s + 5 s/level (105 s at 20), 100% spell failure while on; the +3/+6 punch is the "
     "engine's attack code (not measured)"},
    {"SPWI105", "-", 0x0U, "none", 0, 0, 0, "Identify: no casting time, spends the action, the turn goes on (31 ticks)"},
    {"SPWI107", "-", 0x0U, "none", -25, -10, 0,
     "Magic Missile: 5 missiles at level >= 9, each 1d4+1, no save (16-20 measured)"},
    {"SPWI115", "2:+4,7:+4", 0x40000U, "r12:debuff", 0, 0, 12,
     "Blindness (op 74, 60 s): THAC0 +4, AC +4, STATE_BLIND (CGameEffectBlindness::ApplyEffect); a save negates it "
     "all (-Saves pass: no effect, no icon); decay as Vilquar's Eye's op 74 (tbc_decay.lua)"},
    {"SPWI111", "2:-3,9:-1,10:-1,11:-1,12:-1,13:-1", 0x0U, "r100:buff", 0, 0, 100,
     "Shield (op 375 type 1): AC -3, saves +1, 25 s/level (500 s at 20)"},
    {"SPWI113", "-", 0x0U, "none", 0, 0, 0,
     "Missile of Patience: hovers for its target (the suite's foe takes nothing); tbc_mop.lua: 3 shots of 2d8 (save), "
     "then gone, an extra click does nothing"},
    {"SPWI114", "*", 0x0U, "r*:buff", 0, 0, 0,
     "Friends (op 6): CHR +2d4 for 5-20 s + 5 s/level (random each cast: 1650-1800 ticks at 20); the icon follows the "
     "CHR, not its sparkle (an attached visual that outlived it: fixed 2026-11-25)"},
    {"SPWI116", "-", 0x0U, "none", -20, -5, 0, "Tongues of Flame: 5 missiles of 1d4 fire at level >= 9, no save"},
    {"SPWI117", "50:-1,151:-1", 0x0U, "r6:debuff", 0, 0, 6,
     "Seeking Flames: Curse's op 379 and foes-only projectile 257, 30 s (save -1 at the rigged foe's cap)"},
    {"SPWI118", "9:-1,10:-1,11:-1,12:-1,13:-1,50:+1,151:+1", 0x800000U, "r6:buff", 0, 0, 6,
     "Scripture of Steel: Blessing's op 130 and projectile 256, 30 s"},
    {"SPWI119", "2:-4,9:-1,10:-1,11:-1,12:-1,13:-1", 0x0U, "r50:buff", 0, 0, 50,
     "Submerge the Will (op 375 type 15): AC -4, saves +1 for 15 x (25 x level / 2) ticks (the engine's 12.5 s/level, "
     "250 s at 20; the text says 12); decay in tbc_decay.lua"},
    {"SPWI120", "2:+4,7:+4", 0x40000U, "r6:debuff", 0, 0, 6,
     "Vilquar's Eye: op 74 blind 30 s, save at -1 negates; decay in tbc_decay.lua"},
    {"SPWI121", "-", 0x0U, "none", -30, -15, 0, "Reign of Anger: 5 missiles of 1d4+2 at level >= 9, no save"},
    {"SPWI201", "-", 0x0U, "none", -8, -2, 7,
     "Adder's Kiss (op 374): 2-8 in the cast turn, then one 2-8 hit at each of the victim's turn starts, "
     "1 + level/3 hits in all (7 at level 20), then none (tbc_decay.lua phase C); no status icon (vanilla)"},
    {"SPWI202", "-", 0x0U, "none", -16, -4, 0,
     "Black-Barbed Curse: 4d4 piercing, its 35-tick onset resolves in the cast turn (9-12 measured)"},
    {"SPWI203", "2:-2", 0x0U, "r*:buff", 0, 0, 0,
     "Black-Barbed Shield (op 375 type 2): AC -2 for 15 x (10..30) ticks (random); each melee hit on the caster "
     "costs the attacker 1d6 on the same tick (CGameSprite::Swing; spell_follow -FoeAttacks Player1 -FoePre THAC0 0: "
     "-2, -6), a takedown hit (no damage) none"},
    {"SPWI204", "-", 0x0U, "none", 0, 0, 0,
     "Blood Bridge (projectile 226, CGameEffectHealTransfer): the caster gives 20 HP at level 20 (87 -> 67, Morte "
     "20 -> 40) in the cast turn; the suite casts it on the caster himself (no change)"},
    {"SPWI205", "1:+40,2:-2", 0x0U, "r60:buff", 40, 40, 60,
     "Greater Embalming (op 380 p2=1): +2 HP/level (40), AC -2, 300 s (the SPL's, not the text's hour)"},
    {"SPWI207", "*", 0x0U, "r12:debuff", -60, 0, 12,
     "Ice Knife (projectile 221, multi-fire): 1 + (level-7)/2 knives at 20 = 5; the caster aims knives 2-5 (target "
     "cursor, CPlanescape::MultiFireSpell) and the turn waits for them; each 2d4 on a hit plus 1d4 cold and -2 to attack "
     "(stacks, vanilla) on a failed save, 60 s (5 knives: -35, THAC0 +10)"},
    {"SPWI209", "-", 0x0U, "r20:buff", 0, 0, 20,
     "Pain Mirror (op 375 type 3): 5 s/level (100 s at 20); the first melee hit on the caster hurts the attacker as "
     "much (-2 / -2 on the same tick, spell_follow -FoeAttacks), then the mirror is gone"},
    {"SPWI211", "*", 0x0U, "r1200:buff", 0, 0, 1200,
     "Strength (projectile 223): STR +1d8 for the fighter class (4 measured; capped below 19), the engine's long "
     "duration (1200 rounds)"},
    {"SPWI212", "51:+100,52:+100", 0x0U, "r3:debuff", -10, -7, 3,
     "Swarm Curse: 1d4 + level/3 per 5 s pulse (7-10 at 20), 15 s, no casting inside (100% failure); one pulse per "
     "caster round (tbc_hive.lua)"},
    {"SPWI213", "-", 0x4U, "r6:debuff", 0, 0, 6,
     "Horror (op 24): panic 30 s on a failed save (STATE_PANIC); the panicked foe flees on its turns"},
    {"SPWI214", "*", 0x0U, "r20:buff", 0, 0, 20,
     "Luck (op 22, one of seven rows): LUCK +2..8 (2 and 7 measured), 5 s/level (100 s at 20)"},
    {"SPWI216", "2:-3,9:-1,10:-1,11:-1,12:-1,13:-1", 0x20000000U, "r23:buff", 0, 0, 23,
     "Blur (op 65 + AC/saves): AC -3, saves +1, STATE_BLUR, 15 s + 5 s/level (115 s); decay in tbc_rules.lua"},
    {"SPWI217", "-", 0x4U, "r6:debuff", 0, 0, 6, "Ignus' Terror (op 24): panic 30 s, save at -1 negates"},
    {"SPWI218", "-", 0x0U, "none", -12, 0, 0,
     "Infernal Orb (projectile 279): the caster's attack roll, then 1d8 + 1d4 fire (3-4 measured); the text's "
     "STR/DEX -1 is not in the SPL (vanilla)"},
    {"SPWI219", "*", 0x0U, "r600:buff", 0, 0, 600,
     "Power of One (projectile 287): STR +1d8+1 for the fighter class (2-5 measured), the engine's duration "
     "(600 rounds); its effects are parentless (named by the spell on its way)"},
    {"SPWI301", "-", 0x0U, "none", -32, -8, 0, "Ball Lightning (op 374): its balls strike in the cast turn (19-26 on one foe)"},
    {"SPWI304", "-", 0x0U, "r20:buff", 0, 0, 20,
     "Cloak of Warding (op 375 type 0): absorbs 3d4 + level damage (two hits taken at 87 HP, spell_follow), 100 s"},
    {"SPWI305", "-", 0x0U, "none", -60, -3, 0,
     "Elysium's Tears: one meteor per level (10) of 1d2 + 1d4 around the target; the turn holds until the last lands"},
    {"SPWI308", "2:+2,50:-2,151:-2", 0x0U, "r20:debuff", 0, 0, 20,
     "Tasha's Laughter: AC +2, -2 hit, -2 damage in the cast turn, 5 s/level (1500 ticks), icon r20 down by one per "
     "turn (spell_follow; the late penalties of section 4 are gone)"},
    {"SPWI309", "-", 0x0U, "none", -60, -10, 0,
     "Vampiric Touch (op 383): 1d6 per 2 levels (10d6) drained in the cast turn, the caster gains it (over max = "
     "temporary max HP)"},
    {"SPWI310", "-", ~0U, "none", -1000, -1, 0,
     "Ax of Torment: 1d8 then one random final blow (damage, paralysis, teleport, stone: a stoned foe lost 963); "
     "the turn holds for the ax"},
    {"SPWI311", "146:+3", 0x0U, "r20:buff", 0, 0, 20, "Zerthimon's Focus (op 301): critical chance +3, 5 s/level"},
    {"SPWI312", "14:+150", 0x0U, "r20:buff", 0, 0, 20, "Infernal Shield (op 375 type 14): fire resistance +150, 5 s/level"},
    {"SPWI313", "-", 0x0U, "none", -80, -2, 0, "Fiery Rain: 10 meteors of 2d4 fire (save) around the target"},
    {"SPWI314", "-", 0x0U, "r20:buff", 0, 0, 20,
     "Balance in All Things (op 375 type 16): each melee hit on the caster hurts the attacker as much, for level/4 "
     "(5) hits or 100 s (spell_follow: 4/4, 2/2, 4/4, then gone after the 5th)"},
    {"SPWI401", "-", 0x20U, "r2:debuff", -24, -21, 2,
     "Blacksphere (projectile 301): 1d4 + level (20) force, then op 39 sleep 10 s on a failed save (STATE 0x20); the "
     "turn waits ~100 ticks for the sleep onset; the foe loses its next 2 turns, acts on the 3rd (spell_follow)"},
    {"SPWI402", "-", 0x80000000U, "r6:debuff", 0, 0, 6,
     "Confusion (op 128 + tints): several creatures, 10 s + 1 s/level (30 s); icon r6 down by one per owner turn; the "
     "confused foe's turns are the engine's AI (RandomTurn / stands): our idle nudge used to order Attack(PlayerN) "
     "every turn (fixed: EngineDrivesAi)"},
    {"SPWI405", "-", 0x0U, "none", -196, -154, 0,
     "Force Missiles (multi-fire, projectile 227): 1 + min(6, (level-7)/2) = 7 missiles at 20, each 2d4 plus a "
     "level-damage blast (3 ft, save negates; adjacent creatures too, the caster included: vanilla); missiles 2-7 "
     "are aimed by the player with the target cursor and the turn waits for them (fixed: they used to be lost or "
     "fireable in a later turn); End Turn drops the rest"},
    {"SPWI406", "*", 0x0U, "r20:buff", 0, 0, 20,
     "Improved Strength (projectile 228): STR + class dice (priest 1d6+4: +7 on Grace), 5 s/level (100 s)"},
    {"SPWI408", "2:-3", 0x0U, "r*:buff", 0, 0, 0,
     "Shroud of Shadows (projectile 229): AC -3, 5-20 s + 10 s (r4 measured)"},
    {"SPWI409", "-", 0x0U, "none", 0, 0, 0,
     "Remove Curse (op 26): costs the action, nothing timed; the uncurse itself not exercised (no cursed item)"},
    {"SPWI410", "-", 0x0U, "none", -196, -154, 0,
     "Elemental Strike (multi-fire as Force Missiles, FRBLGREN): the engine gives it the same 7 missiles at 20 "
     "(the text's every-3rd-level is not what TransferEffects does: vanilla), 2d4 + level blast each"},
    {"SPWI501", "*", ~0U, "none", -1000, 0, 0,
     "Cloudkill (projectile 230): op 13 death by HD (the suite's thug dies: STATE 0x800), else 1d10 poison; the turn "
     "waits for the cloud to land"},
    {"SPWI502", "-", 0x20U, "r2:debuff", -100, -10, 2,
     "Cone of Cold (projectile 243): 10d4 + 10 cold at 100 ticks (resolves in the cast turn) and op 109 paralysis 105 "
     "ticks; a creature in two of the cone's layers takes it twice (real time too at the same distance: vanilla); the "
     "paralysed foe loses 1 turn and acts at its 2nd turn start (a stale STATE read cost it a 2nd: LoseTurnCheck)"},
    {"SPWI504", "8:+1", 0x400000U, "r24:buff", 0, 0, 24,
     "Enoll Eva's Duplication (op 1 200 %, 357, 375): APR 3/2 -> 5/2 (the engine's stat; the text's 60 s is 120 s in "
     "the SPL), the turn swings 2, 3, 2 as the stat says (spell_follow -CasterAttacks)"},
    {"SPWI505", "-", 0x0U, "r2:debuff", -40, -4, 2,
     "Desert Hell: four 1d10 fire hits at 30/60/90/120 ticks, all in the cast turn (F1), save 1/2"},
    {"SPWI506", "-", 0x0U, "none", -60, -2, 0,
     "Fire and Ice (projectile 245): a fire sphere then a cold sphere, both land in the cast turn"},
    {"SPWI601", "2:+7,7:+1,42:-4,51:+100,52:+100,73:+100", 0x0U, "r60:buff", 0, 0, 60,
     "Antimagic Shell (op 375 type 8, 300 s = 60 rounds): the object re-applies 60-tick resistances (MR 100 ...); they "
     "lapse at the caster's turn start and are back for every enemy turn (spell_follow -During); the icon follows the "
     "object (r60, r59 ...: the re-applied effects showed r1, fixed in status.h)"},
    {"SPWI602", "-", 0x0U, "r*:buff", 0, 0, 0,
     "Globe of Invulnerability (CPsSpellGlobeOfInvulnerability): the globe re-applies its 3 s protection; now paced "
     "as a lingering object (one round of ticks per caster round); it held every later turn to the 4-round cap as a "
     "projectile 'in flight' (fixed)"},
    {"SPWI603", "-", 0x8U, "r*:debuff", 0, 0, 0,
     "Howl of Pandemonium (projectile 231): engine stun by HD (STATE 0x8, random length), stunned foes lose their "
     "turns; its icon comes from the spell while its wind is still out (turn_cast_res; it was missing for far ones)"},
    {"SPWI604", "-", 0x0U, "none", -70, -3, 0,
     "Chain Lightning Storm (projectile 246): 7d10 electric on hostiles, save 1/2, in the cast turn"},
    {"SPWI701", "-", 0x0U, "none", 0, 0, 0,
     "Acid Storm (projectile 247): no hit in the cast turn; the acid is a DOT step at each victim turn start (-9, -3, "
     "-4), its icon r5 counting down from the first step (spell_follow)"},
    {"SPWI702", "-", 0x0U, "none", -140, -10, 0, "Blade Storm (projectile): shards on every foe in range, save 1/2, in the cast turn"},
    {"SPWI704", "-", 0x0U, "r30:buff", 0, 0, 30, "Guardian Mantle (op 375): 50 s + 5 s/level (150 s), the object's icon"},
    {"SPWI705", "-", 0x80000020U, "r*:debuff", -64, -8, 0,
     "Stygian Ice Storm: 8d8 cold with its visuals (SyncSpellOnsets), paralysis and the projectile's confusion"},
    {"SPWI802", "-", 0x0U, "none", -120, -5, 0, "Meteor Storm Bombardment: 10d12 to all foes, save 1/2, in the cast turn"},
    {"SPWI803", "-", ~0U, "none", -1000, 0, 0,
     "Deathbolt (CPsSpellDeathBolt, ~280 ticks of flight): death on a failed save, else 10d6; its effects reach the "
     "foe just after the bolt object is gone: the turn holds kSpellLandTicks for them (they came in the next "
     "creature's turn: fixed); the suite's foe dies and the fight ends before its reading (skip)"},
    {"SPWI804", "-", 0x0U, "r*:debuff", -200, -10, 0, "Ignus' Fury (projectile): fire on foes in the cast turn"},
    {"SPWI805", "-", 0x0U, "none", 0, 0, 0,
     "Power Word, Blind: nothing on a foe over 100 HP (vanilla; the suite's has 1000); a 50 HP foe is blinded "
     "(STATE 0x40000) in the cast turn, r23 down by one per turn (spell_follow -FoeHp 50)"},
    {"SPWI807", "-", 0x0U, "none", -90, -30, 0, "Mechanus Cannon: 20d4 + 10, in the cast turn"},
    {"SPWI901", "-", 0x0U, "none", -120, -40, 0,
     "Celestial Host (CPsSpellCelestialHost, a ~40 s cinematic owning the game freeze): its turn waits up to 20 "
     "rounds (kCinematicCapTicks); the 4-round cap cut it at stage 13 and it did nothing (fixed); 40d3 on foes"},
    {"SPWI902", "-", 0x0U, "none", 0, 0, 0,
     "Conflagration: the flames are an attached object (kind 13) on each victim: 2d6 at each victim turn start for "
     "50 s (its pulse countdown on the round clock: PulseAfterShift; it burned every 51 world ticks, in anyone's "
     "turn); it spreads to creatures in 10 ft, the party too (vanilla)"},
    {"SPWI905", "-", 0x0U, "none", -80, -5, 0,
     "Elysium's Fires: fire bursts in the cast turn (it 'fizzled' while Celestial Host's projectile held the freeze)"},
    {"SPWI909", "-", 0x0U, "none", 0, 0, 0,
     "Power Word, Kill: nothing over 120 HP (vanilla; the suite's foe has 1000); a 100 HP foe dies in the cast turn"},
    {"SPWI912", "-", ~0U, "none", -1000, -1, 0, "Abyssal Fury (projectile 234): the ground swallows the foe in the cast turn"},
    {"SPWI914", "-", 0x0U, "none", -100, -30, 0, "Rune of Torment: 14d6 + 16 in the area, a long cast not cut"},
};
}  // namespace pstf::spellexpect
