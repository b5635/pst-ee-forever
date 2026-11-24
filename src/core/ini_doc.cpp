// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/ini_doc.h"

#include <cctype>
#include <map>
#include <set>

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

// Splits into lines, keeping each line's own ending ("\n", "\r\n" or none for the last).
std::vector<std::string_view> Lines(std::string_view text) {
  std::vector<std::string_view> out;
  while (!text.empty()) {
    size_t nl = text.find('\n');
    size_t n = nl == std::string_view::npos ? text.size() : nl + 1;
    out.push_back(text.substr(0, n));
    text.remove_prefix(n);
  }
  return out;
}

std::string_view Body(std::string_view line) {  // without the line ending
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
  return line;
}

bool IsComment(std::string_view t) { return !t.empty() && (t[0] == ';' || t[0] == '#'); }

std::string_view CommentText(std::string_view t) {
  t.remove_prefix(1);
  return Trim(t);
}

// Keys whose default is 0/1 but which are numbers, not switches.
const std::set<std::string, std::less<>> kNotBool = {
    "lighting.debug",     // debug view 0..8
    "smooth.frame_rate",  // 0 = display refresh rate
    "locks.retry_hours",  // hours
};

}  // namespace

IniKind ClassifyEntry(std::string_view id, std::string_view default_value) {
  if ((default_value == "0" || default_value == "1") && kNotBool.find(id) == kNotBool.end())
    return IniKind::Bool;
  IniMap probe{{"x", std::string(default_value)}};
  if (IniInt(probe, "x", 0) == IniInt(probe, "x", 1)) return IniKind::Int;
  return IniKind::Text;
}

bool IniValueValid(IniKind kind, std::string_view value) {
  if (kind == IniKind::Bool) return value == "0" || value == "1";
  if (kind == IniKind::Int) {
    IniMap probe{{"x", std::string(Trim(value))}};
    return IniInt(probe, "x", 0) == IniInt(probe, "x", 1);
  }
  return value.find_first_of(";\r\n") == std::string_view::npos;  // ParseIni cuts at ';'
}

std::vector<IniEntry> ParseIniEntries(std::string_view text) {
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) text.remove_prefix(3);
  std::vector<IniEntry> out;
  std::string section, pending, last_help;
  for (std::string_view raw : Lines(text)) {
    std::string_view t = Trim(Body(raw));
    if (t.empty()) continue;
    if (IsComment(t)) {
      if (section.empty()) continue;  // the file's header comment
      if (!pending.empty()) pending += ' ';
      pending += std::string(CommentText(t));
      continue;
    }
    if (t.front() == '[') {
      size_t end = t.find(']');
      section = Lower(Trim(t.substr(1, end == std::string_view::npos ? t.size() - 1 : end - 1)));
      pending.clear();
      last_help.clear();
      continue;
    }
    size_t eq = t.find('=');
    if (eq == std::string_view::npos) continue;
    std::string_view value = t.substr(eq + 1);
    size_t cmt = value.find(';');
    if (cmt != std::string_view::npos) value = value.substr(0, cmt);
    IniEntry e;
    e.section = section;
    e.key = Lower(Trim(t.substr(0, eq)));
    e.value = std::string(Trim(value));
    if (!pending.empty()) last_help = pending;
    pending.clear();
    e.help = last_help;
    e.kind = ClassifyEntry(e.Id(), e.value);
    out.push_back(std::move(e));
  }
  return out;
}

std::string MergeIni(std::string_view tmpl, const IniMap& values) {
  std::string out;
  std::set<std::string> used;
  std::string section;
  std::string eol = "\n";
  for (std::string_view raw : Lines(tmpl)) {
    std::string_view body = Body(raw);
    if (body.size() < raw.size()) eol = std::string(raw.substr(body.size()));
    std::string_view t = Trim(body);
    if (!t.empty() && t.front() == '[') {
      size_t end = t.find(']');
      section = Lower(Trim(t.substr(1, end == std::string_view::npos ? t.size() - 1 : end - 1)));
    } else if (!t.empty() && !IsComment(t)) {
      size_t eq = body.find('=');
      if (eq != std::string_view::npos) {
        std::string id = section + "." + Lower(Trim(body.substr(0, eq)));
        auto it = values.find(id);
        if (it != values.end()) {
          used.insert(id);
          std::string_view rest = body.substr(eq + 1);
          size_t cmt = rest.find(';');
          std::string_view tail;
          if (cmt != std::string_view::npos) {  // keep an inline comment and the spaces before it
            size_t s = cmt;
            while (s > 0 && std::isspace(static_cast<unsigned char>(rest[s - 1]))) --s;
            tail = rest.substr(s);
          }
          out.append(body.substr(0, eq + 1));
          out += it->second;
          out.append(tail);
          out.append(raw.substr(body.size()));
          continue;
        }
      }
    }
    out.append(raw);
  }
  // Keys the template does not know (an older or newer build's, or the user's own): kept.
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> extra;
  for (const auto& [id, v] : values) {
    if (used.count(id)) continue;
    size_t dot = id.find('.');
    extra[id.substr(0, dot)].push_back({dot == std::string::npos ? id : id.substr(dot + 1), v});
  }
  if (!extra.empty()) {
    if (!out.empty() && out.back() != '\n') out += eol;
    for (const auto& [sec, kv] : extra) {
      out += eol + "[" + sec + "]" + eol + "; kept from the previous pst_forever.ini (not in this build)" + eol;
      for (const auto& [k, v] : kv) out += k + "=" + v + eol;
    }
  }
  return out;
}

std::string SectionTitle(std::string_view section) {
  static const std::map<std::string, std::string, std::less<>> titles = {
      {"voice", "Voice"},          {"tbc", "Turn-based combat"},  {"smooth", "Smooth motion"},
      {"dev", "Developer"},        {"lighting", "Lighting"},      {"gore", "Gore"},
      {"party", "8-member party"}, {"rolls", "Roll panel"},       {"dialogue", "Dialogue rolls"},
      {"locks", "Lock rolls"},
  };
  auto it = titles.find(section);
  if (it != titles.end()) return it->second;
  std::string s(section);
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}

namespace {

// SetPrivateProfileString('S','K','V'): the three quoted arguments, or false.
bool ParseProfileLine(std::string_view line, std::string_view args[3], size_t* value_begin,
                      size_t* value_end) {
  static constexpr std::string_view kFn = "SetPrivateProfileString(";
  size_t p = line.find(kFn);
  if (p == std::string_view::npos) return false;
  p += kFn.size();
  for (int i = 0; i < 3; ++i) {
    while (p < line.size() && (line[p] == ' ' || line[p] == ',')) ++p;
    if (p >= line.size() || (line[p] != '\'' && line[p] != '"')) return false;
    const char q = line[p++];
    size_t e = line.find(q, p);
    if (e == std::string_view::npos) return false;
    args[i] = line.substr(p, e - p);
    if (i == 2) {
      *value_begin = p;
      *value_end = e;
    }
    p = e + 1;
  }
  return true;
}

bool SameName(std::string_view a, std::string_view b) { return Lower(a) == Lower(b); }

}  // namespace

std::string LuaProfileGet(std::string_view text, std::string_view section, std::string_view key,
                          std::string_view fallback) {
  std::string found(fallback);
  for (std::string_view raw : Lines(text)) {
    std::string_view a[3];
    size_t b, e;
    if (ParseProfileLine(Body(raw), a, &b, &e) && SameName(a[0], section) && SameName(a[1], key))
      found = std::string(a[2]);  // the engine applies them in order: the last one wins
  }
  return found;
}

std::string LuaProfileSet(std::string_view text, std::string_view section, std::string_view key,
                          std::string_view value) {
  std::string out;
  bool done = false;
  std::string eol = "\n";
  for (std::string_view raw : Lines(text)) {
    std::string_view body = Body(raw);
    if (body.size() < raw.size()) eol = std::string(raw.substr(body.size()));
    std::string_view a[3];
    size_t b, e;
    if (ParseProfileLine(body, a, &b, &e) && SameName(a[0], section) && SameName(a[1], key)) {
      out.append(body.substr(0, b));
      out.append(value);
      out.append(raw.substr(e));
      done = true;
      continue;
    }
    out.append(raw);
  }
  if (!done) {
    if (!out.empty() && out.back() != '\n') out += eol;
    out += "SetPrivateProfileString('" + std::string(section) + "','" + std::string(key) + "','" +
           std::string(value) + "')" + eol;
  }
  return out;
}

}  // namespace pstf
