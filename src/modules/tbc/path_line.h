// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based HUD: the movement line along the engine's own walk path (pure, unit-tested).
// The path is the int cell list of CGameSprite (+0x4470), walked from index +0x44F0 to count +0x4478
// (CGameSprite::AIUpdateWalk, win 1403B6D20). Dots are laid every `spacing_ft` feet from the
// creature along the path; the ones beyond the movement left are flagged (drawn red: never walked,
// the economy stops the walk there).
#pragma once
#include <cstdint>
#include <vector>

#include "modules/tbc/economy.h"
#include "modules/tbc/tbc_rules.h"  // Pt

namespace pstf::tbc {

struct Grid {
  int actual_x = 320, actual_y = 320;  // CPathSearch::GRID_ACTUALX/Y (.rdata 14062E58C/590)
  int sx = 16, sy = 12;                // GRID_SQUARE_SIZEX/Y (.rdata 14062E588/589)
};

// AIUpdateWalk: x = c % GRID_ACTUALX, y = GRID_ACTUALY - 1 - c / GRID_ACTUALX, centre of the cell.
inline Pt CellCentre(int32_t c, const Grid& g) {
  const int cx = c % g.actual_x, cy = g.actual_y - 1 - c / g.actual_x;
  return {cx * g.sx + g.sx / 2, cy * g.sy + g.sy / 2};
}

// The movement line is for walking: none while the creature casts (its own action is a cast, also the walk
// into range of a touch spell) and none for enemies' and allies' turns.
inline bool ShowPathLine(bool party_turn, uint16_t current_action) {
  return party_turn && !IsCastAction(current_action);
}

struct Dot {
  Pt p;
  bool reachable;
};

// Dots from `from` through `nodes`, one every `spacing_ft`, at most `max_dots`.
inline std::vector<Dot> PathDots(Pt from, const std::vector<Pt>& nodes, double move_ft, double spacing_ft,
                                 size_t max_dots) {
  std::vector<Dot> out;
  if (spacing_ft <= 0) return out;
  double walked = 0, next = spacing_ft;
  Pt a = from;
  for (const Pt& b : nodes) {
    const double seg = FeetBetween(a.x, a.y, b.x, b.y);
    while (seg > 0 && next <= walked + seg && out.size() < max_dots) {
      const double t = (next - walked) / seg;
      out.push_back({{a.x + static_cast<int>((b.x - a.x) * t), a.y + static_cast<int>((b.y - a.y) * t)},
                     next <= move_ft + 1e-6});
      next += spacing_ft;
    }
    walked += seg;
    a = b;
  }
  return out;
}

// Path length in feet (from the creature through all nodes).
inline double PathFeet(Pt from, const std::vector<Pt>& nodes) {
  double d = 0;
  for (const Pt& b : nodes) {
    d += FeetBetween(from.x, from.y, b.x, b.y);
    from = b;
  }
  return d;
}

}  // namespace pstf::tbc
