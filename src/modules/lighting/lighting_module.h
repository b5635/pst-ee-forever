// SPDX-License-Identifier: GPL-2.0-or-later
// Module: dynamic lighting from the baked GemRB scene data (docs/VISUALS_PLAN.md, phases 0-1).
// Ini [lighting] enabled=1 (0 = vanilla: no hooks at all). Presentation only: nothing a script,
// dialogue, item or spell can observe changes (the LM tint and CGameSprite::InDark stay the engine's).
#pragma once
#include "core/ini.h"

namespace pstf::lighting {

void InstallLighting(const IniMap& ini);

}  // namespace pstf::lighting
