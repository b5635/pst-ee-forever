// SPDX-License-Identifier: GPL-2.0-or-later
// Module: dialogue ability rolls (BG3 style). A reply the engine hides only because the Protagonist's
// STR/DEX/CON/INT/WIS/CHR is too low is listed obscured; clicking it opens the roll panel (d20 +
// floor((ability - 10) / 2) against max(10, (needed - 10) * 2), natural 20/1 always win/lose). On a win the
// node's replies are evaluated by the engine again as if the ability were the needed value (for this node
// only: the next node is vanilla again). Ini [dialogue] rolls=1 (0 = vanilla: no hooks).
#pragma once
#include "core/ini.h"

namespace pstf::dlgroll {

void InstallDialogueRolls(const IniMap& ini);

}  // namespace pstf::dlgroll
