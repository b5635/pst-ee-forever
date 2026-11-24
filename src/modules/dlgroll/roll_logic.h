// SPDX-License-Identifier: GPL-2.0-or-later
// Dialogue ability rolls: the rules and the panel's timeline, pure (no engine access).
//
// A reply the engine hides only because the Protagonist's ability is too low (STR, DEX, CON, INT,
// WIS, CHR in a CheckStat/CheckStatGT) is shown obscured and can be rolled for once per node:
//   DC = max(10, (needed - 10) * 2), modifier = floor((ability - 10) / 2),
//   success = d20 + modifier >= DC; a natural 20 always succeeds, a natural 1 always fails.
// On success the node's replies are evaluated by the engine again as if the ability were `needed`.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pstf::dlgroll {

// STATS.IDS ability ids (the CDerivedStats::GetAtOffset cases 36..42).
constexpr int kStatStr = 36, kStatInt = 38, kStatWis = 39, kStatDex = 40, kStatCon = 41, kStatChr = 42;
constexpr int kMaxAbility = 25;

struct Ability {
  int stat;
  const char* ids;   // STATS.IDS name in scripts
  const char* name;  // shown on the panel
  int card;          // PFDCARD.BAM ability index (STR DEX CON INT WIS CHA)
};
inline const Ability kAbilities[] = {{kStatStr, "STR", "Strength", 0},     {kStatDex, "DEX", "Dexterity", 1},
                                     {kStatCon, "CON", "Constitution", 2}, {kStatInt, "INT", "Intelligence", 3},
                                     {kStatWis, "WIS", "Wisdom", 4},       {kStatChr, "CHR", "Charisma", 5}};
inline const Ability* FindAbility(int stat) {
  for (const auto& a : kAbilities)
    if (a.stat == stat) return &a;
  return nullptr;
}

inline int Dc(int needed) {
  const int d = (needed - 10) * 2;
  return d < 10 ? 10 : d;
}

inline int Modifier(int ability) {
  const int x = ability - 10;
  return x >= 0 ? x / 2 : -((-x + 1) / 2);  // floor
}

enum class Outcome { Fail, Success, CritFail, CritSuccess };
inline Outcome Resolve(int d20, int modifier, int dc) {
  if (d20 >= 20) return Outcome::CritSuccess;
  if (d20 <= 1) return Outcome::CritFail;
  return d20 + modifier >= dc ? Outcome::Success : Outcome::Fail;
}
inline bool Succeeded(Outcome o) { return o == Outcome::Success || o == Outcome::CritSuccess; }

// Abilities a reply condition raises a bar on: CheckStat/CheckStatGT(<any object>, N, ABILITY), in order of
// first appearance, each once. CheckStatLT never asks for more of an ability, so it is not one.
inline std::vector<int> CheckedAbilities(const std::string& cond) {
  std::vector<int> out;
  auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); };
  std::string s;
  s.reserve(cond.size());
  for (char c : cond)
    if (c != ' ' && c != '\t') s += lower(c);
  size_t p = 0;
  while ((p = s.find("checkstat", p)) != std::string::npos) {
    size_t q = p + 9;
    if (s.compare(q, 2, "lt") == 0) {
      p = q;
      continue;
    }
    if (s.compare(q, 2, "gt") == 0) q += 2;
    if (q >= s.size() || s[q] != '(') {
      p = q;
      continue;
    }
    const size_t close = s.find(')', q);
    if (close == std::string::npos) break;
    const std::string args = s.substr(q + 1, close - q - 1);
    const size_t last = args.rfind(',');
    if (last != std::string::npos) {
      const std::string name = args.substr(last + 1);
      for (const auto& a : kAbilities) {
        std::string ids = a.ids;
        for (auto& c : ids) c = lower(c);
        if (name == ids) {
          bool seen = false;
          for (int x : out) seen = seen || x == a.stat;
          if (!seen) out.push_back(a.stat);
        }
      }
    }
    p = close;
  }
  return out;
}

// The smallest ability value in (current, kMaxAbility] for which `holds(value)` is true, or -1.
template <class Fn>
int NeededValue(int current, Fn holds) {
  for (int v = current + 1; v <= kMaxAbility; ++v)
    if (holds(v)) return v;
  return -1;
}

// Obscured reply text: every letter and digit becomes a dot, so the row keeps its shape and length;
// spaces, punctuation and UTF-8 sequences other than letters stay where they are (a multi-byte
// character counts as one letter). `dot` is the replacement (UTF-8).
inline std::string Obscure(const std::string& text, const std::string& dot = "\xC2\xB7") {
  std::string out;
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '^') {  // engine colour codes (^0xAARRGGBB, ^-) pass through untouched
      if (i + 1 < text.size() && text[i + 1] == '-') {
        out += "^-";
        i += 2;
        continue;
      }
      if (i + 10 < text.size() + 1 && text.compare(i + 1, 2, "0x") == 0) {
        out += text.substr(i, 11);
        i += 11;
        continue;
      }
    }
    if (c >= 0x80) {
      size_t n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
      out += dot;
      i += n;
      continue;
    }
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    out += alnum ? dot : std::string(1, static_cast<char>(c));
    ++i;
  }
  return out;
}

// The roll panel's timeline (ms after the Roll click), taken from the BG3 reference clip and the two
// result sounds (check_success.wav / check_failed.wav start at the throw and share the first 5 s):
// the die leaves at once, lands at ~1.85 s (the sound's landing hit at 2.0 s), shows its natural
// number, the ability bonus lights up at 3.0 s and flies into the die, the die shows the total at
// 4.3 s (the sound's bonus cue at 4.4 s), the bonus row fades at 5.0 s, the die pops at the result
// hit (success 5.3 s, failure 5.15 s), the banner follows (success 6.25 s, failure 7.0 s, the failure
// sound's big hit) and Continue appears 0.4 s after it.
struct RollTiming {
  static constexpr uint32_t kLand = 1850, kNatural = 2000, kBonusLit = 3000, kBonusFly = 3400, kTotal = 4300,
                            kBonusFade = 5000, kPopOk = 5250, kPopFail = 5100, kBannerOk = 6250,
                            kBannerFail = 7000, kContinueAfter = 400, kPop = 360;
  static uint32_t Pop(bool ok) { return ok ? kPopOk : kPopFail; }
  static uint32_t Banner(bool ok) { return ok ? kBannerOk : kBannerFail; }
  static uint32_t Continue(bool ok) { return Banner(ok) + kContinueAfter; }
};

// PFD20.BAM frame for a resting die showing n (clamped to -9..40) at pop scale s (0..2).
inline int RestFrame(int n, int s) {
  if (n < -9) n = -9;
  if (n > 40) n = 40;
  if (s < 0) s = 0;
  if (s > 2) s = 2;
  return 24 + s * 50 + (n + 9);
}

// UI.MENU text: `item` added as the last item of menu `menu` (name 'X'), or `ui` unchanged when there is no
// such menu. The menu's own closing brace is found by counting braces from its opening one, skipping
// "..." strings (Lua action text): a `...` Lua chunk may follow the menu, so the last '}' before the next
// menu is not it.
inline std::string InsertMenuItem(const std::string& ui, const std::string& menu, const std::string& item) {
  const size_t at = ui.find("name '" + menu + "'");
  if (at == std::string::npos) return ui;
  const size_t open = ui.rfind('{', at);
  if (open == std::string::npos) return ui;
  int depth = 0;
  bool quoted = false;
  for (size_t p = open; p < ui.size(); ++p) {
    const char c = ui[p];
    if (c == '"') quoted = !quoted;
    if (quoted) continue;
    if (c == '{') ++depth;
    if (c == '}' && --depth == 0) return ui.substr(0, p) + item + ui.substr(p);
  }
  return ui;
}

}  // namespace pstf::dlgroll
