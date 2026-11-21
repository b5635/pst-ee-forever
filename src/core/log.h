// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace pstf {

// Opens <game>/pst_forever/logs/pst_forever.log (truncated each start).
void LogOpen();
void Log(const char* fmt, ...);

// <game>/pst_forever (no trailing slash), UTF-8.
const char* DataDir();

}  // namespace pstf
