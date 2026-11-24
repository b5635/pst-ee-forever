// SPDX-License-Identifier: GPL-2.0-or-later
// The BG3-style d20 roll panel, shared by every check (dialogue ability rolls, lockpicking, bashing).
//
// Lua side (menu PSTF_ROLL): a module opens it with
//   pstfDrOpen{ title = 'Lockpicking Check', ab = 'Lockpicking', card = 1, mod = 9, dc = 14,
//               roll = function() return nat, total, outcome end,   -- C++: RollD20 (-1 = cannot roll)
//               done = function() end,                              -- Continue after the roll
//               alive = function() return true end,                 -- false closes the panel (no done)
//               back = function() end,                              -- Back before a roll (optional)
//               note = 'text',                                      -- optional: no Roll button, Back only
//               pause = true }                                      -- pause the world while it is up
// The panel animates the throw, the bonus card, the total and the banner (timeline: d20.h RollTiming).
// Every probe the dialogue test drives (pstfDrRollClick, pstfDrMs, pstfDrContinue, ...) is the panel's.
#pragma once
#include "modules/rolls/d20.h"

namespace pstf::rolls {

// Adds the panel menu and its Lua once (idempotent). False when the Lua/menu functions differ from the
// manifest (then no module may open it).
bool InstallRollPanel();

struct D20 {
  int natural = 0, total = 0;
  Outcome outcome = Outcome::Fail;
};
// One throw (uniform 1..20, or the test-forced value of PSTF_RollForce(n) once) resolved against `dc`.
D20 RollD20(int modifier, int dc);

// pst_forever\sounds\check_success.wav / check_failed.wav (they carry the whole roll's cues); `what` names
// the caller in the log.
void PlayRollSound(bool ok, const char* what);

}  // namespace pstf::rolls
