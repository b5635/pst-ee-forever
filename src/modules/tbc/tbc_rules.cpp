// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/tbc/tbc_rules.h"

#include <cmath>

namespace pstf::tbc {

std::vector<Pt> ClipPath(const std::vector<Pt>& path, double budget, double* spent) {
  std::vector<Pt> out;
  double used = 0;
  if (!path.empty()) out.push_back(path[0]);
  for (size_t i = 1; i < path.size() && budget > used; ++i) {
    const double dx = path[i].x - path[i - 1].x, dy = path[i].y - path[i - 1].y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len == 0) continue;
    if (used + len <= budget) {
      out.push_back(path[i]);
      used += len;
      continue;
    }
    const double t = (budget - used) / len;
    const Pt cut{path[i - 1].x + static_cast<int>(std::floor(dx * t)),
                 path[i - 1].y + static_cast<int>(std::floor(dy * t))};
    if (!(cut == out.back())) {
      const double cx = cut.x - path[i - 1].x, cy = cut.y - path[i - 1].y;
      used += std::sqrt(cx * cx + cy * cy);
      out.push_back(cut);
    }
    break;
  }
  if (spent) *spent = used;
  return out;
}

}  // namespace pstf::tbc
