// SPDX-License-Identifier: GPL-2.0-or-later
// L0 tests per player spell: every SPWI/SPPR spell of the game (tests/unit/spell_table.h, tools/spell_list.py) was
// cast by the engine in a turn-based fight by the all-spells suite (tests/unit/spell_results.h,
// tools\spell_suite.ps1), and the round-clock rules classify its delayed effects. A new or changed spell (a mod)
// fails here until tools/spell_list.py and the suite have been run again.
#include <cstdio>
#include <cstring>
#include <vector>

#include "mini_test.h"
#include "modules/tbc/round_rules.h"
#include "modules/tbc/status.h"
#include "spell_results.h"
#include "spell_table.h"

using namespace pstf;

namespace {
const spellresults::Result* ResultOf(const char* res) {
  for (const auto& r : spellresults::kResults)
    if (std::strcmp(r.res, res) == 0) return &r;
  return nullptr;
}
}  // namespace

TEST_CASE("spells: every player spell is cast in a turn-based fight, and none stalls its caster's turn") {
  // ok = the engine cast it (its "casts <spell>" feedback) on the caster's turn and the turn ended after End Turn.
  // The suite raises the casters' levels (the save leaves them below most spells' first ability), gives every
  // spell a clean caster (UnapplySpell) and retries a miss once on the next turn.
  int n = 0;
  for (const auto& s : spelltable::kSpells) {
    ++n;
    const auto* r = ResultOf(s.res);
    if (!r) std::printf("  no suite result for %s (%s): run tools\\spell_suite.ps1\n", s.res, s.name);
    CHECK(r != nullptr);
    if (!r) continue;
    if (std::strcmp(r->verdict, "ok") != 0) std::printf("  %s (%s): %s in a turn-based fight\n", s.res, s.name, r->verdict);
    CHECK(std::strcmp(r->verdict, "ok") == 0);
    // the cast resolved and its turn closed within the suite's window after End Turn
    CHECK(r->ticks > 0 && r->ticks < 750);
  }
  CHECK(n == 93);  // PST:EE 3.2.2 + PST-UB-Reloaded: 75 mage + 18 priest spells with a name
}

TEST_CASE("spells: table facts (ids, levels) match the game files") {
  for (const auto& s : spelltable::kSpells) {
    const bool mage = std::strncmp(s.res, "SPWI", 4) == 0;
    const int num = (s.res[4] - '0') * 100 + (s.res[5] - '0') * 10 + (s.res[6] - '0');
    CHECK(s.id == (mage ? 2000 : 1000) + num);
    CHECK(s.level >= 1 && s.level <= 9);
    // Effects in the SPL, or none and a projectile that carries them (Speak with Dead 205, Blade Storm 232, Abyssal
    // Fury 234: PST's hardcoded projectiles) or the engine's own (Identify, projectile 1).
    CHECK(s.count > 0 || s.projectile >= 1);
    if (s.count == 0)
      CHECK(std::strcmp(s.res, "SPPR304") == 0 || std::strcmp(s.res, "SPWI105") == 0 || std::strcmp(s.res, "SPWI702") == 0 ||
            std::strcmp(s.res, "SPWI912") == 0);
  }
}

TEST_CASE("spells: every delayed visual or sound is presentation (world time), never deferred a round") {
  // A delayed play of a BAM, glow or sound (369..373, 174, 215, ...) is presentation for every spell: the round
  // clock never holds it to a later turn (round_rules.h OnsetKind 2 whatever its onset).
  int seen = 0;
  for (const auto& s : spelltable::kSpells) {
    for (int i = s.first; i < s.first + s.count; ++i) {
      const auto& f = spelltable::kFx[i];
      const bool delayed = f.timing == 3 || f.timing == 4 || f.timing == 5;
      if (!delayed || !tbc::IsPresentationOpcode(f.op)) continue;
      ++seen;
      CHECK(tbc::OnsetKind(150, f.op) == 2);
      CHECK(tbc::OnsetKind(10, f.op) == 2);
    }
  }
  CHECK(seen > 0);
}

TEST_CASE("spells: Submerge the Will and Power of One add parentless effects the status icons attribute") {
  // Their real effects come from engine code with no parent (probe_buffs.lua: Submerge's AC + saves from opcode 375,
  // Power of One's strength from projectile 287). The table must still show the delayed carrier with the spell as
  // parent: opcode 375 for Submerge the Will, the delayed visuals for Power of One.
  bool submerge = false, power = false;
  for (const auto& s : spelltable::kSpells) {
    for (int i = s.first; i < s.first + s.count; ++i) {
      const auto& f = spelltable::kFx[i];
      if (std::strcmp(s.res, "SPWI119") == 0 && f.op == 375 && (f.timing == 3 || f.timing == 4)) submerge = true;
      if (std::strcmp(s.res, "SPWI219") == 0 && f.op == 369 && (f.timing == 3 || f.timing == 4)) power = true;
    }
  }
  CHECK(submerge);
  CHECK(power);
  const auto* r = ResultOf("SPWI119");
  // the 375 carrier at least; its op 0 + ops 33..37 come on the attached object's own update (6 when it ran before
  // the turn ended, 1 when not: both seen with the same 44-tick turn)
  CHECK(r && std::strcmp(r->verdict, "ok") == 0 && (r->fx == 1 || r->fx == 6));
}

namespace {
// The delayed non-presentation effects of one spell as the round clock sees them one tick after it landed.
std::vector<tbc::OnsetSeen> OnsetsOf(const spelltable::Spell& s) {
  std::vector<tbc::OnsetSeen> v;
  for (int i = s.first; i < s.first + s.count; ++i) {
    const auto& f = spelltable::kFx[i];
    if (f.timing != 4 || tbc::IsPresentationOpcode(f.op) || f.duration < 2) continue;
    const int32_t left = static_cast<int32_t>(f.duration) - 1;
    v.push_back({left, f.op, 1, tbc::OnsetKind(left, f.op)});
  }
  tbc::SyncSpellOnsets(v.data(), static_cast<int>(v.size()));
  return v;
}
}  // namespace

TEST_CASE("spells: Desert Hell's four heat hits (30/60/90/120 ticks) all land in the caster's turn") {
  // SPWI505: op12 1d10 fire at 30, 60, 90 and 120 ticks. 120 is past a round and a half, so alone it would be
  // round-clocked to the victim's next turn: one hit of an instant spell a round late.
  int hits = 0;
  for (const auto& s : spelltable::kSpells) {
    if (std::strcmp(s.res, "SPWI505") != 0) continue;
    for (const auto& o : OnsetsOf(s)) {
      if (o.opcode != 12) continue;
      ++hits;
      CHECK(o.kind == 1);
    }
  }
  CHECK(hits == 4);
}

TEST_CASE("spells: no player spell's own delayed damage is split between this turn and a later one") {
  // A spell's delayed damage series either resolves together now (an impact spread over its animation) or is a
  // damage-over-time series on 5-second steps that is round-clocked as a whole.
  for (const auto& s : spelltable::kSpells) {
    const auto v = OnsetsOf(s);
    bool now = false, later = false;
    for (const auto& o : v)
      if (o.opcode == 12) (o.kind == 0 ? later : now) = true;
    if (now && later) std::printf("  %s (%s): delayed damage split across turns\n", s.res, s.name);
    CHECK(!(now && later));
  }
}

TEST_CASE("spells: a timed buff's icon shows the rounds the round clock really runs it") {
  // Effect time steps kRoundTicks5s (75) at each of the owner's turn starts, so the icon counts 75-tick rounds,
  // not ROUND_LENGTH (51): Blessing's 30 s = 450 ticks = 6 rounds (with 51 it showed 9).
  for (const auto& s : spelltable::kSpells) {
    if (std::strcmp(s.res, "SPPR101") != 0) continue;
    const auto& f = spelltable::kFx[s.first];
    CHECK(f.op == 130 && f.timing == 0 && f.duration == 30);
    tbc::StatusEffect e;
    e.parent_type = 1, e.parent = s.res, e.timed = true, e.left = static_cast<int32_t>(f.duration) * 15;
    const auto icons = tbc::CollectStatus(1, {e}, 0, tbc::kRoundTicks5s, nullptr, 8);
    CHECK(icons.size() == 1 && icons[0].rounds == 6);
    // and the clock ends it after exactly that many turn-start steps
    int32_t left = e.left, steps = 0;
    while (left > 0) left -= tbc::kRoundTicks5s, ++steps;
    CHECK(steps == icons[0].rounds);
  }
}
