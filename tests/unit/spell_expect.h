// SPDX-License-Identifier: GPL-2.0-or-later
// Spells checked one by one by hand (docs/SPELL_TEST_PLAN.md section 3, column "Vetted"): what each one does in a
// turn-based fight, as measured in game and compared with its SPL, its description and the engine's ApplyEffect.
// test_spells.cpp compares every row with the suite's latest measurement (spell_results.h, foe fails its saves).
// Add a row only after checking the spell yourself; the note says what was checked and where.
#pragma once

namespace pstf::spellexpect {
struct Expect {
  const char* res;
  const char* stats;  // exact derived stat changes on the measured creature, as spell_results.h writes them
  unsigned state;     // STATE bits set
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
    {"SPWI201", "-", 0x0U, "none", -8, -2, 7,
     "Adder's Kiss (op 374): 2-8 in the cast turn, then one 2-8 hit at each of the victim's turn starts, "
     "1 + level/3 hits in all (7 at level 20), then none (tbc_decay.lua phase C); no status icon (vanilla)"},
};
}  // namespace pstf::spellexpect
