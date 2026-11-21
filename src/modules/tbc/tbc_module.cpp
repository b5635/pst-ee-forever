// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based combat in the running game.
//
// How it works (see docs/TBC_RESEARCH.md §8):
// - A fight starts when a hostile (EA >= 200) comes within `start_radius` of a party member
//   (census.h). Party members, allies and hostiles near the party join; the Director rolls
//   initiative once.
// - Only the creature whose turn it is runs its CGameSprite::ProcessAI (scripts + actions) and
//   AIUpdateWalk. Every other combatant is held: the vtable slot for ProcessAI and the two walk calls
//   in AIUpdate skip it. Its animation, effects and incoming triggers keep running, and creatures
//   outside the fight are untouched (the world keeps running).
// - The active creature acts with the engine's own rules and timing for one round of its own
//   activity (turn_clock.h): ROUND_LENGTH ticks of busy time; AI turns end when the creature goes idle;
//   party turns wait for orders and end on the budget or the End Turn key.
// - At the end of its turn a creature is stopped cleanly with the engine's ClearAI(0) + DropPath, so
//   nothing it was doing leaks into others' turns; triggers raised while it waits survive.
// - The fight ends when one side is gone (dead, fled beyond `leave_radius`, left the area), on an
//   area change, or when the world screen goes away; then nobody is held.
#include "modules/tbc/tbc_module.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/patch.h"
#include "core/tick.h"
#include "engine/lua.h"
#include "engine/menu.h"
#include "engine/render.h"
#include "engine/screens.h"
#include "engine/sprites.h"
#include "engine/symbols.h"
#include "engine/world.h"
#include "modules/dev/lua_bridge.h"
#include "modules/tbc/census.h"
#include "modules/tbc/director.h"
#include "modules/tbc/economy.h"
#include "modules/tbc/hud.h"
#include "modules/tbc/path_line.h"
#include "modules/tbc/path_preview.h"
#include "modules/tbc/turn_clock.h"

namespace pstf::tbc {
namespace {

using SpriteFn = void(__fastcall*)(void*);
SpriteFn g_process_ai = nullptr;
SpriteFn g_walk = nullptr;

struct State {
  bool running = false;
  std::unique_ptr<Director> dir;
  std::vector<int32_t> in;        // ids in the fight
  std::vector<int32_t> party;     // party ids (last scan)
  int32_t current = -1;           // whose ProcessAI may run
  TurnClock clock;                // kept for the log line format (busy ticks)
  Economy econ;                   // the active creature's action + movement
  bool hold_current = false;      // its movement is spent: no AIUpdateWalk
  int last_x = 0, last_y = 0;     // its position last frame
  double move_feet = 31.0;        // [tbc] move_feet
  bool hud = false;               // [tbc] hud: the menu was added to UI.MENU
  std::vector<Pt> line;           // movement line in world pixels: creature, then path points
  bool line_preview = false;      // hover preview (not yet ordered) vs the engine's own path
  Pt preview_goal{-1, -1}, preview_start{-1, -1};  // cells of the cached preview
  std::vector<Pt> preview_cells;
  bool draw = false;              // the world overlay hook is installed
  Pt preview_at{-1, -1};          // test probe PSTF_PreviewAt: use this world point as the cursor
  bool unreachable = false;       // the hovered spot cannot be walked to: a red cross there
  Pt cursor{0, 0};
  bool logged_preview = false;
  std::string notice;             // HUD notice and when it expires (world ticks)
  uint32_t notice_until = 0;
  uint32_t last_time = 0;
  bool have_time = false;
  uint64_t area = 0;              // party area identity (compare only)
  uint32_t round_ticks = kRoundTicksPst;
  Radii radii;
  int end_turn_vk = VK_END;
  bool key_down = false;
  bool end_requested = false;
  bool auto_party = false;        // party turns: attack nearest enemy, end when idle
  uint32_t fights = 0;
} g;

bool Contains(const std::vector<int32_t>& v, int32_t id) {
  return std::find(v.begin(), v.end(), id) != v.end();
}

bool Held(int32_t id) { return g.running && id != g.current && Contains(g.in, id); }

void __fastcall HookProcessAI(void* sprite) {
  if (Held(engine::SpriteIdOf(sprite))) return;
  g_process_ai(sprite);
}
void __fastcall HookWalk(void* sprite) {
  const int32_t id = engine::SpriteIdOf(sprite);
  if (Held(id)) return;
  if (g.running && id == g.current && g.hold_current) return;  // movement spent this turn
  g_walk(sprite);
}

void Say(const std::string& text) {
  Log("tbc: %s", text.c_str());
  const std::string err =
      engine::LuaDoString(("Infinity_DisplayString(" + engine::LuaQuote(text) + ")").c_str());
  if (!err.empty()) Log("tbc: display failed: %s", err.c_str());
}

int PartySlot(int32_t id) {
  auto it = std::find(g.party.begin(), g.party.end(), id);
  return it == g.party.end() ? -1 : static_cast<int>(it - g.party.begin());
}

std::string Who(int32_t id) {
  const Combatant* c = g.dir ? g.dir->Find(id) : nullptr;
  char b[64];
  const int slot = PartySlot(id);
  if (slot >= 0) {
    std::snprintf(b, sizeof b, "party member %d", slot + 1);
  } else if (c && c->r.side == Side::Party) {
    std::snprintf(b, sizeof b, "ally %X", static_cast<unsigned>(id));
  } else {
    std::snprintf(b, sizeof b, "enemy %X", static_cast<unsigned>(id));
  }
  return b;
}

void EndFight(const char* why) {
  if (!g.running) return;
  Log("tbc: fight %u over (%s) after %u rounds", g.fights, why, g.dir ? g.dir->Round() : 0);
  g.running = false;
  g.in.clear();
  g.current = -1;
  g.dir.reset();
  g.hold_current = false;
  g.line.clear();
  if (!g.auto_party) {
    // Give the party its own AI scripts back if the fight turned them off.
    const std::string e = engine::LuaDoString("if pstfAiRestore then game:ToggleAI() pstfAiRestore = false end");
    if (!e.empty()) Log("tbc: party AI restore failed: %s", e.c_str());
  }
  if (g.hud) {
    const std::string err = engine::LuaDoString(
        "pstfTbc = {on=false,cards={},dots={},rdots={}} if pstfTbcOpen then Infinity_PopMenu('PSTF_TBC') end");
    if (!err.empty()) Log("tbc: hud close failed: %s", err.c_str());
  }
  Say("Combat over: back to real time");
}

Recruit ToRecruit(const Seen& s) {
  const bool party_side = s.party || IsFriendlyEa(s.ea);
  return {s.id, party_side ? Side::Party : Side::Hostile, 0, 2, 0};
}

void StartFight(const std::vector<Seen>& join, uint32_t now) {
  std::vector<Recruit> who;
  for (const auto& s : join) who.push_back(ToRecruit(s));
  g.dir = std::make_unique<Director>(GetTickCount() | 1u, g.round_ticks);
  g.dir->Start(who, now);
  g.in.clear();
  for (const auto& r : who) {
    g.in.push_back(r.id);
    engine::DropPath(r.id);  // nobody keeps walking into someone else's turn
  }
  g.running = g.dir->Running();
  g.current = -1;
  ++g.fights;
  std::string order;
  for (int32_t id : g.dir->Order()) order += (order.empty() ? "" : ", ") + Who(id);
  Log("tbc: fight %u starts, %zu combatants, order: %s", g.fights, who.size(), order.c_str());
  if (!g.auto_party) {
    // The party's AI scripts would attack on their own (retaliation, "attack nearest") whenever their
    // turn comes: in turn-based combat the player gives every order. The engine's own Toggle AI.
    const std::string e = engine::LuaDoString(
        "pstfAiRestore = false if game:GetPartyAI() then game:ToggleAI() pstfAiRestore = true end "
        "PSTF_Note('party AI on: ' .. tostring(game:GetPartyAI()) .. ', will restore: ' .. tostring(pstfAiRestore))");
    if (!e.empty()) Log("tbc: party AI off failed: %s", e.c_str());
  }
  Say("Combat! Turn-based. End turn: End key");
}

void Scan(uint32_t now) {
  g.party = engine::PartyIds();
  if (g.party.empty()) return;
  engine::SpriteInfo leader;
  if (!engine::GetSprite(g.party[0], &leader)) return;
  if (g.running && leader.area != g.area) {
    EndFight("area changed");
  }
  g.area = leader.area;

  std::vector<Seen> seen;
  for (const auto& s : engine::AllSprites()) {
    if (s.area != g.area) continue;
    seen.push_back({s.id, Contains(g.party, s.id), s.ea, s.alive, s.x, s.y});
  }
  const CensusOut c = Census(seen, g.in, g.running, g.radii);
  if (!g.running) {
    if (c.start) StartFight(c.join, now);
    return;
  }
  for (int32_t id : c.leave) {
    engine::SpriteInfo o;
    const bool there = engine::GetSprite(id, &o) && o.area == g.area;
    Log("tbc: %s leaves the fight (%s, hp %d)", Who(id).c_str(),
        !there ? "left the area" : !o.alive ? "dead" : "out of range", there ? o.hp : 0);
    g.dir->Remove(id);
    g.in.erase(std::remove(g.in.begin(), g.in.end(), id), g.in.end());
  }
  for (const auto& s : c.join) {
    if (!g.dir->Running()) break;
    g.dir->Join(ToRecruit(s));
    g.in.push_back(s.id);
    engine::DropPath(s.id);
    Log("tbc: %s joins (round %u)", Who(s.id).c_str(), g.dir->Round());
  }
  if (!g.dir->Running()) EndFight("one side is gone");
}

void OnTurnChange(int32_t prev, int32_t next) {
  if (prev != -1 && Contains(g.in, prev)) {
    engine::ClearAI(prev);  // clean stop: no action, path or queue carried out of its turn
    engine::DropPath(prev);
  }
  g.current = next;
  g.end_requested = false;
  if (next == -1) return;
  const Combatant* c = g.dir->Find(next);
  const bool party = c && c->r.side == Side::Party;
  const int slot = PartySlot(next);
  g.clock = TurnClock(g.round_ticks, slot >= 0 && !g.auto_party);
  g.econ = Economy(g.move_feet, g.round_ticks, slot >= 0 && !g.auto_party);
  g.hold_current = false;
  g.preview_goal = {-1, -1};
  if (slot >= 0 && !g.auto_party) engine::ClearAI(next);  // nothing queued while it waited: player orders only
  {
    engine::SpriteInfo s0;
    if (engine::GetSprite(next, &s0)) g.last_x = s0.x, g.last_y = s0.y;
  }
  if (slot >= 0 && g.auto_party) {
    // Party AI: the engine's own console path (CLUAConsole::Eval) queues the action on that slot.
    const std::string err = engine::LuaDoString(
        ("C:Eval('Attack(NearestEnemyOf(Myself))', " + std::to_string(slot) + ")").c_str());
    if (!err.empty()) Log("tbc: auto party order failed: %s", err.c_str());
  }
  char b[128];
  if (slot >= 0 && !g.auto_party) {
    std::snprintf(b, sizeof b, "Round %u: %s's turn (End key ends it)", g.dir->Round(),
                  Who(next).c_str());
  } else {
    std::snprintf(b, sizeof b, "Round %u: %s's turn", g.dir->Round(), Who(next).c_str());
  }
  (void)party;
  Say(b);
}

bool OurWindowFocused() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
}

// The preview search: cells from the creature at `from` to world point `to`, on the engine's own walk
// costs (A* in path_preview.h). Empty when unreachable.
std::vector<Pt> PreviewCells(int32_t id, Pt from, Pt to) {
  const Grid grid;
  const Pt goal{to.x / grid.sx, to.y / grid.sy}, start{from.x / grid.sx, from.y / grid.sy};
  const engine::CellCoster coster = engine::CosterFor(id);
  if (!coster.ok()) return {};
  // A creature standing next to an obstacle can be "inside" it at its own size: near the start (and
  // the goal) the walkable test uses size 1, a single cell, so the search can leave.
  engine::CellCoster thin = coster;
  thin.personal_space = 1;
  auto cost = [&](int x, int y) {
    const int c = coster.Cost(x, y);
    if (c < kImpassable) return c;
    const bool close_by = (std::abs(x - start.x) <= 3 && std::abs(y - start.y) <= 3) ||
                          (std::abs(x - goal.x) <= 2 && std::abs(y - goal.y) <= 2);
    return close_by ? thin.Cost(x, y) : c;
  };
  int min_cost = kImpassable;
  for (int i = 0; i < 16; ++i)
    if (coster.terrain[i] > 0 && coster.terrain[i] < min_cost) min_cost = coster.terrain[i];
  return FindCellPath(start, goal, cost, 30000, coster.width, coster.height,
                      min_cost < kImpassable ? min_cost : 1);
}

// World points after `from` along the preview cells, ending exactly on `to`.
std::vector<Pt> PreviewPoints(const std::vector<Pt>& cells, Pt to) {
  const Grid grid;
  std::vector<Pt> pts;
  for (size_t i = 1; i < cells.size(); ++i)
    pts.push_back({cells[i].x * grid.sx + grid.sx / 2, cells[i].y * grid.sy + grid.sy / 2});
  if (!pts.empty()) pts.back() = to;
  return pts;
}

// Where the active party member would walk to the cursor (cached per start/goal cell).
std::vector<Pt> HoverPreview(const engine::SpriteInfo& me) {
  int wx = 0, wy = 0;
  g.unreachable = false;
  if (g.preview_at.x >= 0) {
    wx = g.preview_at.x, wy = g.preview_at.y;
  } else if (!engine::CursorWorld(&wx, &wy)) {
    return {};
  }
  const Grid grid;
  const Pt goal{wx / grid.sx, wy / grid.sy}, start{me.x / grid.sx, me.y / grid.sy};
  if (!(goal == g.preview_goal) || !(start == g.preview_start)) {
    g.preview_goal = goal;
    g.preview_start = start;
    g.preview_cells = PreviewCells(g.current, {me.x, me.y}, {wx, wy});
  }
  std::vector<Pt> pts = PreviewPoints(g.preview_cells, {wx, wy});
  if (pts.empty()) {
    g.unreachable = goal.x != start.x || goal.y != start.y;
    g.cursor = {wx, wy};
    return pts;
  }
  g.line_preview = true;
  if (!g.logged_preview) {
    g.logged_preview = true;
    Log("tbc: hover preview %s -> %d,%d: %zu points, %.1f ft (%.1f ft left)", Who(g.current).c_str(), wx, wy,
        pts.size(), PathFeet({me.x, me.y}, pts), g.econ.move_ft);
  }
  return pts;
}

// World pass (engine render): the movement line, gold for the feet left, red beyond, over a dark
// outline; a small diamond where it ends.
void DrawMovementLine() {
  constexpr uint32_t kGold = 0x3CC4FF, kRed = 0x2020E0, kShadow = 0x0A0E14;
  if (g.running && g.unreachable && g.line.empty()) {
    const Pt c = g.cursor;
    for (int pass = 0; pass < 2; ++pass) {
      const uint32_t col = pass ? kRed : kShadow;
      const float w = pass ? 2.4f : 4.4f;
      engine::DrawWorldLine(c.x - 10, c.y - 8, c.x + 10, c.y + 8, col, w);
      engine::DrawWorldLine(c.x - 10, c.y + 8, c.x + 10, c.y - 8, col, w);
    }
    return;
  }
  if (!g.running || g.line.size() < 2) return;
  const float core = g.line_preview ? 2.6f : 3.2f;
  double total = 0;
  for (int pass = 0; pass < 2; ++pass) {
    double left = g.econ.move_ft;
    total = 0;
    for (size_t i = 1; i < g.line.size(); ++i) {
      const Pt a = g.line[i - 1], b = g.line[i];
      const double seg = FeetBetween(a.x, a.y, b.x, b.y);
      total += seg;
      if (pass == 0) {
        engine::DrawWorldLine(a.x, a.y, b.x, b.y, kShadow, core + 2.4f);
        continue;
      }
      if (left >= seg) {
        engine::DrawWorldLine(a.x, a.y, b.x, b.y, kGold, core);
      } else if (left > 0) {
        const double t = left / seg;
        const Pt m{a.x + static_cast<int>((b.x - a.x) * t), a.y + static_cast<int>((b.y - a.y) * t)};
        engine::DrawWorldLine(a.x, a.y, m.x, m.y, kGold, core);
        engine::DrawWorldLine(m.x, m.y, b.x, b.y, kRed, core);
      } else {
        engine::DrawWorldLine(a.x, a.y, b.x, b.y, kRed, core);
      }
      left -= seg;
    }
  }
  const Pt e = g.line.back();
  const uint32_t end_color = total <= g.econ.move_ft + 0.01 ? kGold : kRed;
  const int r = 12, ry = 9;  // a ground-plane diamond (4:3) where the walk would end
  for (int pass = 0; pass < 2; ++pass) {
    const uint32_t c = pass ? end_color : kShadow;
    const float w = pass ? 2.2f : 4.2f;
    engine::DrawWorldLine(e.x - r, e.y, e.x, e.y - ry, c, w);
    engine::DrawWorldLine(e.x, e.y - ry, e.x + r, e.y, c, w);
    engine::DrawWorldLine(e.x + r, e.y, e.x, e.y + ry, c, w);
    engine::DrawWorldLine(e.x, e.y + ry, e.x - r, e.y, c, w);
  }
}

// Only the party member whose turn it is may be selected (none on other turns): the engine's own
// selection calls, every frame, undo a click on another portrait or creature.
void LockSelection() {
  const int32_t allowed = PartySlot(g.current) >= 0 ? g.current : -1;
  for (int32_t id : g.party) {
    if (engine::IsSelected(id) != (id == allowed)) {
      Log("tbc: selection locked to %s", allowed == -1 ? "nobody" : Who(allowed).c_str());
      engine::SelectOnly(allowed);
      return;
    }
  }
}

void UpdateHud(uint32_t now) {
  if (!g.hud || !g.running) return;
  HudView v;
  v.on = true;
  v.round = g.dir->Round();
  for (int32_t id : g.dir->Order()) {
    engine::SpriteInfo o;
    if (!engine::GetSprite(id, &o)) continue;
    const Combatant* c = g.dir->Find(id);
    HudCard card;
    card.id = id;
    card.bestiary = o.bestiary;
    card.name_ref = o.name_ref;
    card.name = Who(id);
    card.hp = o.hp;
    card.max_hp = o.max_hp;
    card.foe = c && c->r.side == Side::Hostile;
    card.active = id == g.current;
    card.done = c && c->done_this_round;
    v.cards.push_back(card);
  }
  v.party_turn = PartySlot(g.current) >= 0;  // also with party AI (tests, demos)
  v.slot = PartySlot(g.current);
  v.move_ft = g.econ.move_ft;
  v.max_ft = g.econ.max_ft;
  v.action_used = g.econ.action_used;
  v.acting = g.econ.acting;
  if (now < g.notice_until) v.msg = g.notice;
  engine::SpriteInfo me;
  if (g.current != -1 && engine::GetSprite(g.current, &me)) {
    std::vector<Pt> nodes;
    for (const auto& p : engine::PathPoints(g.current)) nodes.push_back({p.first, p.second});
    g.line_preview = false;
    g.unreachable = false;
    if (nodes.empty() && g.econ.party && g.econ.MoveLeft() && !g.econ.acting) nodes = HoverPreview(me);
    const Pt from{me.x, me.y};
    g.line.clear();
    if (!nodes.empty()) {
      g.line.push_back(from);
      g.line.insert(g.line.end(), nodes.begin(), nodes.end());
      v.path_ft = static_cast<int>(PathFeet(from, nodes) + 0.5);
      engine::WorldToUi(nodes.back().x, nodes.back().y, &v.label_x, &v.label_y);
    }
  }
  const std::string code = BuildHudLua(v) +
                           " if not pstfTbcOpen then Infinity_PushMenu('PSTF_TBC') end pstfTbcLayout()";
  const std::string err = engine::LuaDoString(code.c_str());
  if (!err.empty()) {
    static int reported = 0;
    if (reported++ < 5) Log("tbc: hud update failed: %s", err.c_str());
  }
}

void OnTick(uint64_t frame) {
  if (engine::ActiveScreen() != engine::Screen::World) {
    EndFight("left the world screen");
    g.have_time = false;
    return;
  }
  uint32_t now = 0;
  bool running = false;
  if (!engine::WorldTime(&now, &running)) return;
  const uint32_t dt = g.have_time && now >= g.last_time ? now - g.last_time : 0;
  g.last_time = now;
  g.have_time = true;
  g.round_ticks = engine::RoundLengthTicks() ? engine::RoundLengthTicks() : kRoundTicksPst;

  if (frame % 4 == 0 || g.running) Scan(now);
  if (!g.running) return;

  const int32_t cur = g.dir->Current();
  if (cur != g.current) OnTurnChange(g.current, cur);
  if (cur == -1) return;

  // End Turn key (edge), only for party turns and only when the game window has focus.
  const bool down = (GetAsyncKeyState(g.end_turn_vk) & 0x8000) != 0 && OurWindowFocused();
  if (down && !g.key_down && g.econ.party) g.end_requested = true;
  g.key_down = down;

  engine::SpriteInfo s;
  if (!engine::GetSprite(cur, &s)) {
    g.dir->Remove(cur);
    return;
  }
  const double moved = FeetBetween(g.last_x, g.last_y, s.x, s.y);
  g.last_x = s.x, g.last_y = s.y;
  g.clock.Feed(dt, s.busy);  // busy-tick count for the log only
  const EconomyStep st = g.econ.Feed(dt, moved, s.has_path, s.action);
  g.hold_current = st.hold_walk;
  if (st.stop != Stop::None) {
    static const char* kStop[] = {"", "Can't get there this turn: no movement left",
                                  "No action left this turn", "Attack round done"};
    Log("tbc: %s: stop (%s), action %u, %.1f/%.1f ft left", Who(cur).c_str(),
        kStop[static_cast<int>(st.stop)], s.action, g.econ.move_ft, g.econ.max_ft);
    engine::ClearAI(cur);
    engine::DropPath(cur);
    if (g.econ.party && st.stop != Stop::RoundDone) {
      g.notice = kStop[static_cast<int>(st.stop)];
      g.notice_until = now + 45;
    }
  }
  if (st.end_turn || g.end_requested) {
    std::string hps;
    for (int32_t id : g.in) {
      engine::SpriteInfo o;
      if (engine::GetSprite(id, &o)) hps += " " + Who(id) + "=" + std::to_string(o.hp);
    }
    Log("tbc: %s ends turn (%s, action %s, %.1f/%.1f ft left, busy %u ticks, total %u) hp:%s",
        Who(cur).c_str(), g.end_requested ? "End Turn" : "economy",
        g.econ.action_used ? "used" : "unused", g.econ.move_ft, g.econ.max_ft, g.clock.busy,
        g.econ.total, hps.c_str());
    g.dir->EndTurn(cur);
    if (!g.dir->Running()) {
      EndFight("one side is gone");
      return;
    }
  }
  LockSelection();
  UpdateHud(now);
}

// --- Lua probes -------------------------------------------------------------------------------
int __cdecl LuaState(engine::LuaState* L) {
  std::string s = g.running ? "running" : "idle";
  if (g.running) {
    char b[160];
    std::snprintf(b, sizeof b, " fight=%u round=%u current=%s busy=%u/%u order=", g.fights,
                  g.dir->Round(), Who(g.current).c_str(), g.clock.busy, g.clock.budget);
    s += b;
    for (int32_t id : g.dir->Order()) s += Who(id) + ";";
  } else {
    s += " fights=" + std::to_string(g.fights);
  }
  Log("lua: PSTF_TbcState -> %s", s.c_str());
  engine::LuaPushString(L, s.c_str());
  return 1;
}
int __cdecl LuaEndTurn(engine::LuaState* L) {
  const bool ok = g.running && g.econ.party;
  if (ok) g.end_requested = true;
  Log("lua: PSTF_EndTurn -> %d", ok ? 1 : 0);
  engine::LuaPushBool(L, ok);
  return 1;
}
// Test helper: the nearest living non-party creature in the party's area becomes an enemy through
// the engine's own Enemy() action function. Returns its id (or -1).
int __cdecl LuaMakeEnemyNearest(engine::LuaState* L) {
  const auto party = engine::PartyIds();
  engine::SpriteInfo me;
  int32_t best = -1;
  long long best_d = -1;
  if (!party.empty() && engine::GetSprite(party[0], &me)) {
    for (const auto& s : engine::AllSprites()) {
      if (s.area != me.area || !s.alive || Contains(party, s.id)) continue;
      const long long dx = s.x - me.x, dy = s.y - me.y, d = dx * dx + dy * dy;
      if (best_d < 0 || d < best_d) best_d = d, best = s.id;
    }
  }
  const bool ok = best != -1 && engine::MakeEnemy(best);
  Log("lua: PSTF_MakeEnemyNearest -> %X (%s)", static_cast<unsigned>(best), ok ? "ok" : "none");
  engine::LuaPushNumber(L, ok ? best : -1);
  return 1;
}

// PSTF_SpritePos(id) -> x, y, hp, ea (or -1 if no such creature).
int __cdecl LuaSpritePos(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  engine::SpriteInfo s;
  const bool ok = engine::GetSprite(id, &s);
  Log("lua: PSTF_SpritePos(%X) -> %d,%d hp %d ea %u", static_cast<unsigned>(id), s.x, s.y, s.hp, s.ea);
  engine::LuaPushNumber(L, ok ? s.x : -1);
  engine::LuaPushNumber(L, ok ? s.y : -1);
  engine::LuaPushNumber(L, ok ? s.hp : -1);
  engine::LuaPushNumber(L, ok ? s.ea : -1);
  return 4;
}

// Test probe: PSTF_PreviewAt(x, y) makes the hover preview use this world point instead of the
// cursor (PSTF_PreviewAt(-1, -1) = cursor again).
int __cdecl LuaPreviewAt(engine::LuaState* L) {
  g.preview_at = {static_cast<int>(engine::LuaNumberArg(L, 1, -1)), static_cast<int>(engine::LuaNumberArg(L, 2, -1))};
  g.preview_goal = {-1, -1};
  g.logged_preview = false;
  Log("lua: PSTF_PreviewAt -> %d,%d", g.preview_at.x, g.preview_at.y);
  return 0;
}

// PSTF_Note(text): writes text to our log (for test scripts and engine-state checks).
int __cdecl LuaNote(engine::LuaState* L) {
  Log("lua note: %s", engine::LuaStringArg(L, 1).c_str());
  return 0;
}

// PSTF_PathFt(x, y) -> feet of the preview path of the party leader to world point x,y, or -1 when
// it cannot get there (same search as the hover preview).
int __cdecl LuaPathFt(engine::LuaState* L) {
  const auto ids = engine::PartyIds();
  const Pt to{static_cast<int>(engine::LuaNumberArg(L, 1, -1)), static_cast<int>(engine::LuaNumberArg(L, 2, -1))};
  double ft = -1;
  engine::SpriteInfo me;
  if (!ids.empty() && engine::GetSprite(ids[0], &me)) {
    const auto pts = PreviewPoints(PreviewCells(ids[0], {me.x, me.y}, to), to);
    if (!pts.empty()) ft = PathFeet({me.x, me.y}, pts);
  }
  Log("lua: PSTF_PathFt(%d,%d) -> %.1f", to.x, to.y, ft);
  engine::LuaPushNumber(L, ft);
  return 1;
}

// Probes: PSTF_PartyId(slot) -> creature id; PSTF_WalkCost(x, y) -> the party leader's engine walk cost
// of the cell under world point x,y (0xFF = impassable).
int __cdecl LuaPartyId(engine::LuaState* L) {
  const auto ids = engine::PartyIds();
  const int slot = static_cast<int>(engine::LuaNumberArg(L, 1, 0));
  engine::LuaPushNumber(L, slot >= 0 && slot < static_cast<int>(ids.size()) ? ids[slot] : -1);
  return 1;
}
int __cdecl LuaWalkCost(engine::LuaState* L) {
  const auto ids = engine::PartyIds();
  const int x = static_cast<int>(engine::LuaNumberArg(L, 1, -1)), y = static_cast<int>(engine::LuaNumberArg(L, 2, -1));
  int c = 0xFF;
  if (!ids.empty()) c = engine::CosterFor(ids[0]).Cost(x / 16, y / 12);
  Log("lua: PSTF_WalkCost(%d,%d) -> %d", x, y, c);
  engine::LuaPushNumber(L, c);
  return 1;
}

int IniInt(const IniMap& ini, const char* key, int fallback) {
  auto it = ini.find(key);
  if (it == ini.end() || it->second.empty()) return fallback;
  try {
    return std::stoi(it->second, nullptr, 0);
  } catch (...) {
    return fallback;
  }
}

}  // namespace

bool InstallTbc(const IniMap& ini) {
  if (!IniBool(ini, "tbc.enabled", false)) {
    Log("tbc: turn-based combat disabled in ini");
    return false;
  }
  g.radii.start = IniInt(ini, "tbc.start_radius", g.radii.start);
  g.radii.join = IniInt(ini, "tbc.join_radius", g.radii.join);
  g.radii.leave = IniInt(ini, "tbc.leave_radius", g.radii.leave);
  g.end_turn_vk = IniInt(ini, "tbc.end_turn_key", VK_END);
  g.auto_party = IniBool(ini, "tbc.auto_party", false);
  {
    const int feet = IniInt(ini, "tbc.move_feet", 31);
    g.move_feet = feet > 0 ? feet : 31;
  }
  {
    char* v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, "PSTF_TBC_AUTOPARTY") == 0 && v) g.auto_party = v[0] == '1';
    free(v);
  }
  if (!engine::SpritesAvailable() || !engine::ScreensAvailable() || !engine::WorldApiAvailable()) {
    Log("tbc: engine functions differ from the manifest; turn-based combat off");
    return false;
  }
  for (const auto& s : sym::kAll)
    if (s.rva == sym::kRvaProcessAI && !CheckSymbol(s)) return false;

  // All-or-nothing: verify every site before patching any.
  const sym::Symbol* slot = nullptr;
  const sym::Symbol* w1 = nullptr;
  const sym::Symbol* w2 = nullptr;
  for (const auto& s : sym::kData)
    if (s.rva == sym::kRvaSlotProcessAI) slot = &s;
  for (const auto& s : sym::kAll) {
    if (s.rva == sym::kRvaWalkCall1) w1 = &s;
    if (s.rva == sym::kRvaWalkCall2) w2 = &s;
  }
  if (!slot || !w1 || !w2 || !CheckSymbol(*slot) || !CheckSymbol(*w1) || !CheckSymbol(*w2)) {
    Log("tbc: hook sites differ from the manifest; turn-based combat off");
    return false;
  }
  const uint64_t walk1 = RedirectCall(*w1, reinterpret_cast<void*>(&HookWalk));
  const uint64_t walk2 = RedirectCall(*w2, reinterpret_cast<void*>(&HookWalk));
  const uint64_t pai = ReplacePointer(*slot, reinterpret_cast<void*>(&HookProcessAI));
  if (walk1 != sym::kVaAIUpdateWalk || walk2 != sym::kVaAIUpdateWalk ||
      pai != sym::kImageBase + sym::kRvaProcessAI) {
    Log("tbc: FAILED to install hooks (walk %llX/%llX, ProcessAI %llX)",
        static_cast<unsigned long long>(walk1), static_cast<unsigned long long>(walk2),
        static_cast<unsigned long long>(pai));
    return false;
  }
  g_walk = reinterpret_cast<SpriteFn>(walk1);
  g_process_ai = reinterpret_cast<SpriteFn>(pai);
  if (!AddTickHandler(&OnTick)) return false;
  if (IniBool(ini, "tbc.hud", true)) {
    if (engine::HudAvailable() && engine::InstallMenuHook()) {
      engine::AddMenuText(BuildHudMenu());
      g.hud = true;
      g.draw = engine::InstallWorldDraw(&DrawMovementLine);
      if (!g.draw) Log("tbc: world overlay not installed; no movement line");
    } else {
      Log("tbc: HUD functions differ from the manifest; turn-based HUD off (combat still on)");
    }
  }
  dev::AddLuaFunction("PSTF_TbcState", &LuaState);
  dev::AddLuaFunction("PSTF_EndTurn", &LuaEndTurn);
  dev::AddLuaFunction("PSTF_MakeEnemyNearest", &LuaMakeEnemyNearest);
  dev::AddLuaFunction("PSTF_SpritePos", &LuaSpritePos);
  dev::AddLuaFunction("PSTF_PreviewAt", &LuaPreviewAt);
  dev::AddLuaFunction("PSTF_PartyId", &LuaPartyId);
  dev::AddLuaFunction("PSTF_Note", &LuaNote);
  dev::AddLuaFunction("PSTF_PathFt", &LuaPathFt);
  dev::AddLuaFunction("PSTF_WalkCost", &LuaWalkCost);
  Log("tbc: installed (ProcessAI slot, 2 walk calls, tick); radii %d/%d/%d, end turn key %X%s, "
      "move %.0f ft, hud %s",
      g.radii.start, g.radii.join, g.radii.leave, g.end_turn_vk, g.auto_party ? ", auto party" : "",
      g.move_feet, g.hud ? "on" : "off");
  return true;
}

}  // namespace pstf::tbc
