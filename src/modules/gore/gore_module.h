// SPDX-License-Identifier: GPL-2.0-or-later
// Module: material hit effects (blood, sparks, bone dust and chips, grit, wisps, smoke, embers) instead of
// the engine's blood animations, with stains and debris on the floor under the actors. Ini [gore]
// enabled=1 (0 = vanilla: no hooks). Presentation only: damage, sounds, scripts and the engine's critical
// hit animations are untouched (crits get an exaggerated spray on top).
#pragma once
#include "core/ini.h"

namespace pstf::gore {

void InstallGore(const IniMap& ini);

}  // namespace pstf::gore
