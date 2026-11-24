// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter for dialogue replies (symbols.h "Dialogue ability rolls"). Game thread only.
//
// CGameDialogEntry::Display is hooked: after the engine has written a node (NPC line + the replies whose
// condition holds) the callback runs. Replies are evaluated with the engine's own CGameDialogReply::Hold,
// optionally with one Protagonist ability set to another value for that call only (the stat struct the
// CheckStat triggers read; the old value is back before the call returns). Nothing is kept across frames:
// the node being shown is found again from the world screen's dialogue each time.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pstf::engine {

struct DialogReply {
  int index = 0;         // position in the node (the marker Lua's reply rows carry)
  uint32_t flags = 0;    // 1 text, 2 condition, 0x20 listed by Display
  int32_t strref = -1;   // reply text
  int number = -1;       // number byte the engine gave it (-1 hidden)
  std::string cond;      // condition source text ("" without a condition)
};

using DialogShownFn = void (*)();
// Installs the Display hook (all-or-nothing). The callback runs after every node is shown.
bool InstallDialogHooks(DialogShownFn shown);

// True while the world screen has a dialogue node up.
bool DialogActive();
// The node on screen: an id that changes with every Display call (0 = none) and its replies.
uint32_t DialogNodeSerial();
std::vector<DialogReply> DialogReplies();
// The reply number colour the engine uses for this dialogue (0x00BBGGRR) and the text colour.
uint32_t DialogNumberColor();
uint32_t DialogTextColor();

// CGameDialogReply::Hold for reply `index` of the node on screen, evaluated for the sprite the node was
// shown for, with the Protagonist's ability `stat` (STATS.IDS 36..42) set to `value` for the call (stat 0 =
// as it is). -1 when the node or the sprite is gone.
int DialogReplyHolds(int index, int stat = 0, int value = 0);
// Same with several abilities set at once ({stat, value} pairs).
int DialogReplyHoldsWith(int index, const std::vector<std::pair<int, int>>& set);
// Sets the number byte of reply `index` (what the number keys and the engine's own list use).
void SetDialogReplyNumber(int index, int number);

// The Protagonist's ability as CheckStat reads it now (-1 if unreadable).
int ProtagonistAbility(int stat);

}  // namespace pstf::engine
