// SPDX-License-Identifier: GPL-2.0-or-later
// Module: sub-tick frame interpolation. Ini [smooth] interpolation=1 (0 = vanilla: no hooks at all).
#pragma once
#include "core/ini.h"

namespace pstf::smooth {

void InstallSmooth(const IniMap& ini);

}  // namespace pstf::smooth
