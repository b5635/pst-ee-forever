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
  const char* icon;   // status icon at the end of the cast turn ("rN:buff", "rN:debuff", "none")
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
    {"SPWI115", "2:+4,7:+4", 0x40000U, "r12:debuff", 0, 0, 12,
     "Blindness (op 74, 60 s): THAC0 +4, AC +4, STATE_BLIND (CGameEffectBlindness::ApplyEffect); a save negates it "
     "all (-Saves pass: no effect, no icon); decay as Vilquar's Eye's op 74 (tbc_decay.lua)"},
    {"SPWI201", "-", 0x0U, "none", -8, -2, 7,
     "Adder's Kiss (op 374): 2-8 in the cast turn, then one 2-8 hit at each of the victim's turn starts, "
     "1 + level/3 hits in all (7 at level 20), then none (tbc_decay.lua phase C); no status icon (vanilla)"},
};
}  // namespace pstf::spellexpect
