// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/tbc/tbc_sim.h"

#include <cstdio>
#include <map>

#include "modules/tbc/director.h"

namespace pstf::tbc {

std::string SimResult::Summary() const {
  char b[128];
  std::snprintf(b, sizeof b, "winner=%s rounds=%u swings=%u clock=%u",
                winner == 0 ? "party" : winner == 1 ? "hostile" : "none", rounds, swings,
                clock_ticks);
  return b;
}

SimResult RunSim(uint32_t seed, uint32_t round_ticks) {
  Director d(seed, round_ticks);
  std::map<int32_t, int> hp = {{1, 6}, {2, 6}, {10, 3}, {11, 3}};
  d.Start({{1, Side::Party, 1, 3, 100},
           {2, Side::Party, 0, 2, 100},
           {10, Side::Hostile, 0, 2, 100},
           {11, Side::Hostile, -1, 2, 100}},
          0);
  SimResult r;
  for (int guard = 0; d.Running() && guard < 1000; ++guard) {
    const int32_t me = d.Current();
    const Combatant* c = d.Find(me);
    if (!c) break;
    if (c->move_left == c->r.move_budget && d.BeginMove(me, 30)) {
      d.MoveFinished(me, 30);
      continue;
    }
    if (d.BeginSwing(me)) {
      ++r.swings;
      const Side enemy = c->r.side == Side::Party ? Side::Hostile : Side::Party;
      int32_t target = -1;
      for (int32_t id : d.Order())
        if (d.Find(id)->r.side == enemy) {
          target = id;
          break;
        }
      d.SwingResolved(me);
      if (target >= 0 && --hp[target] <= 0) d.Remove(target);
      continue;
    }
    d.EndTurn(me);
  }
  for (const auto& e : d.TakeEvents())
    if (e.type == EvType::CombatEnd) r.winner = e.value;
  r.rounds = d.Round();
  r.clock_ticks = d.Clock();
  return r;
}

}  // namespace pstf::tbc
