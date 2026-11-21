// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/ini.h"

#include <cctype>

namespace pstf {
namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

std::string Lower(std::string_view s) {
  std::string r(s);
  for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

}  // namespace

IniMap ParseIni(std::string_view text) {
  IniMap out;
  std::string section;
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) text.remove_prefix(3);  // BOM
  while (!text.empty()) {
    size_t nl = text.find('\n');
    std::string_view line = Trim(text.substr(0, nl));
    text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    if (line.empty() || line[0] == ';' || line[0] == '#') continue;
    if (line.front() == '[') {
      size_t end = line.find(']');
      section = Lower(Trim(line.substr(1, end == std::string_view::npos ? line.size() - 1 : end - 1)));
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    std::string_view value = line.substr(eq + 1);
    size_t cmt = value.find(';');
    if (cmt != std::string_view::npos) value = value.substr(0, cmt);
    out[section + "." + Lower(Trim(line.substr(0, eq)))] = std::string(Trim(value));
  }
  return out;
}

bool IniBool(const IniMap& ini, const std::string& key, bool fallback) {
  auto it = ini.find(key);
  if (it == ini.end()) return fallback;
  const std::string v = Lower(it->second);
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  return fallback;
}

}  // namespace pstf
