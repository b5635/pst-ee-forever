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

namespace pstf::tbc {

constexpr int kHudCards = 12;
constexpr int kHudBar = 48;   // hotbar buttons (4 rows x up to 12 engine action-bar slots)
constexpr int kHudDots = 0;  // the line is drawn in the world pass now (engine DrawLine2d)

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
  std::string msg;             // short notice ("No action left this turn")
  std::vector<int> dots;       // x,y pairs in UI coordinates, reachable
  std::vector<int> red_dots;   // x,y pairs, beyond the movement left
  int path_ft = 0;             // whole path length, shown at its end
  int label_x = -1, label_y = -1;
  // Hover card (BG3): the creature under the cursor, its HP and, for a foe, the chance to hit it.
  bool hover = false;
  int32_t hover_name_ref = -1;
  std::string hover_name;
  int hover_hp = 0, hover_max = 0, hover_chance = -1, hover_ac = 0, hover_needs = 0;
  bool hover_foe = false;
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

std::string BuildHudMenu();             // menu PSTF_TBC + its Lua helpers
std::string BuildHudLua(const HudView& v);  // "pstfTbc = {...}"

}  // namespace pstf::tbc
