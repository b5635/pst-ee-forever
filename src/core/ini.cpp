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

// Whole number, decimal or 0x-prefixed; anything else (or missing) is the fallback.
int IniInt(const IniMap& ini, const std::string& key, int fallback) {
  auto it = ini.find(key);
  if (it == ini.end() || it->second.empty()) return fallback;
  const std::string& v = it->second;
  const bool neg = v[0] == '-';
  const size_t i = neg ? 1 : 0;
  const bool hex = v.size() > i + 1 && v[i] == '0' && (v[i + 1] == 'x' || v[i + 1] == 'X');
  const int base = hex ? 16 : 10;
  long long n = 0;
  bool any = false;
  for (size_t k = i; k < v.size(); ++k) {
    const char c = v[k];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else if (hex && (c == 'x' || c == 'X') && k == i + 1) continue;
    else return fallback;
    if (d >= base) return fallback;
    if (n > (0x7FFFFFFFll + 1) / base) return fallback;
    n = n * base + d;
    any = true;
  }
  if (!any || n > 0x7FFFFFFFll || n < -(0x7FFFFFFFll + 1)) return fallback;
  return static_cast<int>(v[0] == '-' ? -n : n);
}

}  // namespace pstf
