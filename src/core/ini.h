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

// Whole number, decimal or 0x-prefixed (end_turn_key=0x23); anything else (or missing) -> fallback.
int IniInt(const IniMap& ini, const std::string& key, int fallback);

}  // namespace pstf
