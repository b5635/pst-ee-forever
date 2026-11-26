// SPDX-License-Identifier: GPL-2.0-or-later
// Module: status-effect visuals (statusfx_logic.h). Ini [statusfx] enabled=1 (0 = vanilla: no hooks, no
// glow changes). Presentation only.
#pragma once
#include "core/ini.h"

namespace pstf::statusfx {

void InstallStatusFx(const IniMap& ini);

}  // namespace pstf::statusfx
