// SPDX-License-Identifier: GPL-2.0-or-later
// The ini as a document (pure): the shipped data/pst_forever.ini is the schema (sections, keys,
// defaults, the comment above each key as its help). Used by the options tool (pst_forever_config.exe)
// and by deploy (--upgrade): a user's ini is rewritten in the shipped layout with the user's values,
// so new keys of a newer build appear with their defaults.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "core/ini.h"

namespace pstf {

enum class IniKind { Bool, Int, Text };

struct IniEntry {
  std::string section;  // lower-case, as ParseIni
  std::string key;      // lower-case
  std::string value;    // default (the template's value)
  std::string help;     // the comment block above the key (or the section's last one), lines joined
  IniKind kind = IniKind::Text;
  std::string Id() const { return section + "." + key; }
};

// Every key of the template in file order, with help and kind.
std::vector<IniEntry> ParseIniEntries(std::string_view text);

// Bool = default 0 or 1, except the keys that only look like it (lighting.debug, ...); Int = a number
// IniInt reads (decimal, 0x hex); else Text.
IniKind ClassifyEntry(std::string_view id, std::string_view default_value);

// A value the module would read as meant: Bool 0/1 (the tool writes these), Int a number IniInt reads.
bool IniValueValid(IniKind kind, std::string_view value);

// The template text with each key's value taken from `values` (ids "section.key") where present.
// Keys in `values` the template lacks are kept, appended in their sections at the end. Line endings,
// comments and the BOM are the template's.
std::string MergeIni(std::string_view tmpl, const IniMap& values);

// Display title of a section ("tbc" -> "Turn-based combat"); unknown -> the name capitalised.
std::string SectionTitle(std::string_view section);

// Baldur.lua (the engine's own options, Documents\Planescape Torment - Enhanced Edition): one
// SetPrivateProfileString('Section','Key','Value') line per key. Get -> the value, or `fallback` when
// the line is missing. Set -> the text with that line's value replaced, or the line appended.
std::string LuaProfileGet(std::string_view text, std::string_view section, std::string_view key,
                          std::string_view fallback);
std::string LuaProfileSet(std::string_view text, std::string_view section, std::string_view key,
                          std::string_view value);

}  // namespace pstf
