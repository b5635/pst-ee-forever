// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace pstf {

// Lower-case hex SHA-256 of a file, or "" on error.
std::string Sha256File(const wchar_t* path);

// True if the running Torment.exe on disk is the supported build (GOG 3.2.2.0 x64).
bool IsSupportedExe();

}  // namespace pstf
