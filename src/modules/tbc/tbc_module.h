// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/ini.h"

namespace pstf::tbc {

// Turn-based combat ([tbc] enabled=1). Off = no hooks installed, vanilla real time.
bool InstallTbc(const IniMap& ini);

}  // namespace pstf::tbc
