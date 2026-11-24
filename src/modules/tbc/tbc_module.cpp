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
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/patch.h"
#include "core/tick.h"
#include "engine/combat.h"
#include "engine/frame.h"
#include "engine/lua.h"
#include "engine/menu.h"
#include "engine/render.h"
#include "engine/screens.h"
#include "engine/sprites.h"
#include "engine/symbols.h"
#include "engine/world.h"
#include "modules/dev/lua_bridge.h"
#include "modules/tbc/camera_logic.h"
#include "modules/tbc/census.h"
#include "modules/tbc/combat_log.h"
#include "modules/tbc/director.h"
#include "modules/tbc/economy.h"
#include "modules/tbc/hud.h"
#include "modules/tbc/hover.h"
#include "modules/tbc/status.h"
#include "core/gameres.h"
#include "modules/tbc/round_rules.h"
#include "modules/tbc/path_line.h"
#include "modules/tbc/path_preview.h"
#include "modules/tbc/turn_clock.h"
#include "modules/tbc/ui_layout.h"

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
  // Effects advance one round at their owner's turn start ([tbc] round_effects, round_seconds).
  bool round_effects = true;
  uint32_t effect_round_ticks = 75;  // 5 s x 15 ticks
  bool run = true;                   // [tbc] run: everyone runs in a fight
  uint8_t run_scale_other = 11;      // move scale for animations without a run sequence
  bool music = true;                 // [tbc] battle_music: keep the battle song for the whole fight
  bool camera = true;                // [tbc] camera_follow: centre on whoever's turn it is
  Pt cam_from{0, 0}, cam_to{0, 0};
  uint32_t cam_t0 = 0;               // GetTickCount at the pan start; 0 = no pan
  bool cam_smooth = true;            // [tbc] camera_smooth: follow the DRAWN position, once per frame
  bool cam_pre = false;              // the camera runs in the draw pass (true) or on ticks (false)
  CameraEase cam_ease;               // eased view centre (camera_logic.h)
  double cam_last = 0.0;             // CamNowMs of the previous camera step; 0 = first
  uint64_t cam_steps = 0;            // camera follow steps since install (probe PSTF_CameraSteps)
  int32_t step_logged = -1;
  bool test_tick = false;            // env PSTF_TEST_TICK=1: call Lua pstfTestTick() every frame
  int32_t nudged = -1;               // AI creature we ordered this turn (its interrupt is restored)
  bool no_fight = false;             // env PSTF_TBC_NOFIGHT=1: probes only, fights stay real time
  // Dice panel: the last roll and when it came (GetTickCount), its d20 centre in UI coordinates.
  bool dice_on = false;
  engine::RollEvent dice;
  uint32_t dice_t0 = 0;
  bool dice_sound = false;           // save result sound still to play (at the verdict)
  bool dice_panel = false;           // [tbc] dice_panel: the d20 panel for attack rolls and saves
  bool sight = true;                 // [tbc] combat_sight: every combatant is in view during a fight
  bool items = false;                // item use hooks installed: an instant item use takes the action
  bool save_sounds = true;           // [tbc] save_sounds: success/fail sound with the engine's "Save vs." line
  Pt dice_ui{-1, -1};
  Pt hover_at{-1, -1};               // test probe PSTF_HoverAt: this world point instead of the cursor
  bool rolls = false;                // the roll hooks are installed
  uint32_t bar_t0 = 0;               // GetTickCount of the last hotbar collection (0 = due)
  // Combat log (logs\combat.log): creature names from the TLK (registered through Lua), last HP seen,
  // the world tick the current turn started at, the active creature's last action.
  std::string clog_path;
  FILE* clog = nullptr;
  std::map<int32_t, std::string> names;
  std::map<int32_t, int> hp_seen;
  uint32_t turn_w0 = 0;
  uint16_t clog_action = 0xFFFF;
  bool follow = false;               // the camera keeps the active creature centred while it moves
  std::wstring sound_dir;            // <game>/pst_forever/sounds/
  uint32_t resolve_ticks = 0;        // world ticks the turn has waited for its spell to land
  uint32_t since_action = 1000;      // world ticks since its action stopped running
  bool resolving_logged = false;
  PulseBudget pulses;                // lingering spells: one round of pulse ticks per caster round
  int sight_radius = 448;            // [tbc] sight_radius: px of fog lifted around each foe in the fight
  bool select_voice = true;          // [tbc] select_voice: a party member's turn plays its selection line
  bool no_bored = true;              // [tbc] no_bored: no bored voice lines during a fight
  bool steady_ai = false;            // the AI walker of this turn got SetInterrupt(FALSE) (walks in one go)
  bool paused_screen = false;        // a menu screen (inventory, character...) is up: the fight waits
  // Floating text over creatures ("Initiative 7"), until GetTickCount passes `until`.
  struct Float {
    int32_t id;
    std::string text;
    uint32_t until;
  };
  std::vector<Float> floats;
  int apr_halves = 0;                // the active creature's APR x2 (0: unknown) for the HUD
  int party_max = 6;                 // [party] size: portrait slots (6 vanilla, 8 with the party module)
  std::unique_ptr<GameRes> res;      // game resources: icon sizes, spell icons/names (status icons)
  StatusSpans status_spans;          // first-seen length of each buff on each creature
  std::vector<std::vector<StatusIconView>> status_party;
  uint32_t status_t0 = 0;            // GetTickCount of the last party status collection
} g;

// The creature's spell is still on its way (a projectile object it cast is in the area): its turn
// waits for flying ones (Adder's Kiss circles 60 ticks before it flies) and for lingering ones that
// still have pulse ticks this round (the round's pulse lands in the caster's turn).
constexpr uint32_t kResolveCapTicks = 120;  // Cone of Cold / Ice Knife go off 100 ticks after landing
constexpr uint32_t kFlightCapTicks = 300;   // a projectile still flying after 20 s is held to next turn
constexpr double kCamTauMs = 60.0;          // camera easing time constant (CameraEase::Step): the camera
                                            // closes 63 % of the gap in 60 ms at any frame rate
constexpr double kCamLockPx = 6.0;          // this close, the camera locks onto the shown position
constexpr int kCamSnapPx = 600;             // a creature this far off the centre is centred at once
struct Pending {
  int flying = 0, lingering = 0;
};
Pending CasterProjectiles(int32_t id, uint64_t area) {
  Pending n;
  for (const auto& o : engine::AllObjects()) {
    if (o.source != id || o.area != area || o.type != 0) continue;  // CProjectile* (type 0, caster +0x68)
    if (!engine::ProjectileLingers(o.id)) ++n.flying;
    else if (!g.pulses.Spent(o.id)) ++n.lingering;
  }
  return n;
}
std::vector<int32_t> LingeringOf(int32_t id) {
  std::vector<int32_t> v;
  for (const auto& o : engine::AllObjects())
    if (o.source == id && o.type == 0 && engine::ProjectileLingers(o.id)) v.push_back(o.id);
  return v;
}

bool Contains(const std::vector<int32_t>& v, int32_t id) {
  return std::find(v.begin(), v.end(), id) != v.end();
}

bool Held(int32_t id) { return g.running && id != g.current && Contains(g.in, id); }

// Projectiles move in their caster's turn; a landed cloud or swarm also stops once it has used this
// round's pulse ticks. Casters outside the fight are not paced.
bool ProjectileRuns(int32_t caster, int32_t proj, bool lingers) {
  if (!g.running || !Contains(g.in, caster)) return true;
  if (caster != g.current) return false;
  return !lingers || g.pulses.Run(proj);
}

void __fastcall HookProcessAI(void* sprite) {
  if (Held(engine::SpriteIdOf(sprite))) {
    // No scripts or actions while it waits, but spells cast on it land now (round clock frozen).
    if (g.round_effects) engine::HeldEffectsTick(sprite);
    return;
  }
  g_process_ai(sprite);
}
void __fastcall HookWalk(void* sprite) {
  const int32_t id = engine::SpriteIdOf(sprite);
  if (Held(id)) return;
  if (g.running && id == g.current && g.hold_current) return;  // movement spent this turn
  if (g.running && g.run && id == g.current) engine::ForceRun(id, true, g.run_scale_other);
  g_walk(sprite);
}

// Effect round clock: every combatant's persistent effects (DoT, regeneration) wait for its turn.
bool FrozenCombatant(int32_t id) { return g.running && g.round_effects && Contains(g.in, id); }

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

// --- combat log -------------------------------------------------------------------------------------
std::string Name(int32_t id) {
  char b[16];
  std::snprintf(b, sizeof b, " (%X)", static_cast<unsigned>(id));
  const auto it = g.names.find(id);
  return (it != g.names.end() && !it->second.empty() ? it->second : Who(id)) + b;
}

void CLog(const char* kind, const std::string& text) {
  if (g.clog_path.empty()) return;
  if (!g.clog) {
    fopen_s(&g.clog, g.clog_path.c_str(), "a");
    if (!g.clog) return;
  }
  SYSTEMTIME t;
  GetLocalTime(&t);
  const uint32_t round = g.dir ? g.dir->Round() : 0;
  const uint32_t into = g.current != -1 && g.last_time >= g.turn_w0 ? g.last_time - g.turn_w0 : 0;
  const std::string line = CombatLogLine(t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, g.last_time, round, into, kind, text);
  std::fputs(line.c_str(), g.clog);
  std::fputc('\n', g.clog);
  std::fflush(g.clog);
}

// Names come from the TLK through the engine's Lua (Infinity_FetchString), once per creature.
void RegisterNames(const std::vector<int32_t>& ids) {
  std::string code;
  for (int32_t id : ids) {
    if (g.names.count(id)) continue;
    engine::SpriteInfo o;
    if (!engine::GetSprite(id, &o)) continue;
    g.names[id] = "";
    if (o.name_ref >= 0)
      code += "PSTF_SetName(" + std::to_string(id) + ", Infinity_FetchString(" + std::to_string(o.name_ref) + ")) ";
  }
  if (!code.empty()) engine::LuaDoString(code.c_str());
}

std::string HpText(int32_t id) {
  engine::SpriteInfo o;
  if (!engine::GetSprite(id, &o)) return "gone";
  return std::to_string(o.hp) + "/" + std::to_string(o.max_hp);
}

// Every combatant's HP change, with whose turn it happened in.
void LogHpChanges() {
  for (int32_t id : g.in) {
    engine::SpriteInfo o;
    if (!engine::GetSprite(id, &o)) continue;
    const auto it = g.hp_seen.find(id);
    if (it != g.hp_seen.end() && it->second != o.hp) {
      char b[64];
      std::snprintf(b, sizeof b, " %d -> %d (%+d)", it->second, o.hp, o.hp - it->second);
      CLog(o.hp < it->second ? "DAMAGE" : "HEAL", Name(id) + b + (g.current != -1 ? ", in the turn of " + Name(g.current) : ""));
    }
    g.hp_seen[id] = o.hp;
  }
}

void EndFight(const char* why) {
  if (!g.running) return;
  Log("tbc: fight %u over (%s) after %u rounds", g.fights, why, g.dir ? g.dir->Round() : 0);
  {
    std::string hps;
    for (int32_t id : g.in) hps += "\n      " + Name(id) + " hp " + HpText(id);
    CLog("END", "fight " + std::to_string(g.fights) + " over (" + why + ") after " +
                    std::to_string(g.dir ? g.dir->Round() : 0) + " rounds" + hps);
  }
  if (g.run)
    for (int32_t id : g.in) engine::ForceRun(id, false, g.run_scale_other);
  if (g.sight && !g.party.empty()) engine::RevealCombatants(g.party[0], {}, 0);
  g.nudged = -1;
  g.running = false;
  engine::ForgetOnsets();
  g.pulses.Clear();
  g.in.clear();
  g.hp_seen.clear();
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
        "pstfTbc = {on=false,cards={},dots={},rdots={}} if Infinity_IsMenuOnStack('PSTF_TBC') then Infinity_PopMenu('PSTF_TBC') end");
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
  RegisterNames(g.dir->Order());
  g.floats.clear();
  for (int32_t id : g.dir->Order())
    if (const Combatant* c = g.dir->Find(id))
      g.floats.push_back({id, "Initiative: " + std::to_string(c->initiative), GetTickCount() + 3500});
  {
    std::string lines;
    for (int32_t id : g.dir->Order()) {
      const Combatant* c = g.dir->Find(id);
      engine::CombatStats st;
      engine::GetCombatStats(id, &st);
      char b[96];
      std::snprintf(b, sizeof b, " %s hp %s, THAC0 %d, AC %d, initiative %d", c && c->r.side == Side::Hostile ? "foe  " : "party",
                    HpText(id).c_str(), st.thac0, st.ac, c ? c->initiative : 0);
      lines += "\n      " + Name(id) + b;
    }
    CLog("START", "fight " + std::to_string(g.fights) + ", " + std::to_string(who.size()) + " combatants in initiative order:" + lines);
  }
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
    if (c.start && !g.no_fight) StartFight(c.join, now);
    return;
  }
  for (int32_t id : c.leave) {
    engine::SpriteInfo o;
    const bool there = engine::GetSprite(id, &o) && o.area == g.area;
    Log("tbc: %s leaves the fight (%s, hp %d)", Who(id).c_str(),
        !there ? "left the area" : !o.alive ? "dead" : "out of range", there ? o.hp : 0);
    CLog("LEAVE", Name(id) + (!there ? " left the area" : !o.alive ? " died" : " is out of range"));
    g.dir->Remove(id);
    g.in.erase(std::remove(g.in.begin(), g.in.end(), id), g.in.end());
  }
  for (const auto& s : c.join) {
    if (!g.dir->Running()) break;
    g.dir->Join(ToRecruit(s));
    g.in.push_back(s.id);
    engine::DropPath(s.id);
    Log("tbc: %s joins (round %u)", Who(s.id).c_str(), g.dir->Round());
    RegisterNames({s.id});
    const Combatant* jc = g.dir->Find(s.id);
    const int ini = jc ? jc->initiative : 0;
    g.floats.push_back({s.id, "Initiative: " + std::to_string(ini), GetTickCount() + 3500});
    CLog("JOIN", Name(s.id) + " hp " + HpText(s.id) + ", initiative " + std::to_string(ini));
  }
  if (!g.dir->Running()) EndFight("one side is gone");
}

void OnTurnChange(int32_t prev, int32_t next) {
  if (prev != -1 && Contains(g.in, prev)) {
    engine::ClearAI(prev);  // clean stop: no action, path or queue carried out of its turn
    engine::DropPath(prev);
  }
  g.nudged = -1;
  g.steady_ai = false;
  g.apr_halves = 0;
  g.current = next;
  g.end_requested = false;
  g.resolve_ticks = 0;
  g.resolving_logged = false;
  g.since_action = 1000;
  g.turn_w0 = g.last_time;
  g.clog_action = 0xFFFF;
  g.follow = false;
  if (next == -1) return;
  if (g.round_effects) {
    for (int32_t p : LingeringOf(next)) {
      g.pulses.NewRound(p);
      CLog("SPELL", Name(next) + ": lingering spell " + std::to_string(p) + " gets this round's pulse");
    }
  }
  if (g.round_effects && engine::EffectClockInstalled()) {
    engine::SpriteInfo before;
    const bool had = engine::GetSprite(next, &before);
    engine::StepEffectClock(next, g.effect_round_ticks);
    Log("tbc: %s turn start: effects step %u ticks (hp %d, %d timed, %d persistent)", Who(next).c_str(),
        g.effect_round_ticks, had ? before.hp : -1, static_cast<int>(engine::TimedEffects(next).size()),
        engine::PersistentCount(next));
    std::string timed;
    for (const auto& e : engine::TimedEffects(next))
      if (e.left > 0) timed += " op" + std::to_string(e.opcode) + ":" + std::to_string((e.left + 74) / 75) + "rd";
    CLog("TURN", Name(next) + " hp " + HpText(next) + ", effects stepped one round" +
                     (timed.empty() ? "" : ", timed left:" + timed) + ", " +
                     std::to_string(engine::PersistentCount(next)) + " persistent");
  }
  if (g.run) {
    const uint8_t before = engine::MoveScale(next);
    engine::ForceRun(next, true, g.run_scale_other);
    Log("tbc: %s runs: move scale %u -> %u", Who(next).c_str(), before, engine::MoveScale(next));
    CLog("RUN", Name(next) + " move scale " + std::to_string(before) + " -> " + std::to_string(engine::MoveScale(next)));
  }
  if (g.camera) {
    engine::SpriteInfo t;
    int cx = 0, cy = 0;
    if (engine::GetSprite(next, &t) && engine::ViewCenter(&cx, &cy)) {
      g.cam_from = {cx, cy};
      g.cam_to = {t.x, t.y};
      engine::InterpDrawPos(next, &g.cam_to.x, &g.cam_to.y);  // pan to where the creature is shown
      g.cam_t0 = GetTickCount() | 1u;
    }
  }
  const Combatant* c = g.dir->Find(next);
  const bool party = c && c->r.side == Side::Party;
  const int slot = PartySlot(next);
  g.clock = TurnClock(g.round_ticks, slot >= 0 && !g.auto_party);
  g.econ = Economy(g.move_feet, g.round_ticks, slot >= 0 && !g.auto_party);
  g.hold_current = false;
  g.preview_goal = {-1, -1};
  if (slot >= 0 && !g.auto_party) engine::ClearAI(next);  // nothing queued while it waited: player orders only
  // Attacks this round with the engine's own APR: 3/2 = 2 swings in the creature's 1st, 3rd... round,
  // 1 in the others. The engine's half-attack switch is set to match, so its round has exactly that many.
  if (c) {
    const int stat = engine::NumAttacksStat(next);
    g.apr_halves = stat >= 0 ? AprHalvesFromStat(static_cast<int16_t>(stat)) : 0;
    const int want = AttacksThisRound(g.apr_halves, c->own_rounds);
    if (g.apr_halves & 1) engine::SetHalfAttackReal(next, want > g.apr_halves / 2);
    if (g.rolls) g.econ.want_swings = want;
    CLog("APR", Name(next) + " attacks per round x2 = " + std::to_string(g.apr_halves) + ", own round " +
                    std::to_string(c->own_rounds + 1) + ": " + std::to_string(want) + " attack" + (want == 1 ? "" : "s"));
  }
  // Its selection line, as a portrait click would play it (the engine's SelectCharacter with sound).
  if (slot >= 0 && !g.auto_party && g.select_voice) engine::SelectOnly(next, true);
  g.bar_t0 = 0;  // the hotbar is collected again for whoever acts now
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
    const int c = coster.SearchCost(x, y);
    if (c < kImpassable) return c;
    const bool close_by = (std::abs(x - start.x) <= 3 && std::abs(y - start.y) <= 3) ||
                          (std::abs(x - goal.x) <= 2 && std::abs(y - goal.y) <= 2);
    return close_by ? thin.Cost(x, y) : c;
  };
  int min_cost = kImpassable;
  for (int i = 0; i < 16; ++i)
    if (coster.terrain[i] > 0 && coster.terrain[i] < min_cost) min_cost = coster.terrain[i];
  // The walker itself is out of the search map while the engine searches its path (SetTarget's
  // RemoveObject): only the OTHER creatures block, as they will block the real walk.
  const engine::SearchMapLift lift(id);
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

// World pass (engine render): the movement line as a thin glowing thread (BG3): a soft amber halo,
// a bright core, red past the feet left; a small glowing ring where the walk ends. The engine's line
// is drawn at alpha 0xAA at least (DrawLine2d), so the glow is layered colours, wide and dim to thin
// and bright.
struct GlowColors {
  uint32_t halo, mid, core;
};
constexpr GlowColors kGlowGold{0x1A4A8C, 0x40A8F0, 0xC8F4FF};  // 0xBBGGRR
constexpr GlowColors kGlowRed{0x101470, 0x2838E0, 0xB4C0FF};
void GlowSegment(Pt a, Pt b, const GlowColors& c, float scale) {
  engine::DrawWorldLine(a.x, a.y, b.x, b.y, c.halo, 2.6f * scale);
  engine::DrawWorldLine(a.x, a.y, b.x, b.y, c.mid, 1.3f * scale);
  engine::DrawWorldLine(a.x, a.y, b.x, b.y, c.core, 0.55f * scale);
}
void GlowRing(Pt e, const GlowColors& c) {
  constexpr int kN = 20;
  const double rx = 9.0, ry = 6.75;  // ground plane, 4:3
  for (int pass = 0; pass < 3; ++pass) {
    const uint32_t col = pass == 0 ? c.halo : pass == 1 ? c.mid : c.core;
    const float w = pass == 0 ? 2.4f : pass == 1 ? 1.2f : 0.5f;
    for (int i = 0; i < kN; ++i) {
      const double a0 = 6.283185307 * i / kN, a1 = 6.283185307 * (i + 1) / kN;
      engine::DrawWorldLine(e.x + static_cast<int>(rx * std::cos(a0)), e.y + static_cast<int>(ry * std::sin(a0)),
                            e.x + static_cast<int>(rx * std::cos(a1)), e.y + static_cast<int>(ry * std::sin(a1)), col, w);
    }
  }
}
void DrawMovementLine() {
  if (g.running && g.unreachable && g.line.empty()) {
    const Pt c = g.cursor;
    GlowSegment({c.x - 8, c.y - 6}, {c.x + 8, c.y + 6}, kGlowRed, 1.0f);
    GlowSegment({c.x - 8, c.y + 6}, {c.x + 8, c.y - 6}, kGlowRed, 1.0f);
    return;
  }
  if (!g.running || g.line.size() < 2) return;
  const float scale = g.line_preview ? 0.9f : 1.0f;
  double left = g.econ.move_ft, total = 0;
  for (size_t i = 1; i < g.line.size(); ++i) {
    const Pt a = g.line[i - 1], b = g.line[i];
    const double seg = FeetBetween(a.x, a.y, b.x, b.y);
    total += seg;
    if (left >= seg) {
      GlowSegment(a, b, kGlowGold, scale);
    } else if (left > 0) {
      const double t = left / seg;
      const Pt m{a.x + static_cast<int>((b.x - a.x) * t), a.y + static_cast<int>((b.y - a.y) * t)};
      GlowSegment(a, m, kGlowGold, scale);
      GlowSegment(m, b, kGlowRed, scale);
    } else {
      GlowSegment(a, b, kGlowRed, scale);
    }
    left -= seg;
  }
  GlowRing(g.line.back(), total <= g.econ.move_ft + 0.01 ? kGlowGold : kGlowRed);
}


// --- dice panel and hover card --------------------------------------------------------------------
uint32_t DiceMs() { return g.dice_on ? GetTickCount() - g.dice_t0 : 0; }

void PlayResultSound(bool good) {
  if (g.sound_dir.empty()) return;
  const std::wstring f = g.sound_dir + (good ? L"save_success.wav" : L"save_failed.wav");
  const BOOL ok = PlaySoundW(f.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
  Log("tbc: save sound %s (%s)", good ? "success" : "failed", ok ? "playing" : "not played");
}

// New rolls from the engine hooks: the last one involving a combatant goes on the dice panel.
void TakeRolls() {
  bool sound = false, sound_good = false;
  for (const auto& e : engine::TakeRollEvents()) {
    // CheckSave posts its "Save vs. ...: N" feedback line in the same call that queued this event:
    // the sound goes with that line, this frame (the last save of the frame wins).
    if (e.kind == engine::RollEvent::Save && g.save_sounds) sound = true, sound_good = e.success;
    if (!g.running || !Contains(g.in, e.actor)) continue;
    if (e.kind == engine::RollEvent::Attack && e.actor == g.current) g.econ.Swing();
    Log("tbc: roll: %s %s d20 %d total %d needs %d -> %s", Who(e.actor).c_str(),
        e.kind == engine::RollEvent::Attack ? ("attacks " + Who(e.target)).c_str() : "saves", e.roll, e.total, e.needs,
        e.success ? (e.kind == engine::RollEvent::Attack ? "hit" : "saved") : (e.kind == engine::RollEvent::Attack ? "miss" : "failed"));
    {
      char b[128];
      if (e.kind == engine::RollEvent::Attack)
        std::snprintf(b, sizeof b, " attacks %s: d20 %d, total %d, needs %d -> %s", Name(e.target).c_str(), e.roll, e.total,
                      e.needs, e.success ? "HIT" : "miss");
      else
        std::snprintf(b, sizeof b, " saving throw: d20 %d, total %d -> %s", e.roll, e.total, e.success ? "SAVED" : "failed");
      CLog("ROLL", Name(e.actor) + b);
    }
    if (!g.dice_panel) continue;
    g.dice = e;
    g.dice_on = true;
    g.dice_t0 = GetTickCount();
    g.dice_sound = false;
  }
  if (sound) PlayResultSound(sound_good);
  if (g.dice_on && g.dice_sound && DiceTiming::Verdict(DiceMs())) {
    g.dice_sound = false;
    PlayResultSound(g.dice.success);
  }
  if (g.dice_on && !DiceTiming::Visible(DiceMs())) g.dice_on = false;
}

void FillDice(HudView* v) {
  if (!g.dice_on) return;
  const uint32_t ms = DiceMs();
  const engine::RollEvent& e = g.dice;
  v->dice = true;
  v->dice_kind = e.kind == engine::RollEvent::Save ? 1 : 0;
  engine::SpriteInfo a, t;
  if (engine::GetSprite(e.actor, &a)) v->dice_actor_ref = a.name_ref;
  if (engine::GetSprite(e.target, &t)) v->dice_target_ref = t.name_ref;
  v->dice_actor = Who(e.actor);
  v->dice_target = Who(e.target);
  v->dice_face = DiceTiming::Face(ms, e.roll);
  v->dice_landed = DiceTiming::Landed(ms);
  v->dice_verdict = DiceTiming::Verdict(ms);
  v->dice_good = e.success;
  v->dice_alpha = DiceTiming::Alpha(ms);
  char b[96];
  const int mod = e.total - e.roll;
  if (e.kind == engine::RollEvent::Attack) {
    std::snprintf(b, sizeof b, "%d %s %d = %d     needs %d", e.roll, mod < 0 ? "-" : "+", mod < 0 ? -mod : mod, e.total,
                  e.needs);
    v->dice_result = e.roll == 20 ? "Critical hit!" : e.roll == 1 ? "Critical miss" : e.success ? "Hit" : "Miss";
  } else {
    std::snprintf(b, sizeof b, "%d %s %d = %d", e.roll, mod < 0 ? "-" : "+", mod < 0 ? -mod : mod, e.total);
    v->dice_result = e.success ? "Saved" : "Failed";
  }
  v->dice_sum = b;
}

// Status icons (buffs/debuffs) of a creature: one per spell or item with timed effects on it.
std::vector<StatusIconView> StatusIconsOf(int32_t id, int box, size_t max_icons) {
  std::vector<StatusIconView> out;
  if (!g.res) return out;
  std::vector<StatusEffect> fx;
  for (const auto& e : engine::ParentEffects(id)) fx.push_back({e.parent_type, e.parent, e.parent_flags, e.left, e.timed});
  uint32_t now = 0;
  bool running = false;
  engine::WorldTime(&now, &running);
  const int round = engine::RoundLengthTicks() ? engine::RoundLengthTicks() : kRoundTicksPst;
  for (const StatusIcon& s : CollectStatus(id, fx, now, round, &g.status_spans, max_icons * 2)) {
    std::string bam;
    int32_t name = -1;
    if (s.parent_type == 1) {
      if (const SpellFacts* f = g.res->Spell(s.parent)) bam = f->icon, name = f->name;
    } else if (const ItemFacts* f = g.res->Item(s.parent)) {
      bam = f->icon, name = f->name;
    }
    if (bam.empty()) continue;  // no icon to show (helper spells)
    const auto size = g.res->BamSize(bam);
    if (size.first <= 0) continue;
    out.push_back(MakeIconView(s, bam, name, size.first, size.second, box));
    if (out.size() >= max_icons) break;
  }
  return out;
}

// The creature under the cursor (anyone alive in the leader's area; combatants only in a fight), its
// status icons and, for a foe in a fight, the chance that the active party member (or the leader) hits it
// with the engine's own THAC0 and AC.
void FillHover(HoverView* v) {
  int wx = 0, wy = 0;
  if (g.hover_at.x >= 0) {
    wx = g.hover_at.x, wy = g.hover_at.y;  // test probe PSTF_HoverAt
  } else if (!engine::CursorWorld(&wx, &wy)) {
    return;
  }
  std::vector<HoverCandidate> cand;
  if (g.running) {
    for (int32_t id : g.in) {
      engine::SpriteInfo o;
      if (engine::GetSprite(id, &o) && o.alive) cand.push_back({id, o.x, o.y});
    }
  } else {
    for (const auto& s : engine::AllSprites())
      if (s.area == g.area && s.alive) cand.push_back({s.id, s.x, s.y});
  }
  const int32_t id = PickHovered(cand, wx, wy);
  if (id == -1) return;
  engine::SpriteInfo o;
  if (!engine::GetSprite(id, &o)) return;
  v->on = true;
  v->fight = g.running;
  v->name_ref = o.name_ref;
  v->name = Who(id);
  v->hp = o.hp;
  v->max_hp = o.max_hp;
  v->icons = StatusIconsOf(id, kStatusHoverPx, kHoverIcons);
  if (!g.running) return;
  const Combatant* c = g.dir->Find(id);
  v->foe = c && c->r.side == Side::Hostile;
  const int32_t me = PartySlot(g.current) >= 0 ? g.current : (g.party.empty() ? -1 : g.party[0]);
  engine::CombatStats a, t;
  if (v->foe && me != -1 && engine::GetCombatStats(me, &a) && engine::GetCombatStats(id, &t)) {
    v->ac = t.ac;
    v->needs = NeedsToHit(a.thac0, t.ac);
    v->chance = HitChancePercent(a.thac0, t.ac);
  }
}

// Every frame (all screens): our menus on the stack where they belong; on the world screen and the menu
// screens, the status icons next to the portraits (4 times a second) and the hover card (world only).
void UpdateStatusHud(engine::Screen scr) {
  if (!g.hud) return;
  std::string code = "if pstfHudGuard then pstfHudGuard() end";
  const bool world = scr == engine::Screen::World;
  if (world || scr == engine::Screen::Other) {
    const uint32_t ms = GetTickCount();
    if (ms - g.status_t0 >= 250 || g.status_t0 == 0 || world) {
      if (ms - g.status_t0 >= 250 || g.status_t0 == 0) {
        g.status_t0 = ms | 1u;
        g.status_party.clear();
        for (int32_t id : engine::PartyIds()) g.status_party.push_back(StatusIconsOf(id, kStatusPortraitPx, kStatusIcons));
      }
      HoverView h;
      if (world) FillHover(&h);
      code += " " + BuildStatusLua(g.status_party, h) + " pstfStatusLayout()";
    }
  }
  const std::string err = engine::LuaDoString(code.c_str());
  if (!err.empty()) {
    static int reported = 0;
    if (reported++ < 5) Log("tbc: status hud update failed: %s", err.c_str());
  }
}

// A d20 (icosahedron) tumbling in the dice panel, drawn by the engine in the world pass.
void DrawDie() {
  if (!g.dice_on || g.dice_ui.x < 0) return;
  int cx = 0, cy = 0, ex = 0, ey = 0;
  if (!engine::UiToWorld(g.dice_ui.x, g.dice_ui.y, &cx, &cy) || !engine::UiToWorld(g.dice_ui.x + 30, g.dice_ui.y, &ex, &ey))
    return;
  const double r = ex - cx;  // 30 UI px in world units
  const uint32_t ms = DiceMs();
  const double land = DiceTiming::Landed(ms) ? 1.0 : ms / double(DiceTiming::kTumble);
  const double ang = (1.0 - land) * (1.0 - land) * 11.0 + 0.35;  // slows down as it lands
  static const double p = 1.6180339887;
  static const double V[12][3] = {{0, 1, p}, {0, -1, p}, {0, 1, -p}, {0, -1, -p}, {1, p, 0}, {-1, p, 0},
                                  {1, -p, 0}, {-1, -p, 0}, {p, 0, 1}, {-p, 0, 1}, {p, 0, -1}, {-p, 0, -1}};
  const double ca = std::cos(ang), sa = std::sin(ang), cb = std::cos(ang * 0.7 + 0.4), sb = std::sin(ang * 0.7 + 0.4);
  double P[12][3];
  for (int i = 0; i < 12; ++i) {
    const double x = V[i][0], y = V[i][1], z = V[i][2];
    const double x1 = x * ca + z * sa, z1 = -x * sa + z * ca;   // around y
    const double y2 = y * cb - z1 * sb, z2 = y * sb + z1 * cb;  // around x
    P[i][0] = x1 / 1.902, P[i][1] = y2 / 1.902, P[i][2] = z2 / 1.902;
  }
  const bool verdict = DiceTiming::Verdict(ms);
  const uint32_t bone = 0xB0D4E2, gold = 0x48D0FF, red = 0x283CC8, ink = 0x0A0E14;
  const uint32_t edge = !verdict ? bone : g.dice.success ? gold : red;
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < 12; ++i)
      for (int j = i + 1; j < 12; ++j) {
        const double dx = V[i][0] - V[j][0], dy = V[i][1] - V[j][1], dz = V[i][2] - V[j][2];
        if (std::abs(dx * dx + dy * dy + dz * dz - 4.0) > 0.01) continue;  // icosahedron edge = 2
        const bool front = P[i][2] + P[j][2] > -0.35;
        if (!front) continue;
        engine::DrawWorldLine(cx + static_cast<int>(P[i][0] * r), cy - static_cast<int>(P[i][1] * r),
                              cx + static_cast<int>(P[j][0] * r), cy - static_cast<int>(P[j][1] * r),
                              pass ? edge : ink, pass ? 1.3f : 2.6f);
      }
  }
}

void DrawOverlay() {
  DrawMovementLine();
  DrawDie();
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
  FillDice(&v);
  v.slot = PartySlot(g.current);
  v.move_ft = g.econ.move_ft;
  v.max_ft = g.econ.max_ft;
  v.action_used = g.econ.action_used;
  v.acting = g.econ.acting;
  v.attacks_left = g.econ.want_swings > 0 ? g.econ.SwingsLeft() : -1;
  v.attacks_round = g.econ.want_swings;
  {
    const uint32_t ms = GetTickCount();
    g.floats.erase(std::remove_if(g.floats.begin(), g.floats.end(), [&](const State::Float& f) {
                     return static_cast<int32_t>(f.until - ms) <= 0;
                   }), g.floats.end());
    for (const auto& f : g.floats) {
      engine::SpriteInfo o;
      if (!engine::GetSprite(f.id, &o)) continue;
      int wx = o.x, wy = o.y, ux = 0, uy = 0;
      engine::InterpDrawPos(f.id, &wx, &wy);
      if (!engine::WorldToUi(wx, wy - 90, &ux, &uy)) continue;  // above its head
      v.floats.push_back({ux, uy, f.text});
    }
  }
  if (now < g.notice_until) v.msg = g.notice;
  engine::SpriteInfo me;
  if (g.current != -1 && engine::GetSprite(g.current, &me)) {
    std::vector<Pt> nodes;
    for (const auto& p : engine::PathPoints(g.current)) nodes.push_back({p.first, p.second});
    g.line_preview = false;
    g.unreachable = false;
    if (!v.party_turn) nodes.clear();  // enemies' and allies' paths are not shown
    // Aiming a spell, ability or item: no hover line (the engine's own path shows once the order
    // needs a walk, e.g. a touch spell out of reach).
    const bool aiming = engine::GameCursorState() != 0;
    if (nodes.empty() && g.econ.party && g.econ.MoveLeft() && !g.econ.acting && !aiming) nodes = HoverPreview(me);
    const Pt from{me.x, me.y};
    g.line.clear();
    if (!nodes.empty()) {
      g.line.push_back(from);
      g.line.insert(g.line.end(), nodes.begin(), nodes.end());
      v.path_ft = static_cast<int>(PathFeet(from, nodes) + 0.5);
      engine::WorldToUi(nodes.back().x, nodes.back().y, &v.label_x, &v.label_y);
    }
  }
  // BG3 hotbar: every weapon, spell, quick item and ability of the active party member, collected from
  // the engine's own action bar states (selection is locked to it) at the turn start and every 1.5 s.
  std::string bar;
  if (v.party_turn && (g.bar_t0 == 0 || GetTickCount() - g.bar_t0 > 1500)) {
    g.bar_t0 = GetTickCount() | 1u;
    bar = " pstfBarCollect()";
  }
  if (!v.party_turn) bar = " pstfBar = {}";
  const std::string code = BuildHudLua(v) + bar +
                           " pstfTbcLayout()";
  const std::string err = engine::LuaDoString(code.c_str());
  if (!err.empty()) {
    static int reported = 0;
    if (reported++ < 5) Log("tbc: hud update failed: %s", err.c_str());
  }
}

void TestTick() {
  if (!g.test_tick) return;
  // Test scripts count these calls as "frames". With sub-tick interpolation the loop runs faster than
  // the world, so the test tick stays on the iterations that actually advanced the world.
  if (!engine::InterpTickFrame()) return;
  const std::string e = engine::LuaDoString("if pstfTestTick then pstfTestTick() end");
  if (!e.empty()) {
    Log("tbc: test tick error: %s", e.c_str());
    engine::LuaDoString("pstfTestTick = nil");
  }
}

void OnTick(uint64_t frame) {
  // With sub-tick interpolation the loop runs faster than the world: turn decisions, scans and the test
  // tick belong to the iterations that advanced the world, not to the extra draw-only ones.
  if (!engine::InterpTickFrame()) return;
  if (!g.running) TestTick();
  const engine::Screen scr = engine::ActiveScreen();
  UpdateStatusHud(scr);
  if (scr == engine::Screen::Other) {
    // Inventory, character sheet, journal, map...: the fight waits as it is (same turn, same order).
    if (g.running && !g.paused_screen) Log("tbc: fight waits while a menu screen is up");
    g.paused_screen = g.running;
    if (g.running) TestTick();  // tests drive the screens too
    return;
  }
  if (scr != engine::Screen::World) {
    EndFight("left the world screen");
    g.have_time = false;
    g.paused_screen = false;
    return;
  }
  if (g.paused_screen) {
    g.paused_screen = false;
    Log("tbc: back on the world screen, the fight goes on");
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

  // Effect round clock: nobody's timed effects or natural regeneration move with world time.
  if (g.round_effects && engine::EffectClockInstalled() && dt > 0 && dt < 1000)
    for (int32_t id : g.in) engine::ShiftEffectClock(id, static_cast<int32_t>(dt));
  if (g.music && !g.party.empty()) engine::KeepBattleMusic(g.party[0], 150);
  if (g.sight && !g.party.empty()) {
    std::vector<int32_t> foes;
    for (int32_t id : g.in) {
      const Combatant* fc = g.dir->Find(id);
      if (PartySlot(id) < 0 && fc && fc->r.side == Side::Hostile) foes.push_back(id);
    }
    // The party sees every foe in the fight: the fog lifts around it as around a party member, and it
    // is drawn wherever it stands (as Clairvoyance shows creatures).
    engine::RevealCombatants(g.party[0], foes, g.sight_radius);
    engine::KeepVisible(foes);
  }
  if (g.no_bored) engine::ResetBoredTimer();
  // A waiting combatant that was bumped (pushed aside by a walker) must not walk in place: no path.
  for (int32_t id : g.in) {
    if (!Held(id)) continue;
    engine::SpriteInfo o;
    if (engine::GetSprite(id, &o) && o.has_path) {
      engine::DropPath(id);
      CLog("PUSH", Name(id) + " was pushed while waiting: path dropped");
    }
  }
  if (g.round_effects) engine::WatchProjectiles();
  TestTick();  // after the clock shift: tests read the same remaining ticks the engine compares
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
  if (cur == g.nudged) {
    static uint16_t last = 0xFFFF;
    static bool path = false;
    if (s.action != last || s.has_path != path)
      Log("tbc: %s after the order: action %u path %d at %d,%d", Who(cur).c_str(), s.action, s.has_path ? 1 : 0, s.x, s.y);
    last = s.action, path = s.has_path;
  }
  const double moved = FeetBetween(g.last_x, g.last_y, s.x, s.y);
  g.last_x = s.x, g.last_y = s.y;
  if (s.action != g.clog_action) {
    CLog("ACTION", Name(cur) + " " + ActionName(s.action) + (s.has_path ? " (walking)" : ""));
    g.clog_action = s.action;
  }
  // The camera is not here: it follows the DRAWN position on every loop iteration (CameraTick), not the
  // position this tick recorded.
  g.clock.Feed(dt, s.busy);  // busy-tick count for the log only
  // A quick item used between two frames still takes the action (UseItem = 34).
  const uint16_t fed = g.items && engine::TookItemAction(cur) && IsIdleAction(s.action) ? 34 : s.action;
  const EconomyStep st = g.econ.Feed(dt, moved, s.has_path, fed);
  if (!g.econ.party && s.has_path && !g.steady_ai) {
    // Sure of itself: its script does not re-plan the walk every pass (stop-and-go); it walks there in
    // one go, then acts. The engine lifts SetInterrupt(FALSE) when its action queue runs empty.
    g.steady_ai = true;
    engine::SetNoInterrupt(cur);
  }
  g.hold_current = st.hold_walk;
  if (st.nudge) {
    // Its own script picked nothing (many creatures only wait or wander): the engine's attack on the
    // nearest enemy it can see, as a real-time fight would have it retaliate.
    // Hostiles go for the nearest living party member (PlayerN needs no line of sight, unlike
    // NearestEnemyOf); allies for the nearest enemy they can see.
    std::string order = "Attack(NearestEnemyOf(Myself))";
    const Combatant* me = g.dir->Find(cur);
    if (me && me->r.side == Side::Hostile) {
      long long best = -1;
      for (size_t i = 0; i < g.party.size(); ++i) {
        engine::SpriteInfo p;
        if (!engine::GetSprite(g.party[i], &p) || !p.alive || p.area != s.area) continue;
        const long long dx = p.x - s.x, dy = p.y - s.y, d = dx * dx + dy * dy;
        if (best < 0 || d < best) best = d, order = "Attack(Player" + std::to_string(i + 1) + ")";
      }
    }
    engine::ClearAI(cur);  // its script's WaitRandom would hold the order back
    g.nudged = cur;
    const bool ok = engine::EvalAs(cur, order);
    // Its own script must not replace the order with its waits (SetInterrupt(FALSE), set directly:
    // a queued SetInterrupt would itself be replaced). The engine lifts it when the queue is empty.
    engine::SetNoInterrupt(cur);
    Log("tbc: %s idle on its turn: ordered %s (%s)", Who(cur).c_str(), order.c_str(), ok ? "ok" : "failed");
    CLog("AI", Name(cur) + (s.action ? " only waits (" + ActionName(s.action) + ", ignored)" : " chose nothing") +
                   ": ordered " + order);
  }
  if (st.stop != Stop::None) {
    static const char* kStop[] = {"", "Can't get there this turn: no movement left",
                                  "No action left this turn", "Attack round done"};
    CLog("STOP", Name(cur) + ": " + kStop[static_cast<int>(st.stop)]);
    Log("tbc: %s: stop (%s), action %u, %.1f/%.1f ft left", Who(cur).c_str(),
        kStop[static_cast<int>(st.stop)], s.action, g.econ.move_ft, g.econ.max_ft);
    engine::ClearAI(cur);
    engine::DropPath(cur);
    if (g.econ.party && st.stop != Stop::RoundDone) {
      g.notice = kStop[static_cast<int>(st.stop)];
      g.notice_until = now + 45;
    }
  }
  g.since_action = g.econ.acting ? 0 : g.since_action + dt;
  bool wait_spell = false;
  if ((st.end_turn || g.end_requested) && g.resolve_ticks < kFlightCapTicks) {
    const Pending pend = CasterProjectiles(cur, s.area);
    int n = pend.flying;
    if (g.resolve_ticks < kResolveCapTicks) {
      n += pend.lingering;
      for (int32_t id : g.in) n += engine::ResolvingOnsets(id);  // e.g. Cone of Cold's 100-tick onset
    }
    // A projectile appears up to a second after the cast animation ends: give it 25 ticks.
    // A melee or ranged attack has no projectile to wait for (an arrow is part of its swing).
    const bool may_cast = g.econ.act_action != 3;
    if (n > 0 || (g.econ.action_used && may_cast && g.since_action < 25 && g.resolve_ticks < kResolveCapTicks)) {
      wait_spell = true;
      g.resolve_ticks += dt;
      if (!g.resolving_logged) {
        g.resolving_logged = true;
        if (n > 0) Log("tbc: %s: turn waits for its spell (%d projectile%s or onsets)", Who(cur).c_str(), n, n == 1 ? "" : "s");
        if (n > 0) CLog("SPELL", Name(cur) + ": turn waits for its spell to land (" + std::to_string(n) + " projectiles/onsets)");
        else g.resolving_logged = false;
      }
    }
  }
  if ((st.end_turn || g.end_requested) && !wait_spell) {
    std::string hps;
    for (int32_t id : g.in) {
      engine::SpriteInfo o;
      if (engine::GetSprite(id, &o)) hps += " " + Who(id) + "=" + std::to_string(o.hp);
    }
    {
      char b[160];
      const char* why = g.end_requested ? "End Turn"
                        : (!g.econ.party && IsWaitAction(s.action)) ? "its script only waits (ignored)"
                        : g.econ.action_used && !g.econ.MoveLeft() ? "action and movement spent"
                        : g.econ.action_used ? "acted" : "nothing to do";
      std::snprintf(b, sizeof b, " ends turn: %s; action %s, %.1f of %.1f ft walked, %u ticks", why,
                    g.econ.action_used ? "used" : "unused", g.econ.max_ft - g.econ.move_ft, g.econ.max_ft, g.econ.total);
      CLog("END", Name(cur) + b);
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
  if (g.rolls) TakeRolls();
  LogHpChanges();
  UpdateHud(now);
}

double CamNowMs() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The camera runs inside the DRAW pass (render.h InstallWorldPreDraw), not on world ticks: with
// [smooth] interpolation movers are drawn gliding between ticks, so a tick-rate camera jumps by one
// whole tick step while the creature slides — jaggy running whenever the camera is centred on them.
// Easing by real time (camera_logic.h) gives the same feel at any frame rate. With interpolation off the
// draw pass runs at EE's own rate, so this is exactly the old behaviour.
void CameraStep() {
  if (!g.camera || !g.running) return;
  if (!g.cam_smooth && !engine::InterpTickFrame()) return;  // camera_smooth=0: the old tick-rate camera
  if (engine::ActiveScreen() != engine::Screen::World) return;
  const int32_t cur = g.dir->Current();
  if (cur < 0) return;

  // Real time at sub-millisecond resolution: GetTickCount moves in 15.6 ms steps, so at 60 draws/s
  // most frames would see dt = 0 and the camera would stand still, then lurch.
  const double now = CamNowMs();
  const double dt = g.cam_last > 0.0 && now >= g.cam_last ? now - g.cam_last : 0.0;
  g.cam_last = now;

  if (g.cam_t0) {
    // A quick pan (about a quarter second, eased) to whoever's turn it is.
    const uint32_t ms = GetTickCount();
    const double t = std::min(1.0, static_cast<double>(ms - g.cam_t0) / 260.0);
    const double e = t * t * (3.0 - 2.0 * t);
    engine::CenterView(g.cam_from.x + static_cast<int>(static_cast<double>(g.cam_to.x - g.cam_from.x) * e),
                       g.cam_from.y + static_cast<int>(static_cast<double>(g.cam_to.y - g.cam_from.y) * e));
    if (t >= 1.0) g.cam_t0 = 0;
    g.cam_ease.locked = false;
    return;
  }

  engine::SpriteInfo s;
  if (!engine::GetSprite(cur, &s)) return;
  int sx = s.x, sy = s.y;
  engine::InterpDrawPos(cur, &sx, &sy);  // where it is shown; integer position when interpolation is off
  // Standing still, the player scrolls freely: only a creature walking on a path pulls the camera.
  if (!s.has_path) {
    g.cam_ease.locked = false;
    return;
  }

  int cx = 0, cy = 0;
  if (!engine::ViewCenter(&cx, &cy)) return;
  // The view moved under us (the player scrolled, the engine centred): ease from where it is now.
  if (std::abs(cx - g.cam_ease.X()) > 1 || std::abs(cy - g.cam_ease.Y()) > 1) g.cam_ease.Reset(cx, cy);
  if (g.cam_ease.Distant(sx, sy, kCamSnapPx)) {
    g.cam_ease.Reset(sx, sy);  // an area away: show it at once, no easing across the whole area
    g.cam_ease.locked = true;
  } else {
    g.cam_ease.Step(sx, sy, dt, kCamTauMs, kCamLockPx);
  }
  engine::CenterView(g.cam_ease.X(), g.cam_ease.Y());
  if (!g.follow) {
    CLog("CAMERA", "follows " + Name(cur));
    g.follow = true;
  }
  ++g.cam_steps;
}

// Fallback when the draw-pass hook could not be installed: follow once per tick, as before.
void CameraTick(uint64_t /*frame*/) { CameraStep(); }

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

// PSTF_SpritePos(id[, quiet]) -> x, y, hp, ea, name ref, walking (or -1 if no such creature).
// "walking" = the creature has a path, which is what the follow camera uses; the engine's own position
// can advance only every other tick, so tests must not decide "moving" from position deltas alone.
int __cdecl LuaSpritePos(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  engine::SpriteInfo s;
  const bool ok = engine::GetSprite(id, &s);
  if (engine::LuaNumberArg(L, 2, 0) == 0)
    Log("lua: PSTF_SpritePos(%X) -> %d,%d hp %d ea %u path %d", static_cast<unsigned>(id), s.x, s.y, s.hp, s.ea,
        s.has_path ? 1 : 0);
  engine::LuaPushNumber(L, ok ? s.x : -1);
  engine::LuaPushNumber(L, ok ? s.y : -1);
  engine::LuaPushNumber(L, ok ? s.hp : -1);
  engine::LuaPushNumber(L, ok ? s.ea : -1);
  engine::LuaPushNumber(L, ok ? s.name_ref : -1);  // TLK name (Infinity_FetchString)
  engine::LuaPushNumber(L, ok && s.has_path ? 1 : 0);
  return 6;
}

// PSTF_ViewCenter() -> x, y: the point the viewport is centred on (-1, -1 when no camera). Test probe
// for the camera follow: with interpolation it must change on iterations that did not advance the world.
int __cdecl LuaViewCenter(engine::LuaState* L) {
  int x = 0, y = 0;
  const bool ok = engine::ViewCenter(&x, &y);
  Log("lua: PSTF_ViewCenter -> %d,%d", ok ? x : -1, ok ? y : -1);
  engine::LuaPushNumber(L, ok ? x : -1);
  engine::LuaPushNumber(L, ok ? y : -1);
  return 2;
}

// PSTF_CameraSteps() -> camera follow steps since install, and 1 while the quick pan to a new turn is
// running (a pan is not a follow step). With [tbc] camera_smooth=1 and interpolation on the steps count
// drawn frames, so they grow faster than PSTF_SmoothTicks().
int __cdecl LuaCameraSteps(engine::LuaState* L) {
  Log("lua: PSTF_CameraSteps -> %llu (pan %d)", static_cast<unsigned long long>(g.cam_steps), g.cam_t0 ? 1 : 0);
  engine::LuaPushNumber(L, static_cast<double>(g.cam_steps));
  engine::LuaPushNumber(L, g.cam_t0 ? 1 : 0);
  return 2;
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


// --- effect / roll / action bar probes (tests) -------------------------------------------------
// PSTF_Effects(id) -> "op:type:left;..." of its timed and equipped effects (left = ticks to expiry).
int __cdecl LuaEffects(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  std::string s;
  for (const auto& e : engine::TimedEffects(id)) {
    char b[48];
    std::snprintf(b, sizeof b, "%d:%u:%d;", e.opcode, e.type, e.left);
    s += b;
  }
  Log("lua: PSTF_Effects(%X) -> %s persistent=%d", static_cast<unsigned>(id), s.c_str(), engine::PersistentCount(id));
  engine::LuaPushString(L, s.c_str());
  return 1;
}
// PSTF_HasEffect(id, opcode) -> ticks left of the first such effect (-1: none, 0: not timed).
int __cdecl LuaHasEffect(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const int op = static_cast<int>(engine::LuaNumberArg(L, 2, -1));
  int left = -1;
  for (const auto& e : engine::TimedEffects(id))
    if (e.opcode == op) {
      left = e.left;
      break;
    }
  if (engine::LuaNumberArg(L, 3, 0) == 0) Log("lua: PSTF_HasEffect(%X, %d) -> %d", static_cast<unsigned>(id), op, left);
  engine::LuaPushNumber(L, left);
  return 1;
}
// PSTF_Objects([source]) -> number of non-creature objects (of that caster); logs each.
int __cdecl LuaObjects(engine::LuaState* L) {
  const auto src = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -2));
  auto objs = engine::AllObjects();
  if (src == -3) {
    // new kinds only: not the area's doors, containers, regions, spawn points... seen at load
    static const uint8_t kStatic[] = {16, 17, 33, 64, 65, 97, 113};
    objs.erase(std::remove_if(objs.begin(), objs.end(), [&](const engine::ObjectInfo& o) {
                 return std::find(std::begin(kStatic), std::end(kStatic), o.type) != std::end(kStatic);
               }), objs.end());
  } else if (src != -2) {
    objs.erase(std::remove_if(objs.begin(), objs.end(), [&](const engine::ObjectInfo& o) { return o.source != src; }), objs.end());
  }
  for (const auto& o : objs) {
    std::string raw;
    for (int k = 0; k < 16; ++k) {
      char b[12];
      std::snprintf(b, sizeof b, " %X", static_cast<unsigned>(o.raw[k]));
      raw += b;
    }
    Log("lua: object %X type %u vtbl %llX src %X tgt %X at %d,%d raw+50:%s", static_cast<unsigned>(o.id), o.type,
        static_cast<unsigned long long>(o.vtable), static_cast<unsigned>(o.source), static_cast<unsigned>(o.target),
        o.x, o.y, raw.c_str());
  }
  engine::LuaPushNumber(L, static_cast<double>(objs.size()));
  return 1;
}
// PSTF_TbcCurrent() -> id whose turn it is (-1), round, party turn, action used, acting, feet left.
int __cdecl LuaTbcCurrent(engine::LuaState* L) {
  engine::LuaPushNumber(L, g.running ? g.current : -1);
  engine::LuaPushNumber(L, g.running && g.dir ? g.dir->Round() : 0);
  engine::LuaPushBool(L, g.running && g.econ.party);
  engine::LuaPushBool(L, g.running && g.econ.action_used);
  engine::LuaPushBool(L, g.running && g.econ.acting);
  engine::LuaPushNumber(L, g.running ? g.econ.move_ft : 0);
  return 6;
}
// PSTF_FightId(i) -> id of the i-th combatant (1-based, initiative order), -1 past the end.
int __cdecl LuaFightId(engine::LuaState* L) {
  const int i = static_cast<int>(engine::LuaNumberArg(L, 1, 0));
  int32_t id = -1;
  if (g.running && g.dir) {
    const auto order = g.dir->Order();
    if (i >= 1 && i <= static_cast<int>(order.size())) id = order[i - 1];
  }
  engine::LuaPushNumber(L, id);
  return 1;
}
// PSTF_MakeEnemy(id) -> the engine's Enemy() on that creature.
int __cdecl LuaMakeEnemy(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const bool ok = engine::MakeEnemy(id);
  Log("lua: PSTF_MakeEnemy(%X) -> %d", static_cast<unsigned>(id), ok ? 1 : 0);
  engine::LuaPushBool(L, ok);
  return 1;
}
// PSTF_EvalAs(id, bcs[, takeover]) -> queues BCS actions on any creature (engine console path, engine/sprites.h).
int __cdecl LuaEvalAs(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const std::string text = engine::LuaStringArg(L, 2);
  const bool takeover = engine::LuaNumberArg(L, 3, 0) != 0;  // clear its actions first, scripts wait
  if (takeover) engine::ClearAI(id);
  const bool ok = engine::EvalAs(id, text);
  if (takeover) engine::SetNoInterrupt(id);
  Log("lua: PSTF_EvalAs(%X, %s) -> %d", static_cast<unsigned>(id), text.c_str(), ok ? 1 : 0);
  engine::LuaPushBool(L, ok);
  return 1;
}
// PSTF_SetHp(id, hp): test helper (keeps a target alive across a spell suite) -> 1 when the engine took it.
int __cdecl LuaSetHp(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const int hp = static_cast<int>(engine::LuaNumberArg(L, 2, -1));
  const bool ok = engine::SetHpForTest(id, hp);
  Log("lua: PSTF_SetHp(%X, %d) -> %d", static_cast<unsigned>(id), hp, ok ? 1 : 0);
  engine::LuaPushNumber(L, ok ? 1 : 0);
  return 1;
}
// PSTF_HoverAt(x, y): the hover card uses this world point instead of the cursor (-1,-1: cursor).
int __cdecl LuaHoverAt(engine::LuaState* L) {
  g.hover_at = {static_cast<int>(engine::LuaNumberArg(L, 1, -1)), static_cast<int>(engine::LuaNumberArg(L, 2, -1))};
  return 0;
}
// PSTF_DiceAt(x, y): the HUD layout tells where the dice panel's d20 sits (UI coordinates).
int __cdecl LuaDiceAt(engine::LuaState* L) {
  g.dice_ui = {static_cast<int>(engine::LuaNumberArg(L, 1, -1)), static_cast<int>(engine::LuaNumberArg(L, 2, -1))};
  return 0;
}
// PSTF_RollProbe(kind, success, roll): test probe, puts a roll on the dice panel (and plays the
// save sound) as if the engine had rolled it for the active creature.
int __cdecl LuaRollProbe(engine::LuaState* L) {
  engine::RollEvent e;
  e.kind = engine::LuaNumberArg(L, 1, 0) == 1 ? engine::RollEvent::Save : engine::RollEvent::Attack;
  e.success = engine::LuaNumberArg(L, 2, 0) != 0;
  e.roll = static_cast<int>(engine::LuaNumberArg(L, 3, 10));
  e.total = e.roll + 2;
  e.needs = 12;
  e.actor = g.current;
  e.target = g.party.empty() ? -1 : g.party[0];
  g.dice = e;
  g.dice_on = g.running;
  g.dice_t0 = GetTickCount();
  g.dice_sound = e.kind == engine::RollEvent::Save;
  Log("lua: PSTF_RollProbe -> %s", g.dice_on ? "shown" : "no fight");
  return 0;
}
// PSTF_SetName(id, name): the HUD's Lua tells the combat log a creature's TLK name.
int __cdecl LuaSetName(engine::LuaState* L) {
  g.names[static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1))] = engine::LuaStringArg(L, 2);
  return 0;
}
// PSTF_WorldTicks() -> the world clock.
int __cdecl LuaWorldTicks(engine::LuaState* L) {
  uint32_t t = 0;
  bool r = false;
  engine::WorldTime(&t, &r);
  engine::LuaPushNumber(L, t);
  return 1;
}
// PSTF_Near(radius) -> logs every creature within radius px of the party leader (id, ea, hp, pos, name).
int __cdecl LuaNear(engine::LuaState* L) {
  const double r = engine::LuaNumberArg(L, 1, 1500);
  const auto party = engine::PartyIds();
  engine::SpriteInfo me;
  int n = 0;
  if (!party.empty() && engine::GetSprite(party[0], &me)) {
    for (const auto& s : engine::AllSprites()) {
      if (s.area != me.area) continue;
      const double dx = s.x - me.x, dy = s.y - me.y;
      if (dx * dx + dy * dy > r * r) continue;
      ++n;
      Log("lua: near %X ea %u hp %d/%d alive %d at %d,%d name %d best %d", static_cast<unsigned>(s.id), s.ea, s.hp,
          s.max_hp, s.alive ? 1 : 0, s.x, s.y, s.name_ref, s.bestiary);
    }
  }
  engine::LuaPushNumber(L, n);
  return 1;
}
// PSTF_Stats(id) -> thac0, ac, hp, maxhp
int __cdecl LuaStats(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  engine::CombatStats c;
  const bool ok = engine::GetCombatStats(id, &c);
  Log("lua: PSTF_Stats(%X) -> thac0 %d ac %d hp %d/%d move %u active %s", static_cast<unsigned>(id), c.thac0, c.ac, c.hp,
      c.max_hp, engine::MoveScale(id), engine::ActiveFlags(id).c_str());
  engine::LuaPushNumber(L, ok ? c.thac0 : -1);
  engine::LuaPushNumber(L, ok ? c.ac : -1);
  engine::LuaPushNumber(L, ok ? c.hp : -1);
  engine::LuaPushNumber(L, ok ? c.max_hp : -1);
  return 4;
}
// PSTF_Bar() -> logs the 12 action bar slots as the engine has them now.
int __cdecl LuaBar(engine::LuaState* L) {
  std::string s;
  for (const auto& b : engine::ActionBarButtons()) s += b.icon + (b.disabled ? "(x) " : " ");
  Log("lua: PSTF_Bar -> %s", s.c_str());
  engine::LuaPushString(L, s.c_str());
  return 1;
}
// PSTF_BarHex(slot): logs the 0x2A0 bytes of an action bar slot (probe).
int __cdecl LuaBarHex(engine::LuaState* L) {
  const int slot = static_cast<int>(engine::LuaNumberArg(L, 1, 0));
  (void)slot;
  Log("lua: bar strings: %s", engine::ScanBarStrings().c_str());
  return 0;
}
// PSTF_BarSlots() -> "ICON:1;ICON:0;:0;..." the 12 slots after a SetState (1 = usable), for the HUD bar.
int __cdecl LuaBarSlots(engine::LuaState* L) {
  std::string s;
  for (const auto& b : engine::ActionBarButtons()) s += b.icon + (b.disabled ? ":0;" : ":1;");
  engine::LuaPushString(L, s.c_str());
  return 1;
}

// PSTF_CenterOn(id): the initiative card click: the camera centres on that combatant (where it is drawn).
int __cdecl LuaCenterOn(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  engine::SpriteInfo o;
  const bool ok = engine::GetSprite(id, &o);
  if (ok) {
    int x = o.x, y = o.y;
    engine::InterpDrawPos(id, &x, &y);
    g.cam_t0 = 0;  // a pan to the turn owner in progress gives way
    engine::CenterView(x, y);
  }
  Log("lua: PSTF_CenterOn(%X) -> %d", static_cast<unsigned>(id), ok ? 1 : 0);
  engine::LuaPushBool(L, ok);
  return 1;
}
// PSTF_TbcAttacks() -> attacks this round (0: unknown), swings so far, APR x2 of the active creature.
int __cdecl LuaTbcAttacks(engine::LuaState* L) {
  engine::LuaPushNumber(L, g.running ? g.econ.want_swings : 0);
  engine::LuaPushNumber(L, g.running ? g.econ.swings : 0);
  engine::LuaPushNumber(L, g.running ? g.apr_halves : 0);
  return 3;
}
// PSTF_SearchByte(x, y[, id]) -> occupancy byte of the search-map cell under world point x,y (probe).
int __cdecl LuaSearchByte(engine::LuaState* L) {
  const auto ids = engine::PartyIds();
  const int x = static_cast<int>(engine::LuaNumberArg(L, 1, -1)), y = static_cast<int>(engine::LuaNumberArg(L, 2, -1));
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 3, ids.empty() ? -1 : ids[0]));
  const int b = engine::SearchMapByte(id, x / 16, y / 12);
  Log("lua: PSTF_SearchByte(%d,%d) -> %d", x, y, b);
  engine::LuaPushNumber(L, b);
  return 1;
}
// PSTF_PreviewPts(x, y) -> "x,y;x,y;..." of the leader's preview path to world point x,y (probe), and
// PSTF_EnginePath(id) -> the same for the engine's current walk path of that creature.
int __cdecl LuaPreviewPts(engine::LuaState* L) {
  const auto ids = engine::PartyIds();
  const Pt to{static_cast<int>(engine::LuaNumberArg(L, 1, -1)), static_cast<int>(engine::LuaNumberArg(L, 2, -1))};
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 3, ids.empty() ? -1 : ids[0]));
  std::string out;
  engine::SpriteInfo me;
  if (engine::GetSprite(id, &me))
    for (const Pt& p : PreviewPoints(PreviewCells(id, {me.x, me.y}, to), to))
      out += std::to_string(p.x) + "," + std::to_string(p.y) + ";";
  Log("lua: PSTF_PreviewPts(%d,%d) -> %s", to.x, to.y, out.c_str());
  engine::LuaPushString(L, out.c_str());
  return 1;
}
int __cdecl LuaEnginePath(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  std::string out;
  for (const auto& p : engine::PathPoints(id)) out += std::to_string(p.first) + "," + std::to_string(p.second) + ";";
  Log("lua: PSTF_EnginePath(%X) -> %s", static_cast<unsigned>(id), out.c_str());
  engine::LuaPushString(L, out.c_str());
  return 1;
}

// PSTF_IconSize(bam) -> w, h of its first frame (0, 0 when unknown): hotbar icons keep their aspect.
int __cdecl LuaIconSize(engine::LuaState* L) {
  const std::string name = engine::LuaStringArg(L, 1);
  const auto s = g.res ? g.res->BamSize(name) : std::pair<int, int>{0, 0};
  engine::LuaPushNumber(L, s.first);
  engine::LuaPushNumber(L, s.second);
  return 2;
}

// PSTF_Status(id) -> number of status icons; logs "BAM name rounds buff|debuff frame-rects;..." and the raw
// timed parent effects (type res flags left).
int __cdecl LuaStatus(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  std::string raw;
  for (const auto& e : engine::ParentEffects(id))
    raw += std::to_string(e.parent_type) + ":" + e.parent + ":" + std::to_string(e.parent_flags) + ":" + std::to_string(e.left) + " ";
  const auto icons = StatusIconsOf(id, kStatusPortraitPx, kStatusIcons);
  std::string out;
  for (const auto& v : icons)
    out += v.bam + " " + std::to_string(v.name_ref) + " r" + std::to_string(v.rounds) + (v.debuff ? " debuff" : " buff") +
           " frame" + std::to_string(v.frame.size()) + ";";
  Log("lua: PSTF_Status(%X) -> %zu: %s | effects %s", static_cast<unsigned>(id), icons.size(), out.c_str(), raw.c_str());
  engine::LuaPushNumber(L, static_cast<double>(icons.size()));
  return 1;
}

std::string Bg3Transform(const std::string& ui) {
  std::string why;
  std::string out = Bg3Layout(ui, &why, g.party_max);
  if (why.empty()) Log("tbc: BG3 layout: %d portraits on the left edge, hotbar in the bottom bar", g.party_max);
  else Log("tbc: BG3 layout not applied: %s", why.c_str());
  return out;
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
  g.round_effects = IniBool(ini, "tbc.round_effects", true);
  {
    const int secs = IniInt(ini, "tbc.round_seconds", 5);
    g.effect_round_ticks = static_cast<uint32_t>(secs > 0 ? secs : 5) * 15u;
  }
  g.run = IniBool(ini, "tbc.run", true);
  g.run_scale_other = static_cast<uint8_t>(IniInt(ini, "tbc.run_scale", 11));
  g.music = IniBool(ini, "tbc.battle_music", true);
  g.camera = IniBool(ini, "tbc.camera_follow", true);
  g.cam_smooth = IniBool(ini, "tbc.camera_smooth", true);
  g.dice_panel = IniBool(ini, "tbc.dice_panel", false);
  g.save_sounds = IniBool(ini, "tbc.save_sounds", true);
  g.sight = IniBool(ini, "tbc.combat_sight", true);
  g.sight_radius = IniInt(ini, "tbc.sight_radius", g.sight_radius);
  g.select_voice = IniBool(ini, "tbc.select_voice", true);
  g.no_bored = IniBool(ini, "tbc.no_bored", true);
  g.party_max = IniInt(ini, "party.size", 6) >= 8 && IniBool(ini, "party.enabled", false) ? 8 : 6;
  {
    const int feet = IniInt(ini, "tbc.move_feet", 31);
    g.move_feet = feet > 0 ? feet : 31;
  }
  {
    char* v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, "PSTF_TBC_AUTOPARTY") == 0 && v) g.auto_party = v[0] == '1';
    free(v);
    v = nullptr;
    if (_dupenv_s(&v, &n, "PSTF_TEST_TICK") == 0 && v) g.test_tick = v[0] == '1';
    free(v);
    v = nullptr;
    if (_dupenv_s(&v, &n, "PSTF_TBC_NOFIGHT") == 0 && v) g.no_fight = v[0] == '1';
    free(v);
  }
  {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    g.sound_dir = dir + L"pst_forever\\sounds\\";
    g.res = std::make_unique<GameRes>(dir.substr(0, dir.size() - 1));
    char exe8[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe8, MAX_PATH);
    std::string d8(exe8);
    d8 = d8.substr(0, d8.find_last_of("\\/") + 1);
    g.clog_path = d8 + "pst_forever\\logs\\combat.log";
    if (FILE* f = nullptr; fopen_s(&f, g.clog_path.c_str(), "w") == 0 && f) {  // fresh each game start
      std::fputs("# pst_forever combat log: wall clock, world tick, round, ticks into the turn | event\n", f);
      std::fclose(f);
    }
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
  // Presentation, so it belongs to the draw pass, not to world ticks (CameraStep). If that hook cannot be
  // installed, the camera still follows, just once per tick as before.
  g.cam_pre = g.camera ? engine::InstallWorldPreDraw(&CameraStep) : false;
  if (g.camera && !g.cam_pre) AddTickHandler(&CameraTick);
  if (g.round_effects && !engine::InstallEffectClock(&FrozenCombatant)) g.round_effects = false;
  if (g.round_effects) engine::SetOnsetRule(&OnsetKind);
  // A spell's projectile (a flying knife, a cloud) moves only in its caster's turn.
  if (g.round_effects) engine::SetProjectileGate(&ProjectileRuns);
  g.items = engine::InstallItemUseHooks();
  {
    char* v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, "PSTF_FEEDBACK_LOG") == 0 && v && v[0] == '1') engine::InstallFeedbackLog();
    free(v);
  }
  if (IniBool(ini, "tbc.hud", true)) {
    if (engine::HudAvailable() && engine::InstallMenuHook()) {
      const bool bg3 = IniBool(ini, "tbc.bg3_layout", true);
      engine::AddMenuText(BuildHudMenu(bg3, g.party_max));
      if (bg3) engine::AddMenuTransform(&Bg3Transform);
      g.hud = true;
      g.draw = engine::InstallWorldDraw(&DrawOverlay);
      g.rolls = engine::InstallRollHooks();
      if (!g.draw) Log("tbc: world overlay not installed; no movement line");
    } else {
      Log("tbc: HUD functions differ from the manifest; turn-based HUD off (combat still on)");
    }
  }
  dev::AddLuaFunction("PSTF_TbcState", &LuaState);
  dev::AddLuaFunction("PSTF_EndTurn", &LuaEndTurn);
  dev::AddLuaFunction("PSTF_MakeEnemyNearest", &LuaMakeEnemyNearest);
  dev::AddLuaFunction("PSTF_SpritePos", &LuaSpritePos);
  dev::AddLuaFunction("PSTF_ViewCenter", &LuaViewCenter);
  dev::AddLuaFunction("PSTF_CameraSteps", &LuaCameraSteps);
  dev::AddLuaFunction("PSTF_PreviewAt", &LuaPreviewAt);
  dev::AddLuaFunction("PSTF_PartyId", &LuaPartyId);
  dev::AddLuaFunction("PSTF_Note", &LuaNote);
  dev::AddLuaFunction("PSTF_PathFt", &LuaPathFt);
  dev::AddLuaFunction("PSTF_WalkCost", &LuaWalkCost);
  dev::AddLuaFunction("PSTF_Effects", &LuaEffects);
  dev::AddLuaFunction("PSTF_HasEffect", &LuaHasEffect);
  dev::AddLuaFunction("PSTF_Objects", &LuaObjects);
  dev::AddLuaFunction("PSTF_Stats", &LuaStats);
  dev::AddLuaFunction("PSTF_Bar", &LuaBar);
  dev::AddLuaFunction("PSTF_Near", &LuaNear);
  dev::AddLuaFunction("PSTF_BarSlots", &LuaBarSlots);
  dev::AddLuaFunction("PSTF_BarHex", &LuaBarHex);
  dev::AddLuaFunction("PSTF_TbcCurrent", &LuaTbcCurrent);
  dev::AddLuaFunction("PSTF_MakeEnemy", &LuaMakeEnemy);
  dev::AddLuaFunction("PSTF_WorldTicks", &LuaWorldTicks);
  dev::AddLuaFunction("PSTF_SetName", &LuaSetName);
  dev::AddLuaFunction("PSTF_EvalAs", &LuaEvalAs);
  dev::AddLuaFunction("PSTF_FightId", &LuaFightId);
  dev::AddLuaFunction("PSTF_SetHp", &LuaSetHp);
  dev::AddLuaFunction("PSTF_DiceAt", &LuaDiceAt);
  dev::AddLuaFunction("PSTF_HoverAt", &LuaHoverAt);
  dev::AddLuaFunction("PSTF_RollProbe", &LuaRollProbe);
  dev::AddLuaFunction("PSTF_CenterOn", &LuaCenterOn);
  dev::AddLuaFunction("PSTF_IconSize", &LuaIconSize);
  dev::AddLuaFunction("PSTF_Status", &LuaStatus);
  dev::AddLuaFunction("PSTF_TbcAttacks", &LuaTbcAttacks);
  dev::AddLuaFunction("PSTF_SearchByte", &LuaSearchByte);
  dev::AddLuaFunction("PSTF_PreviewPts", &LuaPreviewPts);
  dev::AddLuaFunction("PSTF_EnginePath", &LuaEnginePath);
  Log("tbc: installed (ProcessAI slot, 2 walk calls, tick); radii %d/%d/%d, end turn key %X%s, "
      "move %.0f ft, hud %s, camera %s",
      g.radii.start, g.radii.join, g.radii.leave, g.end_turn_vk, g.auto_party ? ", auto party" : "",
      g.move_feet, g.hud ? "on" : "off",
      !g.camera ? "off" : (!g.cam_pre ? "tick handler" : (g.cam_smooth ? "eased in draw pass" : "tick rate")));
  return true;
}

}  // namespace pstf::tbc
