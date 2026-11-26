// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based combat: who is in the fight (pure, unit-tested). Fed each scan with every creature in
// the party's area. Uses the engine's own notions: EA ranges (CAIObjectType::IsEnemyOf: <= 30 good,
// >= 200 evil) and its "nearby" ellipse (CInfGame::CanRestParty: dx^2 + 16*dy^2/9 <= r^2, isometric).
// Neutrals (EA 31..199) stay out, unless one is fighting a combatant (its attack or spell targets one, or a
// combatant's targets it): then it joins on the other side from that combatant, or it would act in real time
// against creatures held for their turns.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace pstf::tbc {

constexpr uint8_t kEaGoodCutoff = 30;   // EA_GOODCUTOFF
constexpr uint8_t kEaEvilCutoff = 200;  // EA_EVILCUTOFF
constexpr bool IsHostileEa(uint8_t ea) { return ea >= kEaEvilCutoff; }
constexpr bool IsFriendlyEa(uint8_t ea) { return ea <= kEaGoodCutoff; }

constexpr bool Near(int x1, int y1, int x2, int y2, int r) {
  const long long dx = x1 - x2, dy = y1 - y2;
  return dx * dx + 16 * dy * dy / 9 <= static_cast<long long>(r) * r;
}

struct Seen {
  int32_t id;
  bool party;  // one of the (up to 6) party members
  uint8_t ea;
  bool alive;
  int x, y;
  int32_t target = -1;    // its current action's target (engine UpdateTarget), -1: none
  bool fighting = false;  // its current action is an attack or a spell (target is a foe)
  bool ally = false;      // output only: a neutral joining on the party's side
};

struct Radii {
  int start = 450;   // a hostile this close to a party member starts a fight
  int join = 650;    // creatures this close to a party member join (or are pulled in at start)
  int leave = 1100;  // a hostile farther than this from every party member has left the fight
};

struct CensusOut {
  bool start = false;
  std::vector<Seen> join;      // not yet in the fight, should be now
  std::vector<int32_t> leave;  // in the fight, should not be any more
};

inline bool NearAnyParty(const Seen& s, const std::vector<Seen>& all, int r) {
  return std::any_of(all.begin(), all.end(), [&](const Seen& p) {
    return p.party && p.alive && Near(s.x, s.y, p.x, p.y, r);
  });
}

// The side an EA puts a creature on: 1 party side (good), 2 hostile side (evil), 0 neutral (no own side).
constexpr int SideOfEa(uint8_t ea) { return IsFriendlyEa(ea) ? 1 : IsHostileEa(ea) ? 2 : 0; }

// `in_fight`: ids the director currently has; `hostile_side`: those of them on the hostile side.
// `running`: a fight is on.
inline CensusOut Census(const std::vector<Seen>& all, const std::vector<int32_t>& in_fight,
                        bool running, const Radii& r, const std::vector<int32_t>& hostile_side = {}) {
  CensusOut out;
  auto has = [](const std::vector<int32_t>& v, int32_t id) { return std::find(v.begin(), v.end(), id) != v.end(); };
  auto in = [&](int32_t id) { return has(in_fight, id); };
  if (!running) {
    out.start = std::any_of(all.begin(), all.end(), [&](const Seen& s) {
      return s.alive && !s.party && IsHostileEa(s.ea) && NearAnyParty(s, all, r.start);
    });
    if (!out.start) return out;
  }
  for (const auto& s : all) {
    if (!s.alive || in(s.id)) continue;
    if (s.party || ((IsHostileEa(s.ea) || IsFriendlyEa(s.ea)) && NearAnyParty(s, all, r.join)))
      out.join.push_back(s);
  }
  // Sides of everyone in the fight after this scan: 1 party side, 2 hostile side, 0 not in it.
  auto side = [&](int32_t id) -> int {
    if (in(id)) return has(hostile_side, id) ? 2 : 1;
    for (const auto& j : out.join)
      if (j.id == id) return j.party || IsFriendlyEa(j.ea) || j.ally ? 1 : 2;
    return 0;
  };
  for (bool more = true; more;) {  // a neutral pulled in may be fighting another neutral: repeat
    more = false;
    for (const auto& s : all) {
      if (!s.alive || s.party || IsHostileEa(s.ea) || IsFriendlyEa(s.ea) || side(s.id) ||
          !NearAnyParty(s, all, r.join))
        continue;
      int foe = s.fighting && s.target != s.id ? side(s.target) : 0;
      for (const auto& c : all)
        if (!foe && c.alive && c.fighting && c.target == s.id && c.id != s.id) foe = side(c.id);
      if (!foe) continue;
      Seen j = s;
      j.ally = foe == 2;
      out.join.push_back(j);
      more = true;
    }
  }
  for (int32_t id : in_fight) {
    auto it = std::find_if(all.begin(), all.end(), [&](const Seen& s) { return s.id == id; });
    if (it == all.end() || !it->alive) {
      out.leave.push_back(id);  // dead, or left the area
    } else if (!it->party && !NearAnyParty(*it, all, r.leave)) {
      out.leave.push_back(id);  // fled
    }
  }
  return out;
}

}  // namespace pstf::tbc
