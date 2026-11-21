// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal ini parser (pure). Keys are "section.key", lower-cased; values trimmed.
#pragma once
#include <map>
#include <string>
#include <string_view>

namespace pstf {

using IniMap = std::map<std::string, std::string>;

IniMap ParseIni(std::string_view text);

// "1/true/yes/on" -> true, "0/false/no/off" -> false, anything else (or missing) -> fallback.
bool IniBool(const IniMap& ini, const std::string& key, bool fallback);

}  // namespace pstf
