// SPDX-License-Identifier: GPL-2.0-or-later
// Module: 8-member party. Ini [party] enabled=1, size=8 (0 / 6 = vanilla: nothing patched).
#pragma once
#include "core/ini.h"

namespace pstf::party {

// Runs first in PSTF_Init: it rewrites engine code other modules may check.
void InstallParty(const IniMap& ini);

}  // namespace pstf::party
