// SPDX-License-Identifier: GPL-2.0-or-later
// BG3-style layout of the engine's own bottom bar (pure text transform of UI.MENU, unit-tested).
//
// WORLD_ACTIONBAR_LEFT (the PC bottom bar, 1024 x 85, shown on the world screen and on the inventory,
// character, journal... screens alike) holds six portrait groups (background mosaic, portrait button,
// health bar, level-up button; slots 0..5 at x 338..848) and the four "select" buttons (TACTBUTT
// sequences 0..3: weapons, spells, items, special abilities). With the layout on:
//   - the portrait groups move, unchanged, into a menu of their own, PSTF_PORTRAITS, laid out as a
//     column on the left edge of the screen (named items, placed by Lua; 8 slots with the 8-member
//     party: slots 6 and 7 are copies of slot 5's group, renumbered);
//   - the four select buttons are gone (the hotbar shows all of it at once);
//   - the hotbar (menu PSTF_BAR) opens with the bar and sits where the portraits were.
// Anything unexpected (a mod changed the bar) leaves UI.MENU exactly as it was.
#pragma once
#include <string>
#include <vector>

namespace pstf::tbc {

struct MenuItem {
  size_t begin = 0, end = 0;  // [begin, end): from the item keyword's line start to after its '}'
};

// Top-level items of the menu named `menu_name` (by `name "X"` or `name 'X'`); empty if not found.
// Braces inside double-quoted strings do not count.
inline std::vector<MenuItem> MenuItems(const std::string& t, const std::string& menu_name, size_t* menu_end = nullptr) {
  std::vector<MenuItem> out;
  size_t at = t.find("name \"" + menu_name + "\"");
  if (at == std::string::npos) at = t.find("name '" + menu_name + "'");
  if (at == std::string::npos) return out;
  int depth = 1;  // inside the menu's own braces
  bool quoted = false;
  size_t item_start = std::string::npos;
  for (size_t i = at; i < t.size(); ++i) {
    const char c = t[i];
    if (c == '"') quoted = !quoted;
    if (quoted) continue;
    if (c == '{') {
      if (depth == 1) {
        // the item keyword is on the line before its brace
        size_t line = t.rfind('\n', i);
        line = line == std::string::npos ? 0 : t.rfind('\n', line - 1);
        item_start = line == std::string::npos ? 0 : line + 1;
      }
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 1 && item_start != std::string::npos) {
        out.push_back({item_start, i + 1});
        item_start = std::string::npos;
      } else if (depth == 0) {
        if (menu_end) *menu_end = i + 1;
        return out;
      }
    }
  }
  return {};
}

inline bool Has(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

// The portrait part of an item: 0 background, 1 portrait, 2 health bar, 3 level-up; -1 none.
inline int PortraitPart(const std::string& item, int* slot) {
  for (int n = 0; n < 6; ++n) {
    const std::string k = std::to_string(n);
    if (Has(item, "getPortraitBg(" + k + ")")) return *slot = n, 0;
    if (Has(item, "\tportrait " + k + "\n") || Has(item, "\tportrait " + k + "\r")) return *slot = n, 1;
    if (Has(item, "\thealthbar " + k + "\n") || Has(item, "\thealthbar " + k + "\r")) return *slot = n, 2;
    if (Has(item, "Infinity_CanLevelUp(" + k + ")")) return *slot = n, 3;
  }
  return -1;
}

inline bool SelectButton(const std::string& item) {
  if (!Has(item, "\"TACTBUTT\"")) return false;
  for (int q = 0; q < 4; ++q)
    if (Has(item, "sequence " + std::to_string(q) + "\n") || Has(item, "sequence " + std::to_string(q) + "\r")) return true;
  return false;
}

// Lua placing the portrait column: left edge, top to bottom slot 0..PSTF_PARTY_MAX-1, vertically centred
// above the bottom bar (85 px), shrunk to fit when 8 do not (same offsets inside a group as the bar had:
// background 85 x 85 at its corner). pstfPortRect(p) -> x, y, size: where the status icons go.
inline const char* kPortraitLua = R"LUA(
pstfBg3Ok = true
pstfPortParts = {{'Bg', 0, 0, 85, 85}, {'Pic', 6, 4, 74, 58}, {'Hp', 4, 64, 78, 16}, {'Lvl', 69, 67, 10, 10}}
pstfPortW, pstfPortH = -1, -1
function pstfPortRect(p)
	local sw, sh = Infinity_GetScreenSize()
	local n = PSTF_PARTY_MAX or 6
	local g = math.min(85, math.floor((sh - 85 - 8) / n))
	local y0 = math.max(4, math.floor((sh - 85 - n * g) / 2))
	return 4, y0 + p * g, g
end
function pstfPortLayout()
	local sw, sh = Infinity_GetScreenSize()
	if sw == pstfPortW and sh == pstfPortH then return false end
	pstfPortW, pstfPortH = sw, sh
	for n = 0, (PSTF_PARTY_MAX or 6) - 1 do
		local x, y, g = pstfPortRect(n)
		local s = g / 85
		for _, p in ipairs(pstfPortParts) do
			Infinity_SetArea('pstfPort' .. p[1] .. n, x + math.floor(p[2] * s), y + math.floor(p[3] * s),
				math.max(1, math.floor(p[4] * s)), math.max(1, math.floor(p[5] * s)))
		end
	end
	return false
end
)LUA";

// A portrait group of slot 5 made into slot `to` (party slots past the engine bar's six).
inline std::string PortraitForSlot(std::string item, int to) {
  const std::string k = std::to_string(to);
  for (const char* pat : {"getPortraitBg(5)", "getPortraitBgTooltip(5)", "Infinity_CanLevelUp(5)", "Infinity_OnPortraitLClick(5)",
                          "Infinity_OnPortraitRClick(5)", "Infinity_OnPortraitDblClick(5)", "IsPickPartySpriteDead(5)"}) {
    std::string from(pat), repl(pat);
    repl.replace(repl.find("(5)"), 3, "(" + k + ")");
    for (size_t at = item.find(from); at != std::string::npos; at = item.find(from, at + repl.size())) item.replace(at, from.size(), repl);
  }
  for (const std::string kw : {"portrait", "healthbar"}) {
    for (const char* eol : {"\n", "\r"}) {
      const std::string from = "\t" + kw + " 5" + eol, repl = "\t" + kw + " " + k + eol;
      for (size_t at = item.find(from); at != std::string::npos; at = item.find(from, at + repl.size())) item.replace(at, from.size(), repl);
    }
  }
  // conditions on the slot number ("Infinity_GetNumCharacters() > 5", "draggedPortrait ~= 5",
  // "mouseOverPortrait = 5")
  for (const char* pat : {"> 5", "~= 5", "Portrait = 5"}) {
    std::string from(pat), repl(pat);
    repl.replace(repl.size() - 1, 1, k);
    for (size_t at = item.find(from); at != std::string::npos; at = item.find(from, at + repl.size())) item.replace(at, from.size(), repl);
  }
  // any other "(5)" argument in its Lua (e.g. a party helper) follows the slot too
  for (size_t at = item.find("(5)"); at != std::string::npos; at = item.find("(5)", at + 3)) item.replace(at, 3, "(" + k + ")");
  return item;
}

// In the column an empty slot shows nothing (the bar showed an empty socket ornament there): the
// background part (0) gets an `enabled` of its own.
inline std::string OnlyIfFilled(int part, int slot) {
  return part == 0 ? "\n\t\tenabled \"Infinity_GetNumCharacters() > " + std::to_string(slot) + "\"" : std::string();
}

// UI.MENU with the BG3 layout, or `ui` itself (and *why set) when the bar is not as expected.
// party_max > 6: slots 6.. get portrait groups of their own (copies of slot 5's, renumbered).
inline std::string Bg3Layout(const std::string& ui, std::string* why = nullptr, int party_max = 6) {
  size_t menu_end = 0;
  const auto items = MenuItems(ui, "WORLD_ACTIONBAR_LEFT", &menu_end);
  if (items.empty()) {
    if (why) *why = "no WORLD_ACTIONBAR_LEFT menu";
    return ui;
  }
  static const char* kPart[] = {"Bg", "Pic", "Hp", "Lvl"};
  std::string moved;
  std::string slot5[4];
  std::vector<MenuItem> cut;
  int parts = 0, selects = 0;
  for (const auto& it : items) {
    const std::string text = ui.substr(it.begin, it.end - it.begin);
    int slot = -1;
    const int part = PortraitPart(text, &slot);
    if (part >= 0) {
      const size_t brace = text.find('{');
      moved += text.substr(0, brace + 1) + "\n\t\tname \"pstfPort" + kPart[part] + std::to_string(slot) + "\"" +
               OnlyIfFilled(part, slot) + text.substr(brace + 1) + "\n";
      if (slot == 5) slot5[part] = text;
      cut.push_back(it);
      ++parts;
    } else if (SelectButton(text)) {
      cut.push_back(it);
      ++selects;
    }
  }
  if (parts != 24 || selects != 4) {
    if (why) *why = "bottom bar not as expected (" + std::to_string(parts) + " portrait items, " + std::to_string(selects) + " select buttons)";
    return ui;
  }
  for (int s = 6; s < party_max; ++s)
    for (int part = 0; part < 4; ++part) {
      const std::string text = PortraitForSlot(slot5[part], s);
      const size_t brace = text.find('{');
      moved += text.substr(0, brace + 1) + "\n\t\tname \"pstfPort" + kPart[part] + std::to_string(s) + "\"" +
               OnlyIfFilled(part, s) + text.substr(brace + 1) + "\n";
    }
  const std::string open_old = "onOpen \"horribleTimeFrameOffset = 0\"";
  const size_t open_at = ui.find(open_old);
  if (open_at == std::string::npos || open_at > items.front().begin) {
    if (why) *why = "bottom bar open/close hooks not found";
    return ui;
  }
  std::string out;
  size_t from = 0;
  for (const auto& c : cut) {
    out += ui.substr(from, c.begin - from);
    from = c.end;
  }
  out += ui.substr(from);
  // the portrait column and the hotbar go (back) on top of the bar whenever it opens (pstfHudGuard)
  const std::string open_new = "onOpen \"horribleTimeFrameOffset = 0 if pstfHudRaise then pstfHudRaise() end\"";
  out.replace(out.find(open_old), open_old.size(), open_new);
  out += "\n`" + std::string(kPortraitLua) + "`\nmenu\n{\n\tname 'PSTF_PORTRAITS'\n\talign left top\n\tignoreEsc\n\tignoreFocus 1\n"
         "\tlabel\n\t{\n\t\tarea 0 0 1 1\n\t\tenabled \"pstfPortLayout()\"\n\t}\n" + moved + "}\n";
  if (why) why->clear();
  return out;
}

}  // namespace pstf::tbc
