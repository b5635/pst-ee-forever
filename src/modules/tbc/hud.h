// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based HUD, BG3 style, drawn by the engine's own .menu UI (pure text builders, unit-tested).
//   - top: the initiative bar, one card per combatant with its bestiary picture (the journal's own
//     `bestiary` table, matched on the animation's bestiary index), HP bar, active card larger with
//     a gold frame, cards that already acted this round dimmed;
//   - bottom: the active party member's action pip, movement bar "23 / 31 ft" and End Turn;
//   - in the world: the movement line, dots along the engine's walk path, gold while reachable, red
//     beyond the movement left, with the path length at its end.
// The menu text is appended to UI.MENU at load (engine/menu.h). Every frame of a fight the module
// sets the Lua table `pstfTbc` (BuildHudLua) and calls pstfTbcLayout(), which places the elements.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "modules/tbc/status.h"

namespace pstf::tbc {

constexpr int kHudCards = 12;
constexpr int kHudBar = 48;   // hotbar buttons (engine bar: all at once above it, up to 48)
constexpr int kBarCols = 10, kBarRows = 2;  // BG3 layout: a page of the hotbar in the bottom bar
constexpr int kStatusSlots = 8;  // party slots with status icons next to the portrait
constexpr int kStatusIcons = 8;  // icons per portrait (2 rows of 4)
constexpr int kHoverIcons = 8;   // icons on the hover card
constexpr int kStatusPortraitPx = 26, kStatusHoverPx = 30, kStatusFramePx = 2;
constexpr int kHudDots = 0;  // the line is drawn in the world pass now (engine DrawLine2d)
constexpr int kHudFloats = 16;  // floating text over creatures ("Initiative 7")

struct HudFloat {
  int x = 0, y = 0;  // UI coordinates of the text centre
  std::string text;
};

struct HudCard {
  int32_t id = -1;
  int32_t bestiary = -1;
  int32_t name_ref = -1;
  std::string name;  // fallback when name_ref is -1
  int hp = 0, max_hp = 0;
  bool foe = false, active = false, done = false;
};

struct HudView {
  bool on = false;
  uint32_t round = 0;
  std::vector<HudCard> cards;  // initiative order
  bool party_turn = false;
  int slot = -1;               // party slot whose turn it is (-1: not a party turn)
  double move_ft = 0, max_ft = 0;
  bool action_used = false, acting = false;
  int attacks_left = -1;       // swings left this round (-1: not known)
  int attacks_round = 0;       // swings this round (APR, alternating for 1.5 etc.)
  std::vector<HudFloat> floats;
  std::string msg;             // short notice ("No action left this turn")
  std::vector<int> dots;       // x,y pairs in UI coordinates, reachable
  std::vector<int> red_dots;   // x,y pairs, beyond the movement left
  int path_ft = 0;             // whole path length, shown at its end
  int label_x = -1, label_y = -1;
  bool provokes = false;       // the previewed walk leaves a foe's reach: "Provokes attack of opportunity" under it
  // Dice panel: the last attack roll or saving throw.
  bool dice = false;
  int dice_kind = 0;           // 0 attack, 1 saving throw
  int32_t dice_actor_ref = -1, dice_target_ref = -1;  // name strrefs (-1: use the strings)
  std::string dice_actor, dice_target;
  int dice_face = 0;           // face shown now (tumbling, then the roll)
  bool dice_landed = false, dice_verdict = false, dice_good = false;
  std::string dice_sum;        // "17 + 2 = 19  vs 14"
  std::string dice_result;     // "Hit" / "Miss" / "Saved" / "Failed"
  double dice_alpha = 0;
};

// A status icon as the menu shows it: the BAM, its fitted rect and the drained frame (offsets inside
// the icon box), the source's name and the rounds left.
struct StatusIconView {
  std::string parent;  // the spell or item resref the icon stands for
  std::string bam;
  int32_t name_ref = -1;
  int rounds = 0;
  bool debuff = false;
  Rect fit;
  std::vector<Rect> frame;
};
StatusIconView MakeIconView(const StatusIcon& s, const std::string& bam, int32_t name_ref, int bam_w, int bam_h, int box);

// Hover card (BG3): the creature under the cursor, its HP, buffs/debuffs and, in a fight, for a foe,
// the chance that the active party member hits it ("80%" next to the cursor, on a party turn while not aiming).
struct HoverView {
  bool on = false;
  int32_t name_ref = -1;
  std::string name;
  int hp = 0, max_hp = 0, chance = -1, ac = 0, needs = 0;
  bool foe = false, fight = false;
  bool aim = false;            // a party member's turn, plain (attack) cursor: the chance shows at the cursor
  int cur_x = -1, cur_y = -1;  // the cursor in UI coordinates
  int px_w = 0;                // the game window's width in screen px (UI px differ with Scale UI; 0 = same)
  int level = 0;               // "Lv. 5" after the name (0: not shown)
  std::string sub;             // the line under the name: class or stance (hover.h HoverSubtitle)
  std::vector<StatusIconView> icons;
};
// "pstfStat = {...} pstfHov = {...}"; party[slot] = that slot's icons.
std::string BuildStatusLua(const std::vector<std::vector<StatusIconView>>& party, const HoverView& h);

// menus PSTF_TBC (fight overlay) and PSTF_BAR (hotbar) + their Lua helpers. `bg3`: the hotbar sits in
// the bottom bar where the portraits were (they move to the right edge, ui_layout.h), and stays there
// out of fights too; else it shows in fights only, above the turn panel.
// menu PSTF_STATUS (status icons next to the portraits + hover card), shown with the bottom bar.
// pstfHudGuard() (called every frame) keeps our menus on the stack exactly when they belong there.
std::string BuildHudMenu(bool bg3 = false, int party_max = 6);
// UI menus PSTF_SAVE1..N: the saving-throw banners (save_banner.h), fed by PSTF_SaveBanner(i).
std::string BuildSaveBannerMenu();
// UI menus PSTF_CT1..N: the combat text lines (combat_text.h), fed by PSTF_CombatText(i).
std::string BuildCombatTextMenu();
std::string BuildHudLua(const HudView& v);  // "pstfTbc = {...}"

}  // namespace pstf::tbc
