// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based combat: who is in the fight (pure, unit-tested). Fed each scan with every creature in
// the party's area. Uses the engine's own notions: EA ranges (CAIObjectType::IsEnemyOf: <= 30 good,
// >= 200 evil) and its "nearby" ellipse (CInfGame::CanRestParty: dx^2 + 16*dy^2/9 <= r^2, isometric).
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

// `in_fight`: ids the director currently has. `running`: a fight is on.
inline CensusOut Census(const std::vector<Seen>& all, const std::vector<int32_t>& in_fight,
                        bool running, const Radii& r) {
  CensusOut out;
  auto in = [&](int32_t id) {
    return std::find(in_fight.begin(), in_fight.end(), id) != in_fight.end();
  };
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
