// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/ini.h"

namespace pstf {

// Loads <game>/pst_forever/pst_forever.ini. Missing file = empty map = every module off.
const IniMap& Config();

}  // namespace pstf
