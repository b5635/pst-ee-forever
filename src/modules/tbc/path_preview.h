// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based HUD: where would this creature walk if the player clicked here? (pure, unit-tested)
// A* over search-map cells with the engine's own per-cell walk cost (CSearchBitmap::GetCost through
// engine::CellCoster: terrain table + personal space of the creature; 0xFF = impassable), 8 neighbours
// with diagonal steps allowed between two blocked cells (isometric corridors are diagonal stairs of
// cells; a corner-cut rule disconnected them in the Mortuary),
// then string-pulled where a straight line crosses only walkable cells, as CPathSearch::SmoothPath does.
// It is a preview: the walk itself is the engine's path, drawn from the sprite once it exists.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

#include "modules/tbc/tbc_rules.h"  // Pt

namespace pstf::tbc {

constexpr int kImpassable = 0xFF;
using CostFn = std::function<int(int cx, int cy)>;  // cell -> 1..254, or kImpassable

// Every cell on the segment (Bresenham) after `a` is walkable (a: where the walker already is) and,
// with `max_cost`, no dearer than that (a straight cut must not run through a creature the path went
// round).
inline bool LineWalkable(Pt a, Pt b, const CostFn& cost, int max_cost = kImpassable - 1) {
  int x = a.x, y = a.y;
  const int dx = std::abs(b.x - a.x), dy = -std::abs(b.y - a.y);
  const int sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1;
  int err = dx + dy;
  for (bool first = true;; first = false) {
    if (!first) {
      const int c = cost(x, y);
      if (c >= kImpassable || c > max_cost) return false;
    }
    if (x == b.x && y == b.y) return true;
    const int e2 = 2 * err;
    if (e2 >= dy) err += dy, x += sx;
    if (e2 <= dx) err += dx, y += sy;
  }
}

// Cells from start to goal (both included), smoothed to turning points; empty when unreachable
// or when the search exceeds `max_expand` cells. The start cell is never tested (the creature stands
// there).
// `min_cost`: the cheapest walkable cell cost (the terrain table's minimum), so the heuristic stays
// admissible and tight (the engine's open ground costs 5, not 1).
inline std::vector<Pt> FindCellPath(Pt start, Pt goal, const CostFn& cost, int max_expand, int grid_w = 320,
                                    int grid_h = 320, int min_cost = 1, int* expanded_out = nullptr) {
  std::vector<Pt> out;
  auto in = [&](int x, int y) { return x >= 0 && y >= 0 && x < grid_w && y < grid_h; };
  if (!in(goal.x, goal.y) || !in(start.x, start.y) || cost(goal.x, goal.y) >= kImpassable) return out;
  auto key = [&](int x, int y) { return y * grid_w + x; };
  auto h = [&](int x, int y) {  // octile distance, step costs 10 / 14
    const int dx = std::abs(x - goal.x), dy = std::abs(y - goal.y);
    return min_cost * (10 * std::max(dx, dy) + 4 * std::min(dx, dy));
  };
  struct Node {
    int f, g, k;
  };
  auto cmp = [](const Node& a, const Node& b) { return a.f > b.f || (a.f == b.f && a.g < b.g); };
  std::priority_queue<Node, std::vector<Node>, decltype(cmp)> open(cmp);
  std::unordered_map<int, int> g, parent;
  const int sk = key(start.x, start.y), gk = key(goal.x, goal.y);
  g[sk] = 0;
  parent[sk] = -1;
  open.push({h(start.x, start.y), 0, sk});
  int expanded = 0;
  bool found = sk == gk;
  while (!open.empty() && !found) {
    const Node n = open.top();
    open.pop();
    if (n.g != g[n.k]) continue;
    if (++expanded > max_expand) {
      if (expanded_out) *expanded_out = expanded;
      return out;
    }
    const int x = n.k % grid_w, y = n.k / grid_w;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy) continue;
        const int nx = x + dx, ny = y + dy;
        if (!in(nx, ny)) continue;
        const int c = cost(nx, ny);
        if (c >= kImpassable) continue;
        const int nk = key(nx, ny), ng = n.g + c * (dx && dy ? 14 : 10);
        auto it = g.find(nk);
        if (it != g.end() && it->second <= ng) continue;
        g[nk] = ng;
        parent[nk] = n.k;
        if (nk == gk) found = true;
        open.push({ng + h(nx, ny), ng, nk});
      }
  }
  if (expanded_out) *expanded_out = expanded;
  if (!found) return out;
  std::vector<Pt> cells;
  for (int k = gk; k != -1; k = parent[k]) cells.push_back({k % grid_w, k / grid_w});
  std::reverse(cells.begin(), cells.end());
  // String pulling: from each kept point, jump to the farthest cell still in a straight walkable line
  // that is no dearer per cell than the path it replaces (detours round creatures stay detours).
  std::vector<int> cell_cost(cells.size());
  for (size_t k = 0; k < cells.size(); ++k) cell_cost[k] = cost(cells[k].x, cells[k].y);
  out.push_back(cells.front());
  size_t i = 0;
  while (i + 1 < cells.size()) {
    size_t j = cells.size() - 1;
    auto dearest = [&](size_t a, size_t b) {
      int m = 0;
      for (size_t k = a + 1; k <= b; ++k) m = std::max(m, cell_cost[k]);
      return m;
    };
    while (j > i + 1 && !LineWalkable(cells[i], cells[j], cost, dearest(i, j))) --j;
    out.push_back(cells[j]);
    i = j;
  }
  return out;
}

}  // namespace pstf::tbc
