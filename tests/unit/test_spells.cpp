// SPDX-License-Identifier: GPL-2.0-or-later
// L0 tests per player spell: every SPWI/SPPR spell of the game (tests/unit/spell_table.h, tools/spell_list.py) was
// cast by the engine in a turn-based fight by the all-spells suite (tests/unit/spell_results.h,
// tools\spell_suite.ps1), and the round-clock rules classify its delayed effects. A new or changed spell (a mod)
// fails here until tools/spell_list.py and the suite have been run again.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>
#include <vector>

#include "mini_test.h"
#include "modules/tbc/round_rules.h"
#include "modules/tbc/status.h"
#include "spell_expect.h"
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

namespace {
// Spells the generic outcome checks below cannot judge, each with why (and where it is checked or open instead,
// docs/SPELL_TEST_PLAN.md section 4).
struct Skip {
  const char* res;
  const char* why;
};
constexpr Skip kOutcomeSkips[] = {
    {"SPWI307", "Hold Undead: the suite's save has no undead to hold"},
    {"SPWI108", "Pacify: nothing on a hostile Hive thug, in real time too (vanilla; probe pacify, 2026-11-25)"},
    {"SPWI601", "Antimagic Shell: strips the caster's own magic, its icon carrier too (G test)"},
    {"SPWI603", "Howl of Pandemonium: the stun's length is the engine's (by HD), random (vetted r*)"},
    {"SPWI803", "Deathbolt: kills the suite's only foe, the fight ends before the measurement (death in the cast turn: spell_follow)"},
    {"SPWI705", "Stygian Ice Storm: its projectile adds a 30 s confusion: the icon outlasts the SPL's paralyse"},
    {"SPWI218", "Infernal Orb: needs the caster's attack roll, a miss does nothing"},
    {"SPWI902", "Conflagration: burns at the victims' turn starts, nothing in the cast turn (vetted)"},
};
bool Skipped(const char* res) {
  for (const auto& k : kOutcomeSkips)
    if (std::strcmp(k.res, res) == 0) return true;
  return false;
}
// The measured change of one derived stat in a result's "STAT:+N,..." list (0 when not listed).
int StatDelta(const char* list, int st) {
  if (!list) return 0;
  const std::string s(list);
  const std::string key = std::to_string(st) + ":";
  size_t p = 0;
  while ((p = s.find(key, p)) != std::string::npos) {
    if (p == 0 || s[p - 1] == ',') return std::atoi(s.c_str() + p + key.size());
    p += key.size();
  }
  return 0;
}
int IconRounds(const char* icon) { return icon && icon[0] == 'r' ? std::atoi(icon + 1) : 0; }
}  // namespace

TEST_CASE("spells: each spell's own timed effects are on at the end of its cast turn, with their icon") {
  // From the SPL (spell_table.h, the suite caster's header) and the suite's measurement at the end of the caster's
  // turn (spell_results.h; the foe always fails its saves): a timed effect (timing 0) on the spell's target shows as
  // the engine's stat or STATE bit, and the spell's status icon shows ceil(duration / 5 s) rounds (the round clock
  // steps 75 ticks per turn start). Hardcoded projectiles' own effects are not in the table: only what the SPL says.
  struct Want {
    const char* res;
    std::string what;
  };
  // Durations the engine computes itself (the SPL field is not the duration), checked by their own tests:
  //   SPPR104 Detect Evil (op 376: 50 s + 25 s/level, 5 min cap), SPWI104 Fist of Iron (op 382: 5 s + 5 s/level).
  auto engine_duration = [](const char* res) {
    return std::strcmp(res, "SPPR104") == 0 || std::strcmp(res, "SPWI104") == 0;
  };
  std::vector<Want> bad;
  int checked = 0;
  for (const auto& s : spelltable::kSpells) {
    const auto* r = ResultOf(s.res);
    if (!r || std::strcmp(r->verdict, "ok") != 0 || !r->stats) continue;
    if (engine_duration(s.res) || Skipped(s.res)) continue;
    uint32_t longest = 0;
    int luck_rows = 0;
    std::vector<int> rounds_ok;  // a spell with several rows of one opcode applies one of them (probability ranges)
    for (int i = s.first; i < s.first + s.count; ++i) {
      const auto& f = spelltable::kFx[i];
      if (f.timing != 0 || f.duration == 0 || tbc::IsPresentationOpcode(f.op)) continue;
      if (f.target != 1 && f.target != 2) continue;  // self / preset target: the measured creature
      longest = std::max(longest, f.duration);
      rounds_ok.push_back(static_cast<int>((f.duration * 15 + tbc::kRoundTicks5s - 1) / tbc::kRoundTicks5s));
      uint32_t bit = 0;
      switch (f.op) {
        case 74: bit = 0x40000; break;       // blind
        case 24: bit = 0x4; break;           // horror: panic
        case 128: bit = 0x80000000u; break;  // confusion
        case 109: bit = 0x20; break;         // paralyse: helpless
        case 39: bit = 0x1; break;           // sleep
        case 130: bit = 0x800000; break;     // bless
        case 129: bit = 0x8000000; break;    // aid
        case 22: ++luck_rows; break;         // luck: one of its rows applies
        default: break;
      }
      if (bit && (r->state & bit) == 0) bad.push_back({s.res, "op " + std::to_string(f.op) + " STATE bit missing"});
    }
    if (luck_rows) {
      const int d = StatDelta(r->stats, 32);
      if (d < 2 || d > 8) bad.push_back({s.res, "luck +" + std::to_string(d)});
    }
    if (longest) {
      ++checked;
      const int want = static_cast<int>((longest * 15 + tbc::kRoundTicks5s - 1) / tbc::kRoundTicks5s);
      const bool any = std::find(rounds_ok.begin(), rounds_ok.end(), IconRounds(r->icon)) != rounds_ok.end();
      if (IconRounds(r->icon) != want && !any)
        bad.push_back({s.res, std::string("icon ") + r->icon + ", want r" + std::to_string(want)});
    }
  }
  for (const auto& b : bad) std::printf("  %s (%s)\n", b.res, b.what.c_str());
  if (checked < 20) std::printf("  outcome checks: only %d spells with a timed effect on their target\n", checked);
  CHECK(bad.empty());
  CHECK(checked >= 19);  // 19 on 2026-11-25 (93 spells, 8 skips, 2 engine durations): fewer = rows lost
}

TEST_CASE("spells: damage lands in the caster's turn, heals heal") {
  // op 12 (damage) on the target, not delayed past a round: the foe's HP is lower at the end of the cast turn; op 17
  // (heal) on a friendly spell's caster (who starts at 30 HP): higher.
  std::vector<std::string> bad;
  for (const auto& s : spelltable::kSpells) {
    const auto* r = ResultOf(s.res);
    if (!r || std::strcmp(r->verdict, "ok") != 0 || !r->stats) continue;
    // Raise Dead targets a dead creature (header target 3), not its caster; Missile of Patience waits for a click
    if (s.target == 3 || std::strcmp(s.res, "SPWI113") == 0 || Skipped(s.res)) continue;
    bool dmg = false, heal = false;
    for (int i = s.first; i < s.first + s.count; ++i) {
      const auto& f = spelltable::kFx[i];
      if (f.target != 2 && f.target != 1) continue;
      const bool now = f.timing == 1 || f.timing == 0 ||
                       (f.timing == 4 && tbc::OnsetKind(static_cast<int32_t>(f.duration) - 1, f.op) != 0);
      if (f.op == 12 && now && s.hostile) dmg = true;
      if (f.op == 17 && now && !s.hostile) heal = true;
    }
    if (dmg && r->hp >= 0) bad.push_back(std::string(s.res) + " " + s.name + ": no damage in the cast turn");
    if (heal && r->hp <= 0) bad.push_back(std::string(s.res) + " " + s.name + ": no healing");
  }
  for (const auto& b : bad) std::printf("  %s\n", b.c_str());
  CHECK(bad.empty());
}

namespace {
// "r*:debuff" = any rounds of that kind (a random duration: Chromatic Orb's paralysis 10-40 s)
bool IconMatches(const char* got, const char* want) {
  if (std::strncmp(want, "r*:", 3) == 0) return got[0] == 'r' && std::strstr(got, want + 2) != nullptr;
  return std::strcmp(got, want) == 0;
}
}  // namespace

TEST_CASE("spells: every hand-vetted spell still measures as vetted (spell_expect.h vs spell_results.h)") {
  int n = 0;
  for (const auto& e : spellexpect::kExpect) {
    const auto* r = ResultOf(e.res);
    CHECK(r != nullptr);
    if (!r) continue;
    ++n;
    const bool ok = std::strcmp(r->verdict, "ok") == 0 && (std::strcmp(e.stats, "*") == 0 || std::strcmp(r->stats, e.stats) == 0) && (e.state == ~0U || r->state == e.state) &&
                    IconMatches(r->icon, e.icon) && r->hp >= e.hp_min && r->hp <= e.hp_max &&
                    (IconRounds(e.icon) == 0 || IconRounds(e.icon) == e.rounds);
    if (!ok)
      std::printf("  %s: measured %s st=%s state=%X icon=%s hp=%d; vetted st=%s state=%X icon=%s hp %d..%d\n", e.res,
                  r->verdict, r->stats, r->state, r->icon, r->hp, e.stats, e.state, e.icon, e.hp_min, e.hp_max);
    CHECK(ok);
  }
  CHECK(n == static_cast<int>(sizeof spellexpect::kExpect / sizeof spellexpect::kExpect[0]));
}
