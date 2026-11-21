// SPDX-License-Identifier: GPL-2.0-or-later
// A scripted fight run through the real Director (pure). Used by the unit tests and by the
// in-game console probe PSTF_TbcSim(seed), so both sides prove the same code with the same numbers.
#pragma once
#include <cstdint>
#include <string>

namespace pstf::tbc {

struct SimResult {
  int winner = -1;  // 0 party, 1 hostile
  uint32_t rounds = 0;
  uint32_t swings = 0;
  uint32_t clock_ticks = 0;  // combat clock at the end (start tick 0)
  std::string Summary() const;
};

// Party: id 1 (1.5 APR, 6 hp), id 2 (1 APR, 6 hp). Hostiles: 10, 11 (1 APR, 3 hp).
// Every swing hits the first living enemy for 1 hp (the engine would roll; the sim only
// exercises turn flow). Movement: each creature walks 30 px before attacking.
SimResult RunSim(uint32_t seed, uint32_t round_ticks);

}  // namespace pstf::tbc
