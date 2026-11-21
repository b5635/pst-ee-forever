// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/ini.h"

namespace pstf::dev {

// Dev autoload (env PSTF_AUTOLOAD=1 or [dev] autoload=1). See autoload.cpp.
bool InstallAutoload(const IniMap& ini);

}  // namespace pstf::dev
