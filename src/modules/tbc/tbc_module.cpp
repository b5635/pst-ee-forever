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
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/patch.h"
#include "core/shot.h"
#include "core/tick.h"
#include "engine/combat.h"
#include "engine/dialog.h"
#include "engine/frame.h"
#include "engine/gl_world.h"
#include "engine/lua.h"
#include "engine/menu.h"
#include "engine/party.h"
#include "engine/render.h"
#include "engine/soft_draw.h"
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
#include "modules/tbc/ground_fx.h"
#include "modules/tbc/maneuvers.h"
#include "modules/tbc/status.h"
#include "core/gameres.h"
#include "modules/tbc/round_rules.h"
#include "modules/tbc/save_banner.h"
#include "modules/tbc/combat_text.h"
#include "modules/tbc/path_line.h"
#include "modules/tbc/path_preview.h"
#include "modules/tbc/turn_clock.h"
#include "modules/tbc/ui_layout.h"

namespace pstf::tbc {
namespace {

using SpriteFn = void(__fastcall*)(void*);
SpriteFn g_process_ai = nullptr;
using PendingFn = uint64_t(__fastcall*)(void*, int);
PendingFn g_pending = nullptr;
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
  bool cast_feedback = false;          // FeedBack hook on: casts counted (engine::SpellCasts)
  int turn_casts = 0;                  // the current creature's cast count seen so far this turn
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
  bool save_banner = false;          // [tbc] save_banner: icon + "Save Success: N (Death)" over every save
  bool save_silenced = false;        // the engine's own "Save vs." line is patched out
  struct Banner {
    int32_t id;
    bool good;
    std::string text;
    uint32_t t0;          // GetTickCount when it appeared
    bool sounded = false; // the verdict sound played
    int ax = 0, ay = 0, slot = 0;  // where it sits: its place in the creature's combat text stack
  };
  std::vector<Banner> banners;       // oldest first, at most kSaveBanners
  int banners_good = 0, banners_bad = 0;  // made since the start (test probe)
  bool ctext = false;                // [tbc] combat_text: BG3 combat text (combat_text.h) instead of rising text
  CombatTextBook ct;
  std::map<int32_t, uint64_t> ct_area;  // the area each creature with lines was in (lines end when it leaves)
  std::map<int32_t, std::map<std::string, int32_t>> ct_status;  // creature -> its statuses (parent -> name strref)
  std::map<int32_t, uint32_t> ct_no_miss;  // a takedown said "Trip: Missed" for this target: no "Miss!" too
  int ct_floaters = 0, ct_swallowed = 0, ct_added = 0;  // test probe counts
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
  std::map<int32_t, int> persist_seen;  // persistent effects per combatant (a new one pulses at once)
  // Turn (its start tick turn_w0 + 1) in which each combatant last got a first pulse: at most one per creature per
  // turn. A pulse forces the effect list, which can drop and re-add a persistent effect (an op 25 poison onset):
  // the count went 0 -> 1 -> 0 -> 1 every frame and pulsed thousands of times (hang after SPWI217).
  std::map<int32_t, uint32_t> persist_pulsed;
  uint32_t turn_w0 = 0;
  uint16_t clog_action = 0xFFFF;
  uint16_t prev_action = 0xFFFF;     // the action before clog_action (a Spell that just ended)
  bool follow = false;               // the camera keeps the active creature centred while it moves
  std::wstring sound_dir;            // <game>/pst_forever/sounds/
  uint32_t resolve_ticks = 0;        // world ticks the turn has waited for its spell to land
  uint32_t since_action = 1000;      // world ticks since its action stopped running
  SpellWait resolving_why = SpellWait::None;  // what the turn's end last waited for (logged on a change)
  bool resolving_capped = false;     // the spell hold cap fired this turn (logged once)
  PulseBudget pulses;                // lingering spells: one round of pulse ticks per caster round
  std::string turn_cast_res;          // the spell the current creature cast this turn (orphan effects' parent)
  uint32_t turn_cast_flags = 0;
  uint32_t since_flying = 1000;       // ticks since the current creature's last projectile was in the air
  LoseTurnCheck lose_check;          // the "turn lost" check, a few ticks into the turn
  ProjectilePace proj_pace;          // which projectiles move (theirs in their caster's turn; born free fly on)
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
  OrphanMap orphans;                 // parentless spell effects -> the spell they came with (status.h)
  std::vector<std::vector<StatusIconView>> status_party;
  uint32_t status_t0 = 0;            // GetTickCount of the last party status collection
  // Maneuvers (maneuvers.h): Run, Withdraw, Block, Trip/Knockdown, attacks of opportunity.
  bool maneuvers = true;             // [tbc] maneuvers
  bool opportunity = true;           // [tbc] opportunity_attacks
  bool no_push = true;               // [tbc] no_push: walkers never push combatants aside in a fight
  bool keep_queue = true;            // [tbc] keep_queue: an AI's script sequence goes on at its next turn
  bool script_timers = true;         // [tbc] script_timers: script timers run one round per round
  std::map<int32_t, TimerClock> timer_clock;
  int stalemate_rounds = 3;          // [tbc] stalemate_rounds: quiet whole rounds that end the fight (0 = never)
  Stalemate stalemate;
  StaleHold stale_hold;               // the stalemate's creatures: no new fight until one of them changed
  bool stale_logged = false;
  std::map<int32_t, int> timer_owed;  // whole seconds owed to its script timers at its next turn start
  int refunds = 0;                   // spent Spells handed back this turn
  int lead_skips = 0;                // Waits skipped before the current creature's action this turn
  bool freeze_ai = false;            // the current AI's turn is over (spell still landing): no ProcessAI
  bool keep_current = false;         // its turn ended at a second action: that action and the queue wait for its next turn
  engine::QueueStash aoo_stash;      // the reactor's script queue while it swings its attack of opportunity
  bool cast_live = false;            // the current creature's spell is being cast (CurrentSpell set)
  bool cast_done = false;            // an AI's spell went off this turn: one cast per turn
  int aoo_trace = 0;                 // env PSTF_AOO_TRACE=N: log the first N reach checks of each turn
  int aoo_trace_react = 0;           // ... and the ticks of a reaction
  std::map<int32_t, Stance> stance;  // every combatant, from its turn start to the next
  Reaction aoo;                      // the attack of opportunity in progress
  int mop_limit = 3;                 // [tbc] mop_shots: Missile of Patience shots in a turn-based fight (0 = engine's)
  bool mop_hook = false;
  std::map<uint64_t, int> mop_shots;  // hovering missile -> shots fired
  bool mop_end = false;              // the missile fired its last shot: end it (cursor off targeting)
  int ai_path_wait = 0;              // ticks the directed AI waited for its attack's walk path
  std::vector<int32_t> aoo_queue;    // reactors still to swing before the mover walks (one after the other)
  bool aoo_walk_checked = false;     // this walk (its destination) was checked as it started
  std::pair<int, int> aoo_walk_dest{-1, -1};
  std::map<int32_t, bool> aoo_was_in;  // this turn: the mover stood in this foe's reach last tick
  bool aoo_freeze = false;           // the mover (no walk path) pauses its sprite update for the swing
  bool aoo_fudge = false;            // the reactor keeps the engine's RANGE_FUDGE reach for this reaction
  bool aoo_swing = false;            // the reactor swung (TakeRolls)
  bool takedown_swung = false;       // the armed takedown's first swing came
  bool ai_moved = false;             // the directed AI chose its maneuver this turn
  struct Trip {
    int32_t target, attacker;
    Takedown kind;
    int wait;  // ticks until the outcome is read (the save comes as the effect applies)
    int before = 0;  // its running AC effects (opcode 0) before ours
  };
  std::vector<Trip> trips, trip_checks;
  TripPace trip_pace;                // the armed takedown swing: slow wind-up, quick follow-through
  std::map<int32_t, PronePose> poses;  // knocked down: lying / getting up (die cycle, backwards to rise)
  std::vector<std::string> sounds;   // queued from the swing hook (no Lua inside Swing)
  // Maneuver visuals in the world overlay (GetTickCount ms): dust and smoke puffs (ground_fx.h), block glow, sparks.
  enum class FxKind : uint8_t { RunBurst, RunTrail, Smoke, SmokeTrail, KnockDust, Shield, Sparks };
  bool ground_fx = false;  // ground effects drawn at the floor point (soft_draw), else over the sprites
  struct Fx {
    FxKind kind;
    int32_t id;  // follows this creature (-1: stays at x,y)
    int x, y;
    uint32_t t0;
    uint32_t seed;
  };
  std::vector<Fx> fx;
  uint32_t dust_t = 0;               // last dust/smoke puff under a runner or withdrawer
  uint32_t rng = 0x2545F491u;
  // Counters for tests (PSTF_MoveStats): since install.
  struct MoveStats {
    int runs = 0, withdraws = 0, blocks = 0, block_tries = 0, blocked = 0, takedowns = 0, takedown_hits = 0, prone = 0;
    int aoo = 0, aoo_swung = 0, ai_moves = 0, stood = 0, short_runs = 0;
  } ms;
} g;

void FloatText(int32_t id, const std::string& text, uint32_t ms = 1800);
std::vector<StatusIconView> StatusIconsOf(int32_t id, int box, size_t max_icons);

// The creature's spell is still on its way (a projectile object it cast is in the area): its turn
// waits for flying ones (Adder's Kiss circles 60 ticks before it flies) and for lingering ones that
// still have pulse ticks this round (the round's pulse lands in the caster's turn).
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

bool Held(int32_t id) { return g.running && id != g.current && id != g.aoo.reactor && Contains(g.in, id); }

void ManeuverTurnStart(int32_t id);
bool WalkStartProvokes(int32_t cur);
void MopFinish(const char* why);
void HoldStance(int32_t id);
void ManeuverFightEnd();

// Projectiles move in their caster's turn; a landed cloud or swarm also stops once it has used this
// round's pulse ticks; one already flying when the fight began flies on (round_rules.h ProjectilePace).
// Casters outside the fight are not paced.
bool ProjectileRuns(int32_t caster, int32_t proj, bool lingers) {
  if (!g.running || !Contains(g.in, caster)) return true;
  return g.proj_pace.Runs(caster, proj, g.current, lingers, &g.pulses);
}

bool Rising(int32_t id) {
  const auto it = g.poses.find(id);
  return it != g.poses.end() && it->second.phase == PronePose::Phase::Rising;
}

void __fastcall HookProcessAI(void* sprite) {
  const int32_t sid = engine::SpriteIdOf(sprite);
  // freeze_ai: an AI turn that is over but waits for its spell to land runs no more script (its queue is its next
  // turn's: Spell; Wait; IncrementGlobal must not do the next step in the same turn).
  if (Held(sid) || (g.running && Rising(sid)) || (g.running && g.freeze_ai && sid == g.current)) {
    // No scripts or actions while it waits, but spells cast on it land now (round clock frozen).
    if (g.round_effects) engine::HeldEffectsTick(sprite);
    return;
  }
  g_process_ai(sprite);
}
// CGameSprite::ProcessPendingTriggers: the other way a creature's script and actions run (a trigger such as
// AttackedBy arrives; ResolveInstants runs instant actions and the next queued action starts). Held the same as
// ProcessAI ([tbc] keep_queue): the triggers stay pending until its own turn. The dead still run theirs (Die()).
uint64_t __fastcall HookPendingTriggers(void* sprite, int a2) {
  if (g.running && g.keep_queue) {
    const int32_t sid = engine::SpriteIdOf(sprite);
    if ((Held(sid) || (g.freeze_ai && sid == g.current)) && !(engine::SpriteState(sid) & sym::kStateDead)) return 0;
  }
  return g_pending(sprite, a2);
}
void __fastcall HookWalk(void* sprite) {
  const int32_t id = engine::SpriteIdOf(sprite);
  if (Held(id)) return;
  if (g.running && id == g.current && (g.aoo.Active() || !g.aoo_queue.empty())) {  // attacks of opportunity first
    HoldStance(id);
    return;
  }
  if (g.running && id == g.current && (g.hold_current || Rising(id))) return;  // spent / getting up
  if (g.running && id == g.aoo.reactor) return;  // its attack of opportunity is a swing, never a chase
  if (g.running && id == g.current && WalkStartProvokes(id)) {  // its walk leaves reaches: the reactions first (OnTick)
    HoldStance(id);
    return;
  }
  if (g.running && id == g.current && g.stance[id].withdrawn) {
    engine::ForceRun(id, false, g.run_scale_other);  // Withdraw: walk animation and walking speed
  } else if (g.running && g.run && id == g.current) {
    engine::ForceRun(id, true, g.run_scale_other);
  }
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

void StopKeepingScript(int32_t id, const char* why);
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
  g.timer_clock.clear();
  g.timer_owed.clear();
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
  ManeuverFightEnd();  // prone combatants stand up (before g.running goes: their effects are ours)
  g.running = false;
  engine::ForgetOnsets();
  g.pulses.Clear();
  g.proj_pace.Clear();
  g.in.clear();
  g.hp_seen.clear();
  g.persist_seen.clear();
  g.persist_pulsed.clear();
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
  const bool party_side = s.party || IsFriendlyEa(s.ea) || s.ally;
  return {s.id, party_side ? Side::Party : Side::Hostile, 0, 2, 0};
}

void StartFight(const std::vector<Seen>& join, uint32_t now) {
  std::vector<Recruit> who;
  for (const auto& s : join) who.push_back(ToRecruit(s));
  g.dir = std::make_unique<Director>(GetTickCount() | 1u, g.round_ticks);
  g.stalemate = Stalemate{};
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
  for (const auto& s : join)
    if (!s.party && !IsHostileEa(s.ea) && !IsFriendlyEa(s.ea))
      Log("tbc: %s joins as a neutral fighting %s: %s side", Who(s.id).c_str(), Who(s.target).c_str(),
          s.ally ? "party" : "hostile");
  RegisterNames(g.dir->Order());
  g.floats.clear();
  for (int32_t id : g.dir->Order())
    if (const Combatant* c = g.dir->Find(id))
      FloatText(id, "Initiative: " + std::to_string(c->initiative), 3500);
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

std::vector<StaleHold::At> StaleAt(const std::vector<Seen>& who) {
  std::vector<StaleHold::At> v;
  for (const auto& s : who) {
    engine::SpriteInfo o;
    if (engine::GetSprite(s.id, &o)) v.push_back({s.id, o.x, o.y, o.hp});
  }
  return v;
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
    seen.push_back({s.id, Contains(g.party, s.id), s.ea, s.alive, s.x, s.y, s.target,
                    IsAttackAction(s.action) || IsCastAction(s.action)});
  }
  std::vector<int32_t> hostile_side;
  for (int32_t id : g.in)
    if (const Combatant* hc = g.dir ? g.dir->Find(id) : nullptr)
      if (hc->r.side == Side::Hostile) hostile_side.push_back(id);
  const CensusOut c = Census(seen, g.in, g.running, g.radii, hostile_side);
  if (!g.running) {
    if (c.start && !g.no_fight && !g.stale_hold.at.empty()) {
      if (g.stale_hold.Holds(StaleAt(c.join))) {
        if (!g.stale_logged) Log("tbc: no new fight: the stalemate's creatures stand as they stood");
        g.stale_logged = true;
        return;
      }
      Log("tbc: stalemate hold over (a creature moved, was hurt or arrived)");
      g.stale_hold.at.clear();
    }
    if (c.start && !g.no_fight) StartFight(c.join, now);
    return;
  }
  // A combatant whose EA crossed a cutoff (a neutral ally turned hostile by its script, a charmed foe) changes sides.
  for (const auto& s : seen) {
    const Combatant* sc = !s.party && s.alive && Contains(g.in, s.id) ? g.dir->Find(s.id) : nullptr;
    const int want = SideOfEa(s.ea);
    if (!sc || !want) continue;
    const Side side = want == 1 ? Side::Party : Side::Hostile;
    if (sc->r.side == side) continue;
    Log("tbc: %s changes sides (EA %u): now %s side", Who(s.id).c_str(), s.ea, want == 1 ? "party" : "hostile");
    CLog("SIDE", Name(s.id) + " now fights on the " + (want == 1 ? "party" : "hostile") + " side (EA " + std::to_string(s.ea) + ")");
    g.dir->SetSide(s.id, side);
    if (!g.dir->Running()) break;
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
    Log("tbc: %s joins (round %u)%s", Who(s.id).c_str(), g.dir->Round(),
        IsHostileEa(s.ea) || IsFriendlyEa(s.ea) || s.party ? ""
        : s.ally ? ", a neutral fighting the party's foes: party side" : ", a neutral fighting the party's side: hostile side");
    RegisterNames({s.id});
    const Combatant* jc = g.dir->Find(s.id);
    const int ini = jc ? jc->initiative : 0;
    FloatText(s.id, "Initiative: " + std::to_string(ini), 3500);
    CLog("JOIN", Name(s.id) + " hp " + HpText(s.id) + ", initiative " + std::to_string(ini));
  }
  if (!g.dir->Running()) EndFight("one side is gone");
}

void OnTurnChange(int32_t prev, int32_t next) {
  if (prev != -1 && Contains(g.in, prev)) {
    // Script audit probe: what the clean stop throws away (a real-time sequence cut short, e.g. a step counter
    // queued after a Wait) and its script timers (they only count down inside its own turns).
    const std::vector<uint16_t> q = engine::ActionQueue(prev);
    if (q.size() > 1 || (!q.empty() && q[0] != 0)) {
      int lost = 0;
      const std::string t = QueueText(q.data(), static_cast<int>(q.size()), &lost);
      const Combatant* pc0 = g.dir->Find(prev);
      const bool party0 = Contains(g.party, prev) || (pc0 && pc0->r.side == Side::Party);
      if (!g.keep_current && !(g.keep_queue && !party0 && KeepCurrentAtTurnEnd(q[0])) &&
          !(g.keep_queue && !party0 && IsCastAction(q[0]) && engine::CurrentSpell(prev).empty() &&
            engine::CurrentActionDone(prev) == 0) &&
          !(g.keep_queue && KeepQueueAtTurnEnd(party0, prev == g.nudged, q.data(), static_cast<int>(q.size())))) {
        Log("tbc: %s: turn end drops queue [%s]%s", Who(prev).c_str(), t.c_str(), lost ? " (script state lost)" : "");
        CLog("QUEUE", Name(prev) + " turn end drops [" + t + "]" + (lost ? " (script state lost)" : ""));
      }
    }
    if (g.script_timers) {
      const int s = g.timer_clock[prev].Owe(g.effect_round_ticks, g.econ.total);
      g.timer_owed[prev] += s;
    }
    const auto timers = engine::ScriptTimers(prev);
    if (!timers.empty()) {
      std::string t;
      for (const auto& tm : timers) t += (t.empty() ? "" : " ") + std::to_string(tm.first) + ":" + std::to_string(tm.second) + "s";
      CLog("TIMER", Name(prev) + " script timers " + t);
    }
    // Clean stop: no action or path carried out of its turn. An AI creature's queued script actions stay for its
    // next turn (economy.h KeepQueueAtTurnEnd): held, it runs none of them in between.
    const Combatant* pc = g.dir->Find(prev);
    const bool party = Contains(g.party, prev) || (pc && pc->r.side == Side::Party);
    // its script's bookkeeping (IncrementGlobal) became the running action as the turn ended: it has not run yet
    // (after a nudge too: our order is an Attack, a pending IncrementGlobal is its script's)
    if (g.keep_queue && !party && !q.empty() && KeepCurrentAtTurnEnd(q[0])) g.keep_current = true;
    // its next spell is the running action but not cast yet (the turn ended when the first went off)
    if (g.keep_queue && !party && !q.empty() && IsCastAction(q[0]) && engine::CurrentSpell(prev).empty() &&
        engine::CurrentActionDone(prev) == 0)
      g.keep_current = true;
    if (g.keep_current) {
      const std::string t = QueueText(q.data(), static_cast<int>(q.size()));
      Log("tbc: %s: turn end keeps its action and queue [%s] for its next turn", Who(prev).c_str(), t.c_str());
      CLog("QUEUE", Name(prev) + " keeps [" + t + "] (action and queue) for its next turn");
    } else if (g.keep_queue && KeepQueueAtTurnEnd(party, prev == g.nudged, q.data(), static_cast<int>(q.size())) &&
        engine::ClearAIKeepQueue(prev)) {
      const std::string t = QueueText(q.data() + 1, static_cast<int>(q.size()) - 1);
      Log("tbc: %s: turn end keeps queue [%s] for its next turn", Who(prev).c_str(), t.c_str());
      CLog("QUEUE", Name(prev) + " keeps [" + t + "] for its next turn");
    } else {
      engine::ClearAI(prev);
    }
    engine::DropPath(prev);
  }
  MopFinish("turn over");  // a hovering Missile of Patience belongs to its caster's turn
  if (prev != -1 && engine::MultiFireLeft(prev) > 0) {  // so are the missiles a multi-fire spell left to aim
    Log("tbc: %s: %d missiles not aimed: dropped (turn over)", Who(prev).c_str(), engine::MultiFireLeft(prev));
    engine::MultiFireDrop(prev);
  }
  g.mop_shots.clear();
  g.mop_end = false;
  g.nudged = -1;
  g.lead_skips = 0;
  g.refunds = 0;
  g.freeze_ai = false;
  g.keep_current = false;
  g.cast_live = false;
  g.cast_done = false;
  g.steady_ai = false;
  g.apr_halves = 0;
  g.current = next;
  g.end_requested = false;
  g.resolve_ticks = 0;
  g.resolving_why = SpellWait::None;
  g.resolving_capped = false;
  g.since_action = 1000;
  g.turn_w0 = g.last_time;
  g.clog_action = 0xFFFF;
  g.prev_action = 0xFFFF;
  g.follow = false;
  if (next == -1) return;
  if (g.round_effects) {
    for (int32_t p : LingeringOf(next)) {
      g.pulses.NewRound(p);
      CLog("SPELL", Name(next) + ": lingering spell " + std::to_string(p) + " gets this round's pulse");
    }
  }
  if (g.script_timers && g.timer_owed[next] > 0) {
    // its script timers run the round time its turns did not (round_rules.h TimerClock)
    const int s = g.timer_owed[next];
    g.timer_owed[next] = 0;
    const auto t0 = engine::ScriptTimers(next);
    if (!t0.empty() && engine::StepScriptTimers(next, s)) {
      std::string a, b;
      for (const auto& t : t0) a += (a.empty() ? "" : " ") + std::to_string(t.first) + ":" + std::to_string(t.second) + "s";
      for (const auto& t : engine::ScriptTimers(next)) b += (b.empty() ? "" : " ") + std::to_string(t.first) + ":" + std::to_string(t.second) + "s";
      CLog("TIMER", Name(next) + " turn start: script timers run " + std::to_string(s) + " s: " + a + " -> " + (b.empty() ? "none" : b));
    }
  }
  if (g.round_effects && engine::EffectClockInstalled()) {
    engine::SpriteInfo before;
    const bool had = engine::GetSprite(next, &before);
    engine::StepEffectClock(next, g.effect_round_ticks);
    // The casting aura counts the caster's own AI ticks, which run on its turns only: step it by the same round
    // (round_rules.h StepCastAura), or a caster could cast only every few turns.
    int16_t aura = -1;
    bool casting = false;
    if (engine::CastState(next, nullptr, &aura, &casting) && aura >= 0) {
      const int16_t after = StepCastAura(aura, g.effect_round_ticks, engine::RoundLengthTicks(), casting);
      engine::SetCastAura(next, after);
      Log("tbc: %s turn start: casting aura %d -> %d%s", Who(next).c_str(), aura, after, casting ? " (still casting)" : "");
    }
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
  g.turn_casts = g.cast_feedback ? engine::SpellCasts(next) : 0;
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
  ManeuverTurnStart(next);  // stances of its own turn go; a prone creature stands up (its action)
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
  g.turn_cast_res.clear();
  g.since_flying = 1000;
  g.lose_check.Start();  // after the engine has applied this step (round_rules.h LoseTurnCheck)
}

// A creature that cannot act (asleep, stunned, helpless, frozen, stone) loses the turn, read once the engine has
// re-evaluated its state after the turn-start step.
void CheckLostTurn(uint32_t dt) {
  if (g.current == -1 || !g.lose_check.Due(dt)) return;
  const int64_t state = engine::SpriteState(g.current);
  if (!LosesTurn(state)) return;
  g.end_requested = true;
  Say(Who(g.current) + " cannot act: turn lost");
  CLog("TURN", Name(g.current) + " cannot act (state " + std::to_string(static_cast<long long>(state)) + "): turn lost");
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

// --- maneuvers: Run, Withdraw, Block, Trip/Knockdown, attacks of opportunity (maneuvers.h) -------------
uint32_t Rand() {
  g.rng ^= g.rng << 13, g.rng ^= g.rng >> 17, g.rng ^= g.rng << 5;
  return g.rng;
}
int D20() { return static_cast<int>(Rand() % 20) + 1; }
void QueueSound(const char* res) { g.sounds.push_back(res); }
void FlushSounds() {
  if (g.sounds.empty()) return;
  std::string code;
  for (const auto& s : g.sounds) code += "Infinity_PlaySound('" + s + "') ";
  g.sounds.clear();
  const std::string err = engine::LuaDoString(code.c_str());
  if (!err.empty()) Log("tbc: maneuver sound failed: %s", err.c_str());
}
// A combat text line over a creature (combat_text.h): at its head as drawn now, then fixed there.
const CtLine* CtAdd(int32_t id, CtKind kind, CtSeg seg) {
  engine::SpriteInfo o;
  if (!engine::GetSprite(id, &o)) return nullptr;
  int wx = o.x, wy = o.y;
  engine::InterpDrawPos(id, &wx, &wy);
  const int hgt = engine::SpriteTextHeight(id);
  const uint32_t now = GetTickCount();
  for (const CtLine& l : g.ct.Lines())  // a creature that left the area starts a new stack where it is now
    if (l.id == id && g.ct_area[id] != o.area) {
      g.ct.Clear();
      break;
    }
  g.ct_area[id] = o.area;
  ++g.ct_added;
  return &g.ct.Add(id, wx, wy - (hgt > 0 ? hgt : 90) - kCtAboveHeadPx, kind, std::move(seg), now);
}
void FloatText(int32_t id, const std::string& text, uint32_t ms) {
  if (g.ctext) {
    CtAdd(id, CtKind::Text, {text, kCtWhite});
    if (text.compare(0, 11, "Initiative:") != 0) Log("tbc: text over %s: %s", Who(id).c_str(), text.c_str());
    return;
  }
  g.floats.push_back({id, text, GetTickCount() + ms});
}
void AddFx(State::FxKind kind, int32_t id, int x = 0, int y = 0) {
  engine::SpriteInfo o;
  if (id != -1 && engine::GetSprite(id, &o)) x = o.x, y = o.y;
  g.fx.push_back({kind, id, x, y, GetTickCount(), Rand()});
  if (g.fx.size() > 96) g.fx.erase(g.fx.begin());
}
// An effect left on the ground where the creature is now drawn (it does not follow it).
void AddFxAtFeet(State::FxKind kind, int32_t id, int dx = 0, int dy = 0) {
  engine::SpriteInfo o;
  if (!engine::GetSprite(id, &o)) return;
  int x = o.x, y = o.y;
  engine::InterpDrawPos(id, &x, &y);
  AddFx(kind, -1, x + dx, y + dy);
}
bool Foes(int32_t a, int32_t b) {
  const Combatant* ca = g.dir ? g.dir->Find(a) : nullptr;
  const Combatant* cb = g.dir ? g.dir->Find(b) : nullptr;
  return ca && cb && ca->r.side != cb->r.side;
}
int PersonalSpace(int32_t id) {
  const engine::CellCoster c = engine::CosterFor(id);
  return c.ok() && c.personal_space ? c.personal_space : 3;
}
bool ClassOf(int32_t id, uint8_t* cls, uint8_t* spec) {
  *cls = *spec = 0;
  return engine::SpriteClass(id, cls, spec);
}
bool MayTripId(int32_t id) {
  uint8_t c, s;
  return ClassOf(id, &c, &s) && MayTrip(c, s);
}
bool MayBlockId(int32_t id) {
  uint8_t c, s;
  return ClassOf(id, &c, &s) && MayBlock(c, s);
}
bool MayOpportunityId(int32_t id) {
  uint8_t c, s;
  return !ClassOf(id, &c, &s) || MayOpportunity(s);
}
bool MayKnockdownId(int32_t id) {
  const Combatant* c = g.dir ? g.dir->Find(id) : nullptr;
  engine::CombatStats st;
  return c && c->r.side == Side::Hostile && engine::GetCombatStats(id, &st) && MayKnockdown(false, st.thac0);
}
int AttacksForBlock() { return g.econ.want_swings > 0 ? g.econ.SwingsLeft() : 1; }

// engine::OnsetSyncFn: delayed effects that arrived together (round_rules.h SyncSpellOnsets).
void OnsetSync(const int32_t* left, const int* opcode, const uint64_t* source, int* kinds, int n) {
  std::vector<OnsetSeen> v(n);
  for (int i = 0; i < n; ++i) v[i] = {left[i], opcode[i], source[i], kinds[i]};
  SyncSpellOnsets(v.data(), n);
  for (int i = 0; i < n; ++i) kinds[i] = v[i].kind;
}

// The current creature takes a maneuver (button, Lua or the directed AI). False = not allowed now.
bool UseManeuver(int32_t id, AiMove m) {
  if (!g.running || !g.maneuvers || id != g.current || g.aoo.Active()) return false;
  Stance& st = g.stance[id];
  bool ok = false;
  const char* what = "";
  switch (m) {
    case AiMove::Run:
      ok = ApplyRun(&g.econ, &st), what = "runs";
      if (ok) {
        engine::ForceRun(id, true, g.run_scale_other);
        QueueSound("PFSNDRUN");
        AddFxAtFeet(State::FxKind::RunBurst, id);
        FloatText(id, "Run!");
      }
      break;
    case AiMove::Withdraw: {
      const Combatant* me = g.dir ? g.dir->Find(id) : nullptr;
      if (!me || me->r.side != Side::Party) break;  // enemies never withdraw
      ok = ApplyWithdraw(&g.econ, &st), what = "withdraws";
      if (ok) {
        engine::ForceRun(id, false, g.run_scale_other);
        QueueSound("PFSNDWDR");
        AddFxAtFeet(State::FxKind::Smoke, id);
        FloatText(id, "Withdraw!");
      }
      break;
    }
    case AiMove::Block:
      ok = ApplyBlock(&g.econ, &st, MayBlockId(id), AttacksForBlock()), what = "prepares to block";
      if (ok) {
        engine::ClearAI(id);  // its swings are the blocks now
        engine::DropPath(id);
        QueueSound("PFSNDBLK");
        AddFx(State::FxKind::Shield, id);
        FloatText(id, "Block!");
      }
      break;
    case AiMove::Trip:
      ok = ToggleTrip(g.econ, &st, MayTripId(id));
      what = st.takedown == Takedown::Trip ? "will trip with its first attack" : "cancels the trip";
      break;
    case AiMove::Knockdown:
      ok = ArmKnockdown(g.econ, &st, MayKnockdownId(id)), what = "will knock down with its first attack";
      break;
    case AiMove::None:
      break;
  }
  if (!ok) return false;
  if (m == AiMove::Run) ++g.ms.runs;
  if (m == AiMove::Withdraw) ++g.ms.withdraws;
  if (m == AiMove::Block) ++g.ms.blocks;
  char b[96];
  std::snprintf(b, sizeof b, " %s (%.1f/%.1f ft, %d block stacks)", what, g.econ.move_ft, g.econ.max_ft, st.block_stacks);
  CLog("MOVE", Name(id) + b);
  Log("tbc: %s%s", Who(id).c_str(), b);
  g.bar_t0 = 0;
  return true;
}

// Swing.hit1 (melee) filters, inside the engine's Swing: no Lua here, only our state.
int __cdecl MeleeThac0(int32_t attacker, int32_t /*target*/) {
  if (!g.running || attacker != g.current || g.takedown_swung) return 0;
  const auto it = g.stance.find(attacker);
  return it == g.stance.end() ? 0 : TakedownThac0(it->second.takedown);
}
uint8_t __cdecl MeleeOutcome(int32_t attacker, int32_t target, bool hit, int roll) {
  if (!g.running || !Contains(g.in, target)) return 0;
  if (attacker == g.current && !g.takedown_swung) {
    Stance& a = g.stance[attacker];
    if (a.takedown != Takedown::None) {
      const Takedown k = a.takedown;
      a.takedown = Takedown::None;
      g.takedown_swung = true;
      ++g.ms.takedowns;
      g.trip_pace.Resolved();  // the rest of the swing plays quicker
      if (!hit) {
        FloatText(target, TakedownText(TakedownName(k), TakedownResult::Missed));
        g.ct_no_miss[target] = GetTickCount();
        CLog("MOVE", Name(attacker) + " " + TakedownName(k) + " misses " + Name(target));
        return 0;
      }
      ++g.ms.takedown_hits;
      g.trips.push_back({target, attacker, k, 0, 0});
      return 2;  // a hit with no damage: the target saves or falls
    }
  }
  if (!hit) return 0;
  auto it = g.stance.find(target);
  if (it == g.stance.end() || it->second.block_stacks <= 0) return 0;
  --it->second.block_stacks;
  ++g.ms.block_tries;
  engine::CombatStats d;
  const int thac0 = engine::GetCombatStats(target, &d) ? d.thac0 : 20;
  const int block = D20();
  const bool stopped = BlockStops(block, thac0, roll);
  char b[128];
  std::snprintf(b, sizeof b, " blocks: d20 %d + AC %d vs THAC0 %d, attack d20 %d -> %s (%d stacks left)", block, kBlockAc,
                thac0, roll, stopped ? "BLOCKED" : "not blocked", it->second.block_stacks);
  CLog("MOVE", Name(target) + b);
  Log("tbc: %s%s", Who(target).c_str(), b);
  if (!stopped) return 0;
  ++g.ms.blocked;
  QueueSound("PFSNDPRY");
  AddFx(State::FxKind::Sparks, target);
  FloatText(target, "Blocked!");
  return 1;
}

// Knocked down: a save vs death against opcode 0 (AC -4 for kProneSeconds of its own round clock: +4 to hit
// it), from the attacker, the engine's own roll. A hit, saved or not, plays the armour hit sound and the
// target's pain (its damage sequence, with its own sound); a failed save knocks it down (die cycle, held).
int RunningAcEffects(int32_t id) {
  int n = 0;
  for (const auto& e : engine::TimedEffects(id)) n += e.opcode == 0 && e.type == 4096 && e.left > 0;
  return n;
}
void ApplyTrips() {
  for (auto tr : g.trips) {
    engine::PlayArmorHitSound(tr.target, sym::kHitSoundCrushing);  // a trip is a blunt blow
    engine::SetSpriteSequence(tr.target, sym::kSeqDamage);
    engine::EffectSpec e;
    e.opcode = 0;  // Stat: AC vs. Damage Type Modifier
    e.param1 = static_cast<uint32_t>(-kProneAcPenalty);
    e.param2 = 0;  // all weapons
    e.timing = 0;  // duration in seconds
    e.duration = kProneSeconds;
    e.save_type = 4;  // save vs death
    tr.before = RunningAcEffects(tr.target);
    const bool ok = engine::ApplyEffect(tr.target, tr.attacker, e);
    CLog("MOVE", Name(tr.attacker) + " " + TakedownName(tr.kind) + " hits " + Name(tr.target) +
                     (ok ? ": save vs death or knocked down" : ": effect could not be applied"));
    tr.wait = 2;
    if (ok) g.trip_checks.push_back(tr);
  }
  g.trips.clear();
  for (auto it = g.trip_checks.begin(); it != g.trip_checks.end();) {
    if (--it->wait > 0) {
      ++it;
      continue;
    }
    const bool down = RunningAcEffects(it->target) > it->before;
    g.stance[it->target].prone = down;
    if (down) {
      ++g.ms.prone;
      g.poses[it->target].Fall();
      AddFxAtFeet(State::FxKind::KnockDust, it->target);  // dust thrown up when the body lands
      StopKeepingScript(it->target, "knocked down");
      engine::DropPath(it->target);
    }
    FloatText(it->target, TakedownText(TakedownName(it->kind), down ? TakedownResult::Success : TakedownResult::Failed));
    CLog("MOVE", Name(it->target) + (down ? " is knocked down until its next turn (+4 to hit it)" : " keeps its footing (saved)"));
    Log("tbc: %s: %s %s", Who(it->target).c_str(), TakedownName(it->kind), down ? "success, knocked down" : "failure (saved)");
    it = g.trip_checks.erase(it);
  }
}
// Gets up: the die cycle backwards (its AC penalty ends with its round clock at this turn start).
// Stops a creature outside its own turn (knocked down) the way a turn end does: an AI's script sequence that
// has not run yet (its kept queue, a pending IncrementGlobal) stays for its next turn.
void StopKeepingScript(int32_t id, const char* why) {
  const std::vector<uint16_t> q = engine::ActionQueue(id);
  const Combatant* c = g.dir ? g.dir->Find(id) : nullptr;
  const bool party = Contains(g.party, id) || (c && c->r.side == Side::Party);
  const int n = static_cast<int>(q.size());
  if (g.keep_queue && !party && n && KeepCurrentAtTurnEnd(q[0])) {
    CLog("QUEUE", Name(id) + " " + why + ": keeps [" + QueueText(q.data(), n) + "]");
  } else if (g.keep_queue && KeepQueueAtTurnEnd(party, false, q.data(), n) && engine::ClearAIKeepQueue(id)) {
    CLog("QUEUE", Name(id) + " " + why + ": keeps [" + QueueText(q.data() + 1, n - 1) + "]");
  } else {
    engine::ClearAI(id);
  }
}

void StandUp(int32_t id) {
  g.stance[id].prone = false;
  auto it = g.poses.find(id);
  if (it != g.poses.end()) it->second.Rise();
}

// Every sprite's per-tick update (CGameSprite::AIUpdate): the trip swing's pace and the prone poses.
void SpriteUpdate(void* sprite, int32_t id, void (*orig)(void*)) {
  int n = 1;
  if (g.trip_pace.Active() && id == g.trip_pace.id) {
    const int ph = static_cast<int>(g.trip_pace.phase);
    n = g.trip_pace.Updates(engine::IsAttackSequence(engine::SpriteSequence(id)));
    CLog("PACE", Name(id) + " phase " + std::to_string(ph) + " n " + std::to_string(n) + " anim " + engine::AnimProbe(id));
  }
  // A mover caught one tick late (it floats: no walk path to hold) waits where it is for the swing.
  if (g.aoo_freeze && g.aoo.Active() && id == g.aoo.mover) n = 0;
  // The reactor's attack round runs at kReactionPace until it swings: its swing slot (weapon speed, rolled at the
  // round start) may be late in the round, and a reaction must not hold the mover for a whole round.
  if (g.aoo.Active() && id == g.aoo.reactor && !g.aoo.swung) n = kReactionPace;
  for (int i = 0; i < n; ++i) orig(sprite);
  if (g.poses.empty()) return;
  const auto it = g.poses.find(id);
  if (it == g.poses.end()) return;
  PronePose& p = it->second;
  const int64_t state = engine::SpriteState(id);
  if (state < 0 || (state & sym::kStateDead)) {  // died on the ground: the engine's own death pose stays
    g.poses.erase(it);
    return;
  }
  const int back = p.Back();
  if (back < 0) {
    engine::SetSpriteSequence(id, sym::kSeqReady);
    g.poses.erase(it);
    return;
  }
  p.Tick(engine::PoseLying(id, back));
}

// A Missile of Patience shot (its TransferEffects): counted per missile in a fight; after the last one it ends.
bool MopShot(uint64_t missile) {
  if (!g.running || g.mop_limit <= 0) return true;
  int& n = g.mop_shots[missile];
  if (n >= g.mop_limit) return false;
  ++n;
  CLog("SPELL", Name(g.current) + ": Missile of Patience shot " + std::to_string(n) + " of " + std::to_string(g.mop_limit));
  Log("tbc: Missile of Patience shot %d of %d", n, g.mop_limit);
  if (n >= g.mop_limit) g.mop_end = true;
  return true;
}
// The hovering missile waits for the player's target (the cursor in its mode).
bool MopWaiting() { return g.mop_hook && engine::MopUp() && engine::GameCursorMode() == sym::kCursorModeTarget; }
// It ends itself on its next update once the cursor leaves the targeting mode (AIUpdate -> RemoveSelf).
void MopFinish(const char* why) {
  if (!MopWaiting()) return;
  engine::SetCursorMode(0);
  Log("tbc: Missile of Patience ends (%s)", why);
}

bool BumpGuard(int32_t id) { return g.running && g.no_push && Contains(g.in, id); }

void ManeuverTurnStart(int32_t id) {
  static const int trace = [] {
    char* v = nullptr;
    size_t n = 0;
    const int t = _dupenv_s(&v, &n, "PSTF_AOO_TRACE") == 0 && v ? std::atoi(v) : 0;
    free(v);
    return t;
  }();
  g.aoo_trace = trace;
  g.aoo_trace_react = trace ? 80 : 0;
  g.aoo_was_in.clear();
  g.aoo_queue.clear();
  g.aoo_walk_checked = false;
  g.takedown_swung = false;
  g.trip_pace.Stop();
  g.ai_moved = false;
  g.ai_path_wait = 0;
  if (g.aoo_stash.held) engine::RestoreQueue(&g.aoo_stash);  // a reaction cut short
  g.aoo = Reaction{};
  if (!g.maneuvers) return;
  Stance& st = g.stance[id];
  const bool prone = st.prone;
  NewTurn(&st);
  if (prone) {
    StandUp(id);
    g.econ.action_used = true;  // standing up is this turn's action
    FloatText(id, "Stands up");
    ++g.ms.stood;
    CLog("MOVE", Name(id) + " stands up and loses its action");
  }
}

void ManeuverFightEnd() {
  for (auto& kv : g.stance)
    if (kv.second.prone) StandUp(kv.first);
  g.stance.clear();
  g.trips.clear();
  g.trip_checks.clear();
  g.trip_pace.Stop();
  if (g.aoo_stash.held) engine::RestoreQueue(&g.aoo_stash);  // a reaction cut short
  g.aoo = Reaction{};
  g.aoo_queue.clear();
  g.fx.clear();
}

// Start an attack of opportunity: the mover waits at the edge of the reach, the reactor swings once.
void StartReaction(int32_t reactor, int32_t mover, bool fudge) {
  g.stance[reactor].reaction_used = true;
  ++g.ms.aoo;
  g.aoo = Reaction{};
  g.aoo.reactor = reactor, g.aoo.mover = mover;
  g.aoo_swing = false;
  std::string order = "Attack(NearestEnemyOf(Myself))";
  const int slot = PartySlot(mover);
  if (slot >= 0) order = "Attack(Player" + std::to_string(slot + 1) + ")";
  // ClearAI (SetCurrAction) clears the engine's attack-round flag: the reach it had when the mover left stays.
  const bool engaged = engine::InAttackRound(reactor);
  if (g.keep_queue && !Contains(g.party, reactor)) {
    // its script sequence (kept for its next turn) waits out the swing; a kept running action does not
    const std::vector<uint16_t> q = engine::ActionQueue(reactor);
    if (!q.empty() && q[0] != 0)
      CLog("QUEUE", Name(reactor) + " attack of opportunity drops its running " + ActionName(q[0]));
    if (q.size() > 1 && engine::StashQueue(reactor, &g.aoo_stash))
      CLog("QUEUE", Name(reactor) + " attack of opportunity: [" + QueueText(q.data() + 1, static_cast<int>(q.size()) - 1) + "] waits");
  }
  engine::ClearAI(reactor);
  const bool ok = engine::EvalAs(reactor, order);
  if (engaged || fudge) engine::KeepAttackRound(reactor);
  engine::NewAttackRound(reactor);  // a reaction swings now, not at its next round's slot
  g.aoo_freeze = fudge;
  g.aoo_fudge = engaged || fudge;  // its reach included RANGE_FUDGE: kept until it swings
  engine::SetNoInterrupt(reactor);
  FloatText(reactor, "Attack of Opportunity!", 2200);
  CLog("AOO", Name(reactor) + " gets an attack of opportunity on " + Name(mover) + " (" + order + (ok ? ")" : ", failed)"));
  Log("tbc: %s: attack of opportunity on %s (%s)", Who(reactor).c_str(), Who(mover).c_str(), ok ? "ordered" : "order failed");
}

// The mover waits for its attacks of opportunity: standing (ready stance, turned toward its way), not walking in place.
void HoldStance(int32_t id) {
  if (engine::SpriteSequence(id) == static_cast<int16_t>(sym::kSeqWalk) && !g.poses.count(id))
    engine::SetSpriteSequence(id, sym::kSeqReady);
}

// A foe that may react to `mover` now: alive, able, its reaction unused, standing.
bool MayReact(int32_t id, int32_t mover, const engine::SpriteInfo& me, engine::SpriteInfo* o) {
  if (id == mover || !Foes(id, mover) || !MayOpportunityId(id)) return false;
  if (!engine::GetSprite(id, o) || !o->alive || o->area != me.area || !CanReact(engine::SpriteState(id))) return false;
  const auto st = g.stance.find(id);
  return st == g.stance.end() || (!st->second.reaction_used && !st->second.prone);
}

// The walk about to start (HookWalk, before the engine moves it): every foe whose reach it stands in and that
// its path leaves anywhere swings first, one after the other (queued, OnTick starts them); the mover turns
// toward its way and waits. Checked once per walk (its destination); a reach entered later on the way provokes
// nothing.
bool WalkStartProvokes(int32_t cur) {
  if (!g.running || !g.opportunity || !g.maneuvers) return false;
  if (g.aoo.Active() || !g.aoo_queue.empty()) return true;
  engine::SpriteInfo me;
  if (!engine::GetSprite(cur, &me) || !me.has_path) return false;
  const auto path = engine::PathPoints(cur);
  if (path.empty()) return false;
  if (g.aoo_walk_checked && path.back() == g.aoo_walk_dest) return false;
  g.aoo_walk_checked = true;
  g.aoo_walk_dest = path.back();
  const auto sit = g.stance.find(cur);
  if (sit != g.stance.end() && sit->second.withdrawn) return false;
  const int ps_me = PersonalSpace(cur);
  for (int32_t id : g.in) {
    engine::SpriteInfo o;
    if (!MayReact(id, cur, me, &o)) continue;
    const int reach = ReachCells(kMeleeRangeField, PersonalSpace(id), ps_me);
    if (PathLeavesReach(o.x, o.y, me.x, me.y, path, reach)) g.aoo_queue.push_back(id);
  }
  if (g.aoo_queue.empty()) return false;
  engine::SetSpriteDirection(cur, Direction16(path[0].first - me.x, path[0].second - me.y));
  CLog("AOO", Name(cur) + " starts to move away: " + std::to_string(g.aoo_queue.size()) +
                  " attack(s) of opportunity before it walks");
  return true;
}

// The move preview (the hover path before a click): would that walk provoke? Same rule as WalkStartProvokes
// (foes that may react, path leaves their reach, Withdraw immune). -> the foes it would provoke.
std::vector<int32_t> PreviewProvokes(int32_t cur, const engine::SpriteInfo& me, const std::vector<Pt>& nodes) {
  std::vector<int32_t> out;
  if (!g.running || !g.opportunity || !g.maneuvers || nodes.empty()) return out;
  const auto sit = g.stance.find(cur);
  if (sit != g.stance.end() && sit->second.withdrawn) return out;
  std::vector<std::pair<int, int>> path;
  for (const Pt& p : nodes) path.emplace_back(p.x, p.y);
  const int ps_me = PersonalSpace(cur);
  for (int32_t id : g.in) {
    engine::SpriteInfo o;
    if (!MayReact(id, cur, me, &o)) continue;
    if (PathLeavesReach(o.x, o.y, me.x, me.y, path, ReachCells(kMeleeRangeField, PersonalSpace(id), ps_me))) out.push_back(id);
  }
  return out;
}

// Per tick while the current creature walks: who did it just walk out of reach of?
void CheckOpportunity(int32_t cur, const engine::SpriteInfo& me) {
  if (!g.opportunity || !g.maneuvers) return;
  // Walkers: the reactions queued as the walk started (WalkStartProvokes), one after the other, the mover
  // standing in their reach. Re-checked as each starts (one may have died or fallen meanwhile).
  while (!g.aoo.Active() && !g.aoo_queue.empty()) {
    const int32_t id = g.aoo_queue.front();
    g.aoo_queue.erase(g.aoo_queue.begin());
    engine::SpriteInfo o;
    if (MayReact(id, cur, me, &o)) StartReaction(id, cur, false);
  }
  if (g.aoo.Active() || me.has_path) return;
  // Creatures that move without a walk path (Ignus floats: AIUpdateFly) are caught one tick late: in reach
  // last tick, just outside it now.
  const std::vector<std::pair<int, int>> path;
  const bool withdrawn = g.stance[cur].withdrawn;
  const int ps_me = PersonalSpace(cur);
  for (int32_t id : g.in) {
    if (id == cur || !Foes(id, cur) || !MayOpportunityId(id)) continue;
    engine::SpriteInfo o;
    if (!engine::GetSprite(id, &o) || !o.alive || o.area != me.area) continue;
    const int64_t state = engine::SpriteState(id);
    if (!CanReact(state)) {
      continue;
    }
    // The engine's plain reach: its swing re-checks range at the hit frame without RANGE_FUDGE (Swing clears it).
    const int reach = ReachCells(kMeleeRangeField, PersonalSpace(id), ps_me);
    const bool in = InReach(o.x, o.y, me.x, me.y, reach);
    const bool leaves = false;
    bool& was = g.aoo_was_in[id];
    const bool late = path.empty() && LeftReachLate(was, in, InReach(o.x, o.y, me.x, me.y, reach + kRangeFudge));
    was = in;
    if (g.aoo_trace > 0) {
      --g.aoo_trace;
      Log("tbc: aoo check %X at %d,%d vs %X at %d,%d: reach %d, d2 %d, in %d, leaves %d, path %zu (%d,%d), withdrawn %d, used %d",
          static_cast<unsigned>(cur), me.x, me.y, static_cast<unsigned>(id), o.x, o.y, reach, CellDist2(o.x, o.y, me.x, me.y), in,
          leaves, path.size(), path.empty() ? -1 : path[0].first, path.empty() ? -1 : path[0].second, withdrawn,
          g.stance[id].reaction_used);
    }
    if (late && !withdrawn && !g.stance[id].reaction_used && !g.stance[id].prone) {
      StartReaction(id, cur, true);  // one cell out: the engine's RANGE_FUDGE lets it swing from there
      return;
    }
  }
}

// One tick of a reaction in progress; true while the turn waits for it.
bool ReactionTick(uint32_t dt) {
  if (!g.aoo.Active()) return false;
  engine::SpriteInfo r;
  const bool alive = engine::GetSprite(g.aoo.reactor, &r) && r.alive;
  if (alive && r.has_path) engine::DropPath(g.aoo.reactor);  // out of reach now: no chase
  // Reach with the engine's RANGE_FUDGE (engaged, or caught a cell late): SetCurrAction clears it as the Attack starts.
  if (alive && g.aoo_fudge && !g.aoo.swung) engine::KeepAttackRound(g.aoo.reactor);
  const bool swung = g.aoo_swing;
  g.aoo_swing = false;
  if (g.aoo_trace_react > 0 && alive) {
    --g.aoo_trace_react;
    engine::SpriteInfo m;
    engine::GetSprite(g.aoo.mover, &m);
    Log("tbc: aoo react %X: action %u path %d seq %d round %d fudge %d, mover at %d,%d d2 %d reach %d", static_cast<unsigned>(g.aoo.reactor),
        r.action, r.has_path ? 1 : 0, engine::SpriteSequence(g.aoo.reactor), engine::AttackRoundPos(g.aoo.reactor),
        engine::InAttackRound(g.aoo.reactor) ? 1 : 0, m.x, m.y, CellDist2(r.x, r.y, m.x, m.y),
        ReachCells(kMeleeRangeField, PersonalSpace(g.aoo.reactor), PersonalSpace(g.aoo.mover)));
  }
  if (alive && !g.aoo.Feed(dt, swung)) return true;
  engine::ClearAI(g.aoo.reactor);
  engine::DropPath(g.aoo.reactor);
  if (g.aoo_stash.held && engine::RestoreQueue(&g.aoo_stash))
    CLog("QUEUE", Name(g.aoo.reactor) + " gets its queue back after the attack of opportunity");
  if (g.aoo.swung) ++g.ms.aoo_swung;
  if (!g.aoo.swung)
    Log("tbc: %s: no attack of opportunity swing (action %u, path %d, state %llX)", Who(g.aoo.reactor).c_str(), alive ? r.action : 0,
        alive && r.has_path ? 1 : 0, static_cast<long long>(engine::SpriteState(g.aoo.reactor)));
  CLog("AOO", Name(g.aoo.reactor) + (g.aoo.swung ? " swung; " : " could not swing; ") + Name(g.aoo.mover) + " walks on");
  g.aoo = Reaction{};
  return false;
}

// The directed AI: once per AI turn, when its script shows its intent (an attack or a flight).
constexpr int kAiPathWaitTicks = 10;
void DirectedAi(int32_t cur, const engine::SpriteInfo& s) {
  if (!g.maneuvers || g.econ.party || g.ai_moved || !ActionFree(g.econ)) return;
  if (EngineDrivesAi(engine::SpriteState(cur))) return;  // confused, panicked, berserk: the engine's choice stands
  const bool attack = IsAttackAction(s.action), flee = IsFleeAction(s.action);
  if (!attack && !flee) {
    g.econ.defer_act = false;
    return;
  }
  AiView v;
  v.melee_intent = attack, v.flee_intent = flee;
  v.move_ft = g.econ.move_ft;
  v.hp = s.hp, v.max_hp = s.max_hp > 0 ? s.max_hp : 1;
  v.may_trip = MayTripId(cur), v.may_block = MayBlockId(cur), v.may_knockdown = MayKnockdownId(cur);
  {
    const Combatant* me = g.dir ? g.dir->Find(cur) : nullptr;
    v.may_withdraw = me && me->r.side == Side::Party;  // enemies never withdraw
  }
  v.attacks = AttacksForBlock();
  v.roll100 = static_cast<int>(Rand() % 100) + 1;
  const int ps_me = PersonalSpace(cur);
  int best = -1, fx = s.x, fy = s.y;
  for (int32_t id : g.in) {
    if (id == cur || !Foes(id, cur)) continue;
    engine::SpriteInfo o;
    if (!engine::GetSprite(id, &o) || !o.alive || o.area != s.area) continue;
    const int reach = ReachCells(kMeleeRangeField, ps_me, PersonalSpace(id));
    const int d2 = CellDist2(s.x, s.y, o.x, o.y);
    if (reach * reach >= d2) ++v.adjacent_foes;
    if (best < 0 || d2 < best) best = d2, v.reach_cells = reach, fx = o.x, fy = o.y;
  }
  if (best < 0) return;
  v.gap_cells = static_cast<int>(std::sqrt(static_cast<double>(best)));
  // Out of reach: decide once it walks, by the path it took (a straight gap of 19 cells can be 40 ft around walls).
  // The economy holds its action meanwhile (defer_act), at most kAiPathWaitTicks: no path by then, the straight gap.
  // The engine may plan a long walk in parts: the rest from the path's end to the foe counts too.
  if (attack && v.gap_cells > v.reach_cells) {
    const auto path = s.has_path ? engine::PathPoints(cur) : std::vector<std::pair<int, int>>{};
    if (path.empty() && ++g.ai_path_wait <= kAiPathWaitTicks) {
      g.econ.defer_act = true;
      return;
    }
    if (!path.empty()) v.path_ft = PathFeet(s.x, s.y, path) + FeetBetween(path.back().first, path.back().second, fx, fy);
  }
  g.econ.defer_act = false;
  g.ai_moved = true;
  const AiMove m = ChooseAiMove(v);
  static const char* kNames[] = {"none", "run", "withdraw", "block", "trip", "knockdown"};
  char pf[32];
  std::snprintf(pf, sizeof pf, ", path %.1f ft of %.1f", v.path_ft, v.move_ft);
  CLog("AI", Name(cur) + " maneuver: " + kNames[static_cast<int>(m)] + " (gap " + std::to_string(v.gap_cells) + " cells" +
                 (v.path_ft > 0 ? pf : "") + ", reach " + std::to_string(v.reach_cells) + ", " + std::to_string(v.adjacent_foes) +
                 " foes in reach, d100 " + std::to_string(v.roll100) + ")");
  if (m != AiMove::None && UseManeuver(cur, m)) ++g.ms.ai_moves;
}

// Dust under a runner's feet (smoke wisps under a withdrawer's) while it walks, left behind on the ground.
void RunDust(int32_t cur, double moved) {
  const auto it = g.stance.find(cur);
  if (it == g.stance.end() || moved <= 0 || !(it->second.ran || it->second.withdrawn)) return;
  const uint32_t now = GetTickCount();
  if (now - g.dust_t < (it->second.ran ? 110u : 170u)) return;
  g.dust_t = now;
  AddFxAtFeet(it->second.ran ? State::FxKind::RunTrail : State::FxKind::SmokeTrail, cur,
              static_cast<int>(Rand() % 9) - 4, static_cast<int>(Rand() % 5) - 2);
}

void Ellipse(int cx, int cy, double r, uint32_t col, float w, double wave, double phase, int n = 28) {
  for (int i = 0; i < n; ++i) {
    const double a0 = 6.283185307 * i / n, a1 = 6.283185307 * (i + 1) / n;
    const double r0 = r * (1.0 + wave * std::sin(6.0 * a0 + phase)), r1 = r * (1.0 + wave * std::sin(6.0 * a1 + phase));
    engine::DrawWorldLine(cx + static_cast<int>(r0 * std::cos(a0)), cy + static_cast<int>(r0 * 0.75 * std::sin(a0)),
                          cx + static_cast<int>(r1 * std::cos(a1)), cy + static_cast<int>(r1 * 0.75 * std::sin(a1)), col, w);
  }
}
uint32_t Dim(uint32_t c, double k) {
  const auto ch = [&](int s) { return static_cast<uint32_t>(((c >> s) & 0xFF) * k) << s; };
  return ch(0) | ch(8) | ch(16);
}
// Ground effects of the maneuvers (dust and smoke puffs, ground_fx.h): soft filled noisy blobs drawn at the
// floor point, under every creature and lit like the ground (engine/soft_draw.h). Without the floor hook
// they fall back to the engine's lines over the sprites, dimmer.
bool PuffKind(State::FxKind k, PuffFx* out) {
  switch (k) {
    case State::FxKind::RunBurst: *out = PuffFx::RunBurst; return true;
    case State::FxKind::RunTrail: *out = PuffFx::RunTrail; return true;
    case State::FxKind::Smoke: *out = PuffFx::Smoke; return true;
    case State::FxKind::SmokeTrail: *out = PuffFx::SmokeTrail; return true;
    case State::FxKind::KnockDust: *out = PuffFx::KnockDust; return true;
    default: return false;
  }
}
bool IsGroundFx(State::FxKind k) {
  PuffFx p;
  return PuffKind(k, &p);
}
void GroundFxPuffs(const State::Fx& f, int x, int y, double ms, std::vector<Puff>* out) {
  PuffFx k;
  if (PuffKind(f.kind, &k)) PuffsFor(k, f.seed, ms, x, y, out);
}
int FxPos(const State::Fx& f, int* x, int* y) {
  *x = f.x, *y = f.y;
  if (f.id != -1) {
    engine::SpriteInfo o;
    if (engine::GetSprite(f.id, &o)) *x = o.x, *y = o.y, engine::InterpDrawPos(f.id, x, y);
  }
  return 0;
}
void DrawGroundFx() {  // floor draw point (gl_world.h InstallWorldFloor)
  if (g.fx.empty()) return;
  const uint32_t now = GetTickCount();
  std::vector<Puff> puffs;
  for (const auto& f : g.fx) {
    if (!IsGroundFx(f.kind)) continue;
    int x, y;
    FxPos(f, &x, &y);
    GroundFxPuffs(f, x, y, now - f.t0, &puffs);
  }
  static_assert(sizeof(Puff) == sizeof(engine::SoftPuff), "ground_fx Puff mirrors engine::SoftPuff");
  std::vector<engine::SoftPuff> soft(puffs.size());
  for (size_t i = 0; i < puffs.size(); ++i) {
    const Puff& p = puffs[i];
    soft[i] = {p.x, p.y, p.rx, p.ry, p.bgr, p.alpha, p.seed};
  }
  if (!soft.empty() && !engine::DrawSoftPuffs(soft.data(), static_cast<int>(soft.size()))) g.ground_fx = false;
}
void FxPreDraw() { engine::ArmWorldComposite(); }  // the floor point runs only on armed frames

void DrawManeuverFx() {
  const uint32_t now = GetTickCount();
  g.fx.erase(std::remove_if(g.fx.begin(), g.fx.end(), [&](const State::Fx& f) { return now - f.t0 > 1400; }), g.fx.end());
  for (auto& f : g.fx) {
    int x, y;
    FxPos(f, &x, &y);
    const double ms = now - f.t0;
    if (IsGroundFx(f.kind)) {
      if (g.ground_fx) continue;  // drawn at the floor point, under the creatures
      std::vector<Puff> puffs;
      GroundFxPuffs(f, x, y, ms, &puffs);
      for (const auto& q : puffs)  // fallback: the engine's lines over the sprites, dimmed by the alpha
        Ellipse(static_cast<int>(q.x), static_cast<int>(q.y), q.rx * 0.6, Dim(q.bgr, q.alpha), 1.0f, 0.15, q.seed, 10);
      continue;
    }
    switch (f.kind) {
      case State::FxKind::Shield: {
        const double u = ms / 900.0;
        if (u > 1) break;
        const double r = 20.0 + 10.0 * u;
        Ellipse(x, y - 36, r, Dim(0x40C8F0, 1.0 - 0.7 * u), 2.6f, 0.0, 0.0);
        Ellipse(x, y - 36, r, Dim(0xD0F8FF, 1.0 - 0.7 * u), 1.0f, 0.0, 0.0);
        break;
      }
      case State::FxKind::Sparks: {
        const double u = ms / 450.0;
        if (u > 1) break;
        for (int k = 0; k < 10; ++k) {
          const double a = 6.283185307 * ((f.seed >> (k % 16)) % 64) / 64.0;
          const double d0 = 4.0 + 26.0 * u, d1 = d0 + 9.0 * (1.0 - u);
          const int cy0 = y - 40;
          engine::DrawWorldLine(x + static_cast<int>(d0 * std::cos(a)), cy0 + static_cast<int>(d0 * std::sin(a)),
                                x + static_cast<int>(d1 * std::cos(a)), cy0 + static_cast<int>(d1 * std::sin(a)),
                                Dim(0x60E8FF, 1.0 - 0.6 * u), 1.1f);
        }
        break;
      }
      default:
        break;
    }
  }
}
// The hotbar's maneuver buttons: usable now, which takedown is armed, block stacks.
std::string ManeuverLua() {
  const bool on = g.running && g.maneuvers && PartySlot(g.current) >= 0 && !g.aoo.Active();
  Stance none;
  const Stance& st = on ? g.stance[g.current] : none;
  const auto b = [](bool v) { return v ? "true" : "false"; };
  std::string s = "pstfMv = {on=";
  s += b(g.maneuvers);
  s += ",run=";
  s += b(on && CanRun(g.econ));
  s += ",withdraw=";
  s += b(on && CanWithdraw(g.econ));
  s += ",trip=";
  s += b(on && CanTrip(g.econ, st, MayTripId(g.current)));
  s += ",block=";
  s += b(on && CanBlock(g.econ, st, MayBlockId(g.current), AttacksForBlock()));
  s += ",armed=";
  s += b(on && st.takedown == Takedown::Trip);
  s += ",stacks=" + std::to_string(st.block_stacks);
  // who may trip / block at all (the hotbar hides the button otherwise); known for a party member's turn
  if (g.running && PartySlot(g.current) >= 0) {
    s += ",mayTrip=";
    s += b(MayTripId(g.current));
    s += ",mayBlock=";
    s += b(MayBlockId(g.current));
  }
  s += "}";
  return s;
}


// --- dice panel and hover card --------------------------------------------------------------------
uint32_t DiceMs() { return g.dice_on ? GetTickCount() - g.dice_t0 : 0; }

void PlayResultSound(bool good) {
  if (g.sound_dir.empty()) return;
  const std::wstring f = g.sound_dir + (good ? L"save_success.wav" : L"save_failed.wav");
  const BOOL ok = PlaySoundW(f.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
  Log("tbc: save sound %s (%s)", good ? "success" : "failed", ok ? "playing" : "not played");
}

// A save shown over the creature that rolled (save_banner.h); the sound waits for the icon's verdict.
void AddSaveBanner(const engine::RollEvent& e) {
  const uint32_t now = GetTickCount();
  for (auto& b : g.banners)
    if (b.id == e.actor && SaveBannerSameRoll(now - b.t0)) return;  // another effect of the same spell
  g.banners.erase(std::remove_if(g.banners.begin(), g.banners.end(), [&](const State::Banner& b) { return b.id == e.actor; }),
                  g.banners.end());
  if (static_cast<int>(g.banners.size()) >= kSaveBanners) g.banners.erase(g.banners.begin());
  g.banners.push_back({e.actor, e.success, SaveBannerText(e.success, e.total, e.save_type), now});
  if (g.ctext) {  // it takes a slot in the creature's combat text stack and sits there
    if (const CtLine* l = CtAdd(e.actor, CtKind::Text, {"", kCtWhite})) {
      g.banners.back().ax = l->ax, g.banners.back().ay = l->ay, g.banners.back().slot = l->slot;
    } else {
      g.banners.back().slot = -1;
    }
  } else {
    g.banners.back().slot = -1;
  }
  ++(e.success ? g.banners_good : g.banners_bad);
  engine::SpriteInfo o;
  const bool at = engine::GetSprite(e.actor, &o);
  Log("tbc: save banner %s: %s (feet %d,%d, text height %d)", Who(e.actor).c_str(), g.banners.back().text.c_str(),
      at ? o.x : -1, at ? o.y : -1, engine::SpriteTextHeight(e.actor));
}

// Every tick: the verdict sound (once per tick, the newest verdict wins) and expired banners.
void SaveBannerTick() {
  if (g.banners.empty()) return;
  const uint32_t now = GetTickCount();
  int sound = -1;
  for (auto& b : g.banners)
    if (!b.sounded && now - b.t0 >= kSaveVerdictMs) {
      b.sounded = true;
      sound = b.good ? 1 : 0;
    }
  if (sound >= 0 && g.save_sounds) PlayResultSound(sound == 1);
  g.banners.erase(std::remove_if(g.banners.begin(), g.banners.end(),
                                 [&](const State::Banner& b) { return now - b.t0 >= kSaveLifeMs; }),
                  g.banners.end());
}

// The engine's rising text (engine::InstallFloaterHook): over a creature, it becomes combat text that sits still;
// "Damage: 7" a bare number in its damage type's colour, "Heal: 3" a green one. The engine's own crit line gives way
// to ours (from the roll). Messages at a world point (no creature) stay the engine's.
bool OnFloater(const engine::FloaterInfo& f) {
  ++g.ct_floaters;
  static int logged = 0;
  if (logged < 80) {
    ++logged;
    Log("tbc: floater \"%s\" owner %X type %u colour %06X caller %X%s", f.text.c_str(), static_cast<unsigned>(f.owner), f.type,
        f.color, f.caller_rva, f.in_damage ? " (damage effect)" : "");
  }
  if (!g.ctext || f.type == 5) return false;  // 5: a creature's own words (CGameAIBase::FloatMessage) stay the engine's
  const int32_t id = f.owner != -1 ? f.owner : f.damage_target;
  engine::SpriteInfo o;
  if (id == -1 || !engine::GetSprite(id, &o)) return false;
  const FloaterParse p = ParseFloater(f.text, f.in_damage);
  if (p.kind == CtKind::Damage || p.kind == CtKind::Heal) {
    const uint32_t colour = p.kind == CtKind::Heal ? kCtHeal : DamageColor(f.damage_bits);
    const CtLine* l = CtAdd(id, p.kind, {std::to_string(p.value), colour, p.value});
    if (l && g.ct.Merged()) {
      std::string row;
      for (const CtSeg& s : l->segs) row += (row.empty() ? "" : " ") + s.text;
      Log("tbc: text over %s: %d added to the same damage type's number (row \"%s\")", Who(id).c_str(), p.value, row.c_str());
    }
  } else if (g.rolls && f.text.compare(0, 8, "Critical") == 0) {
    // AttackCombatText says it
  } else if (const std::string t = CleanFloaterText(f.text); !t.empty()) {
    CtAdd(id, CtKind::Text, {t, kCtWhite});
  }
  ++g.ct_swallowed;
  return true;
}

// A buff or debuff that starts shows its name over the creature; one that ends shows it struck through. Party
// members and everyone in the fight, 4 times a second; a creature's first look only records what it has.
void WatchStatuses() {
  static uint32_t t0 = 0;
  const uint32_t now = GetTickCount();
  if (!g.ctext || !g.res || now - t0 < 250) return;
  t0 = now;
  std::vector<int32_t> ids = engine::PartyIds();
  if (g.running)
    for (int32_t id : g.in)
      if (!Contains(ids, id)) ids.push_back(id);
  std::map<int32_t, std::map<std::string, int32_t>> next;
  for (int32_t id : ids) {
    engine::SpriteInfo o;
    if (!engine::GetSprite(id, &o) || !o.alive) continue;
    std::map<std::string, int32_t> have;
    for (const StatusIconView& v : StatusIconsOf(id, kStatusHoverPx, 24))
      if (v.name_ref >= 0) have[v.parent] = v.name_ref;
    const auto it = g.ct_status.find(id);
    if (it != g.ct_status.end()) {
      for (const auto& [parent, ref] : have)
        if (!it->second.count(parent)) {
          CtAdd(id, CtKind::StatusOn, {"@" + std::to_string(ref), kCtWhite});
          Log("tbc: %s: status %s on", Who(id).c_str(), parent.c_str());
        }
      for (const auto& [parent, ref] : it->second)
        if (!have.count(parent)) {
          CtAdd(id, CtKind::StatusOff, {"@" + std::to_string(ref), kCtWhite});
          Log("tbc: %s: status %s off", Who(id).c_str(), parent.c_str());
        }
    }
    next[id] = std::move(have);
  }
  g.ct_status = std::move(next);
}

// "Miss!" / "Critical Miss!" / "Critical Hit!" over the target of an attack roll (combat_text.h).
void AttackCombatText(const engine::RollEvent& e) {
  const char* t = AttackText(e.success, e.crit, e.roll, e.outcome);
  if (!*t || e.target == -1) return;
  const auto nm = g.ct_no_miss.find(e.target);
  if (!e.success && nm != g.ct_no_miss.end() && GetTickCount() - nm->second < 500) return;  // "Trip: Missed" said it
  const CtKind k = AttackKind(t);
  CtAdd(e.target, k, {t, k == CtKind::Crit ? kCtRed : kCtWhite});
}

// New rolls from the engine hooks: the last one involving a combatant goes on the dice panel.
void TakeRolls() {
  bool sound = false, sound_good = false;
  for (const auto& e : engine::TakeRollEvents()) {
    // CheckSave posts its "Save vs. ...: N" feedback line in the same call that queued this event:
    // the sound goes with that line, this frame (the last save of the frame wins).
    if (e.kind == engine::RollEvent::Save && g.save_banner) AddSaveBanner(e);
    else if (e.kind == engine::RollEvent::Save && g.save_sounds) sound = true, sound_good = e.success;
    if (e.kind == engine::RollEvent::Attack && g.ctext) AttackCombatText(e);
    if (!g.running || !Contains(g.in, e.actor)) continue;
    if (e.kind == engine::RollEvent::Attack && e.actor == g.current) g.econ.Swing();
    if (e.kind == engine::RollEvent::Attack && e.actor == g.aoo.reactor) g.aoo_swing = true;
    Log("tbc: roll: %s %s d20 %d total %d needs %d (site %d) -> %s", Who(e.actor).c_str(),
        e.kind == engine::RollEvent::Attack ? ("attacks " + Who(e.target)).c_str() : "saves", e.roll, e.total, e.needs, e.site,
        e.success ? (e.kind == engine::RollEvent::Attack ? "hit" : "saved") : (e.kind == engine::RollEvent::Attack ? "miss" : "failed"));
    {
      char b[128];
      if (e.kind == engine::RollEvent::Attack)
        std::snprintf(b, sizeof b, " attacks %s: d20 %d, total %d, needs %d -> %s", Name(e.target).c_str(), e.roll, e.total,
                      e.needs, e.outcome == 1 ? "hit, BLOCKED" : e.outcome == 2 ? "hit, takedown (no damage)" : e.success ? "HIT" : "miss");
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
    v->dice_result = e.outcome == 1 ? "Blocked!" : e.outcome == 2 ? (e.roll == 20 ? "Critical hit! Takedown" : "Hit: takedown")
                     : e.roll == 20 ? "Critical hit!" : e.roll == 1 ? "Critical miss" : e.success ? "Hit" : "Miss";
  } else {
    std::snprintf(b, sizeof b, "%d %s %d = %d", e.roll, mod < 0 ? "-" : "+", mod < 0 ? -mod : mod, e.total);
    v->dice_result = e.success ? "Saved" : "Failed";
  }
  v->dice_sum = b;
}

// A creature's status sources: its effects with their parent, and its PST attached spell objects
// (parentless: AttributeOrphans gives them the spell seen on its way, e.g. Guardian Mantle).
std::vector<StatusEffect> StatusSources(int32_t id) {
  std::vector<StatusEffect> fx;
  for (const auto& e : engine::ParentEffects(id))
    fx.push_back({e.parent_type, e.parent, e.parent_flags, e.left, e.timed, e.delayed, e.key, e.opcode});
  for (const auto& a : engine::AttachedObjects(id)) fx.push_back({0, "", 0, a.left, true, false, a.key});
  return fx;
}

// Parentless spell effects get the spell seen on its way to the creature (status.h AttributeOrphans).
void TrackOrphans(int32_t id) {
  std::vector<StatusEffect> fx = StatusSources(id);
  uint32_t now = 0;
  bool running = false;
  engine::WorldTime(&now, &running);
  AttributeOrphans(id, &fx, now, &g.orphans);
}

// Status icons (buffs/debuffs) of a creature: one per spell or item with timed effects on it.
std::vector<StatusIconView> StatusIconsOf(int32_t id, int box, size_t max_icons) {
  std::vector<StatusIconView> out;
  if (!g.res) return out;
  std::vector<StatusEffect> fx = StatusSources(id);
  uint32_t now = 0;
  bool running = false;
  engine::WorldTime(&now, &running);
  AttributeOrphans(id, &fx, now, &g.orphans);
  // rounds of effect time: the round clock steps g.effect_round_ticks (75) per turn start, not ROUND_LENGTH (51)
  const int round = static_cast<int>(g.effect_round_ticks);
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
  std::vector<int32_t> cand;
  if (g.running) {
    for (int32_t id : g.in) {
      engine::SpriteInfo o;
      if (engine::GetSprite(id, &o) && o.alive) cand.push_back(id);
    }
  } else {
    for (const auto& s : engine::AllSprites())
      if (s.area == g.area && s.alive) cand.push_back(s.id);
  }
  // Whom a click would select or attack: the engine's pick (inside the selection circle). The probe point asks
  // the engine's own hit test (CGameSprite::IsOver) of each candidate instead.
  int32_t picked = -1;
  if (g.hover_at.x >= 0) {
    for (int32_t c : cand)
      if (engine::SpriteIsOver(c, wx, wy)) {
        picked = c;
        break;
      }
  } else {
    picked = engine::PickedId();
  }
  const int32_t id = HoverPick(cand, picked);
  {
    static int32_t last = -2;
    if (id != last) {
      last = id;
      Log("tbc: hover card: %s (%s pick %X at %d,%d)", id == -1 ? "none" : Who(id).c_str(),
          g.hover_at.x >= 0 ? "probe IsOver" : "engine", static_cast<unsigned>(picked), wx, wy);
    }
  }
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
  v->level = HoverLevel(engine::DerivedStat(id, 34), engine::DerivedStat(id, 68), engine::DerivedStat(id, 69));  // LEVEL, LEVEL2, LEVEL3
  if (v->level < 0 || v->level > 99) v->level = 0;
  uint8_t cls = 0, spec = 0;
  engine::SpriteClass(id, &cls, &spec);
  v->sub = HoverSubtitle(cls, o.ea, PartySlot(id) >= 0);
  if (!g.running) return;
  const Combatant* c = g.dir->Find(id);
  v->foe = c && c->r.side == Side::Hostile;
  const int32_t me = PartySlot(g.current) >= 0 ? g.current : (g.party.empty() ? -1 : g.party[0]);
  engine::CombatStats a, t;
  if (v->foe && me != -1 && engine::GetCombatStats(me, &a) && engine::GetCombatStats(id, &t)) {
    v->ac = t.ac;
    v->needs = NeedsToHit(a.thac0, t.ac);
    v->chance = HitChancePercent(a.thac0, t.ac);
    // BG3: "80%" next to the attack cursor, on a party member's own turn while no spell/item is being aimed
    v->aim = PartySlot(g.current) >= 0 && engine::GameCursorState() == 0 && engine::WorldToUi(wx, wy, &v->cur_x, &v->cur_y);
    if (v->aim) v->px_w = GameClientWidth();
    static int32_t logged = -1;
    if (v->aim && logged != id) {
      logged = id;
      Log("tbc: hover %s: %d%% to hit for %s (THAC0 %d vs AC %d, needs %d)", Who(id).c_str(), v->chance, Who(me).c_str(), a.thac0,
          t.ac, v->needs);
    }
  }
}

// Every frame (all screens): our menus on the stack where they belong; on the world screen and the menu
// screens, the status icons next to the portraits (4 times a second) and the hover card (world only).
void UpdateStatusHud(engine::Screen scr) {
  if (!g.hud) return;
  std::string code = "if pstfHudGuard then pstfHudGuard() end";
  if (!g.running) code += g.maneuvers ? " if pstfMv then pstfMv.on = true end" : " if pstfMv then pstfMv.on = false end";
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
  DrawManeuverFx();
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
    const bool show = ShowPathLine(v.party_turn, me.action);
    if (!show) nodes.clear();  // enemies' and allies' paths, and a caster's (path_line.h ShowPathLine)
    // Aiming a spell, ability or item: no hover line (the engine's own path shows once the order
    // needs a walk, e.g. a touch spell out of reach).
    const bool aiming = engine::GameCursorState() != 0;
    bool preview = false;
    if (show && nodes.empty() && g.econ.party && g.econ.MoveLeft() && !g.econ.acting && !aiming) {
      nodes = HoverPreview(me);
      preview = !nodes.empty();
    }
    // the walk under the cursor would leave a foe's reach: say so under the distance (BG3)
    const std::vector<int32_t> prov = preview ? PreviewProvokes(g.current, me, nodes) : std::vector<int32_t>{};
    v.provokes = !prov.empty();
    if (preview) {  // logged when the walker or the foes it would provoke change
      static std::string logged;
      std::string who;
      for (int32_t r : prov) who += (who.empty() ? "" : ", ") + Who(r);
      const std::string key = Who(g.current) + "|" + who;
      if (key != logged) {
        logged = key;
        Log("tbc: preview: %s walking to %d,%d %s", Who(g.current).c_str(), nodes.back().x, nodes.back().y,
            v.provokes ? ("provokes attacks of opportunity from " + who).c_str() : "provokes nothing");
      }
    }
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
  const std::string code = BuildHudLua(v) + " " + ManeuverLua() + bar +
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
  if (!g.running && g.rolls) TakeRolls();  // in a fight the turn code takes them
  SaveBannerTick();
  WatchStatuses();
  g.ct.Expire(GetTickCount());
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
  CheckLostTurn(dt);
  if (g.current != -1) {  // when the current creature's last projectile went (its effects arrive just after)
    engine::SpriteInfo cs;
    const bool up = engine::GetSprite(g.current, &cs) && CasterProjectiles(g.current, cs.area).flying > 0;
    g.since_flying = up ? 0 : std::min<uint32_t>(g.since_flying + dt, 1000);
  }
  if (g.current != -1 && g.keep_queue && !g.econ.party && !g.cast_done && g.current != g.nudged &&
      IsCastAction(g.clog_action)) {
    // A Spell its script queued after a spent one waits a whole casting aura (a round of its own ticks) before the
    // engine finds it spent too: Shilandra's list of five took four turns of nothing. The turn already limits it to
    // one cast, so the aura does not wait inside its turn: a spent spell fails at once, the next in the list goes.
    int32_t in_progress = 0;
    int16_t counter = -1;
    if (engine::CastState(g.current, &in_progress, &counter, nullptr) && !in_progress && counter >= 0 &&
        engine::CurrentSpell(g.current).empty()) {
      engine::SetCastAura(g.current, -1);
      CLog("QUEUE", Name(g.current) + ": casting aura " + std::to_string(counter) + " -> -1 (no cast yet this turn)");
    }
  }
  if (g.current != -1 && !g.cast_live && g.econ.action_used && IsCastAction(g.clog_action) &&
      !engine::CurrentSpell(g.current).empty())
    g.cast_live = true;  // a cast after another action took the turn's action (its patrol's first step)
  // The spell being cast now: parentless effects its projectile builds take it for their icon (status.h NoteCast).
  if (g.current != -1 && g.res &&
      ((g.econ.acting && IsCastAction(g.econ.act_action)) || g.cast_live)) {  // +0x47F8 outlives the cast
    const std::string res = engine::CurrentSpell(g.current);
    static std::string last_cast;
    if (res != last_cast) {
      Log("tbc: %s: spell being cast %s (parentless effects' fallback parent)", Who(g.current).c_str(),
          res.empty() ? "none" : res.c_str());
      last_cast = res;
    }
    if (!res.empty()) {
      g.cast_live = true;
    } else if (g.cast_live) {
      // Its spell went off. An AI's script may queue the next one at once (Spell; Spell; Spell with no Wait): one
      // cast per turn, the rest is its next turns' (the casting aura would start the next in about 2 s).
      g.cast_live = false;
      if (g.keep_queue && !g.econ.party && g.current != g.nudged && !g.cast_done) {
        g.cast_done = true;
        g.freeze_ai = true;
        CLog("QUEUE", Name(g.current) + ": its spell went off: its script waits for its next turn");
      }
    }
    if (!res.empty()) {
      const SpellFacts* f = g.res->Spell(res);
      g.orphans.NoteCast(res, f ? f->flags : 0, now);
      g.turn_cast_res = res, g.turn_cast_flags = f ? f->flags : 0;
    }
  } else if (g.current != -1 && !g.turn_cast_res.empty()) {
    // its projectile still on the way (Howl of Pandemonium's wind stuns the far ones seconds after the cast): what it
    // builds belongs to that spell too, until the projectile is gone
    engine::SpriteInfo cs;
    if (engine::GetSprite(g.current, &cs) && CasterProjectiles(g.current, cs.area).flying > 0)
      g.orphans.NoteCast(g.turn_cast_res, g.turn_cast_flags, now);
  }
  // A persistent effect added in a fight (Prayer's aura, Detect Evil) is frozen with its owner until the owner's next
  // turn start; real time would run its first update on the next tick: run it now, in the turn it was cast.
  if (g.round_effects && engine::EffectClockInstalled())
    for (int32_t id : g.in) {
      const int n = engine::PersistentCount(id);
      const auto it = g.persist_seen.find(id);
      uint32_t& pulsed = g.persist_pulsed[id];
      if (it != g.persist_seen.end() && n > it->second && pulsed != g.turn_w0 + 1) {
        pulsed = g.turn_w0 + 1;
        engine::PulsePersistent(id);
        Log("tbc: %s: new persistent effect (%d -> %d): first pulse now", Who(id).c_str(), it->second, n);
      }
      g.persist_seen[id] = engine::PersistentCount(id);
    }
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
  // A waiting combatant is never pushed aside (BumpGuard); should it still walk (a path or the walk
  // sequence left over), it stops: no walking in place.
  for (int32_t id : g.in) {
    TrackOrphans(id);
    if (!Held(id)) continue;
    engine::SpriteInfo o;
    if (!engine::GetSprite(id, &o)) continue;
    if (o.has_path) {
      engine::DropPath(id);
      CLog("PUSH", Name(id) + " was pushed while waiting: path dropped");
    } else if (engine::SpriteSequence(id) == sym::kSeqWalk && !g.poses.count(id)) {
      engine::SetSpriteSequence(id, sym::kSeqReady);
      CLog("PUSH", Name(id) + " walked in place while waiting: back to its ready stance");
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
    g.prev_action = g.clog_action;
    g.clog_action = s.action;
  }
  if (g.maneuvers) {
    ApplyTrips();
    // The armed takedown's swing begins: its sound now, a slow wind-up (SpriteUpdate).
    const auto sit = g.stance.find(cur);
    if (sit != g.stance.end() && sit->second.takedown != Takedown::None && !g.takedown_swung && !g.trip_pace.Active() &&
        engine::IsAttackSequence(engine::SpriteSequence(cur))) {
      g.trip_pace.Start(cur);
      QueueSound("PFSNDTRP");
      CLog("MOVE", Name(cur) + " " + TakedownName(sit->second.takedown) + " swing starts (slow wind-up)");
    }
  }
  if (ReactionTick(dt)) {
    // An attack of opportunity: the mover waits at the edge of the reach, its turn does not run.
    if (g.rolls) TakeRolls();
    LogHpChanges();
    FlushSounds();
    UpdateHud(now);
    return;
  }
  DirectedAi(cur, s);  // before the economy sees its attack: a run, block or takedown comes first
  // The camera is not here: it follows the DRAWN position on every loop iteration (CameraTick), not the
  // position this tick recorded.
  g.clock.Feed(dt, s.busy);  // busy-tick count for the log only
  // A quick item used between two frames still takes the action (UseItem = 34).
  uint16_t fed = g.items && engine::TookItemAction(cur) && IsIdleAction(s.action) ? 34 : s.action;
  if (!g.econ.party) g.econ.engine_driven = EngineDrivesAi(engine::SpriteState(cur));
  // A Spell that ended without a cast (spent): its action comes back (economy.h Refund).
  if (g.keep_queue && !g.econ.party && cur != g.nudged && g.econ.action_used && !g.cast_done && !g.cast_live &&
      IsCastAction(g.econ.act_action) && IsCastAction(g.prev_action) && g.clog_action == s.action &&
      !IsCastAction(s.action) && g.prev_action != 0xFFFF &&
      !(g.cast_feedback && engine::SpellCasts(cur) > g.turn_casts) && g.refunds < 8) {
    ++g.refunds;
    g.econ.Refund();
    CLog("QUEUE", Name(cur) + ": its " + ActionName(g.prev_action) + " ended without a cast (spent): the action is not used");
    g.prev_action = 0xFFFF;
  }
  if (g.keep_queue && !g.econ.party && !g.econ.action_used && !g.econ.engine_driven && cur != g.nudged &&
      IsWaitAction(s.action) && g.lead_skips < 8 && !g.cast_done &&
      !(g.cast_feedback && engine::SpellCasts(cur) > g.turn_casts)) {  // an instant cast this tick is its action
    // Its script waits before its action (Wait(1); Spell(...)): the wait is skipped, not replaced by an attack.
    const std::vector<uint16_t> q = engine::ActionQueue(cur);
    if (SkipLeadWait(q.data(), static_cast<int>(q.size())) && engine::EndCurrentAction(cur)) {
      ++g.lead_skips;
      const std::string t = QueueText(q.data(), static_cast<int>(q.size()));
      Log("tbc: %s: skips its script's %s before its action [%s]", Who(cur).c_str(), ActionName(s.action).c_str(), t.c_str());
      CLog("QUEUE", Name(cur) + " skips " + ActionName(s.action) + " before its action [" + t + "]");
      fed = 0;
      g.econ.total = 0;  // its script picks its action now: the think time starts again
    }
  }
  EconomyStep st = g.econ.Feed(dt, moved, s.has_path, fed);
  if (g.cast_feedback) {
    const int casts = engine::SpellCasts(cur);
    if (casts > g.turn_casts && g.econ.SawCast()) {
      Log("tbc: %s: a cast the action economy did not see (no casting time) takes the action", Who(cur).c_str());
      CLog("ACTION", Name(cur) + " casts at once: the action is used");
    }
    g.turn_casts = casts;
  }
  // An attack that has not swung yet only walked: its action is still there to run with.
  if (st.stop == Stop::OutOfMove && g.maneuvers && !g.econ.party && IsAttackAction(s.action) && g.econ.action_used &&
      g.econ.swings == 0 && IsAttackAction(g.econ.act_action) && !g.stance[cur].ran)
    g.econ.action_used = false, g.econ.acting = false;
  if (st.stop == Stop::OutOfMove && g.maneuvers && AiRunsWhenShort(g.econ, IsAttackAction(s.action), true) &&
      UseManeuver(cur, AiMove::Run)) {
    // Stopped short of its target with the action free: it runs the rest of the way (the action goes).
    ++g.ms.ai_moves;
    ++g.ms.short_runs;
    CLog("AI", Name(cur) + " would stop short of its target: runs instead");
    st.stop = Stop::None;
    st.hold_walk = false;
    st.end_turn = false;  // OutOfMove with the action used ended it: the run goes on
  }
  if (!g.econ.party && s.has_path && !g.steady_ai) {
    // Sure of itself: its script does not re-plan the walk every pass (stop-and-go); it walks there in
    // one go, then acts. The engine lifts SetInterrupt(FALSE) when its action queue runs empty.
    g.steady_ai = true;
    engine::SetNoInterrupt(cur);
  }
  g.hold_current = st.hold_walk;
  if (g.maneuvers) {
    RunDust(cur, moved);
    CheckOpportunity(cur, s);
    if (g.aoo.Active() || !g.aoo_queue.empty()) {
      g.hold_current = true;
      HoldStance(cur);
      FlushSounds();
      if (g.rolls) TakeRolls();
      UpdateHud(now);
      return;
    }
  }
  if (st.nudge && EngineDrivesAi(engine::SpriteState(cur))) {
    // confused / panicked / berserk: standing still is the engine's choice too; the turn ends on the idle wait
    CLog("AI", Name(cur) + " is confused, panicked or berserk: its own choice stands (no attack ordered)");
    st.nudge = false;
  }
  if (st.nudge && g.keep_queue && !g.econ.party) {
    const std::vector<uint16_t> q = engine::ActionQueue(cur);
    if (QueueHoldsScriptState(q.data(), static_cast<int>(q.size()))) {
      const std::string t = QueueText(q.data(), static_cast<int>(q.size()));
      Log("tbc: %s: not nudged: its script still sets a variable behind its %s [%s]; the turn ends on the wait",
          Who(cur).c_str(), ActionName(s.action).c_str(), t.c_str());
      CLog("QUEUE", Name(cur) + " not nudged: script state waits behind " + ActionName(s.action) + " [" + t + "]");
      st.nudge = false;
      st.end_turn = true;  // no Stop: a stop clears the AI; the turn end keeps the queue (keep_queue)
    }
  }
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
  if (g.cast_done && !st.end_turn) {
    g.econ.acting = false;
    st.stop = Stop::None;
    st.end_turn = true;  // its one cast this turn went off
  }
  if (st.stop == Stop::NoAction && g.keep_queue && !g.econ.party && cur != g.nudged && !g.econ.engine_driven) {
    // Its script's next action after the one this turn had (Spell; Spell; Attack): the turn ends here and that
    // action starts its next turn (real time would run it next as well), instead of the rest being thrown away.
    if (!g.keep_current)
      CLog("STOP", Name(cur) + ": No action left this turn: " + ActionName(s.action) + " waits for its next turn");
    st.stop = Stop::None;
    st.end_turn = true;
    g.keep_current = true;
    g.freeze_ai = true;
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
  // Missile of Patience: the turn waits while it hovers for its target (End Turn ends it), its last shot ends it.
  if (g.mop_end) {
    g.mop_end = false;
    MopFinish("last shot");
  }
  if (MopWaiting()) {
    if (g.end_requested) MopFinish("End Turn");
    else st.end_turn = false;
  }
  // Force Missiles, Elemental Strike, Ice Knife: the player aims each missile after the first (the engine's target
  // cursor); the turn waits for those clicks, End Turn drops the rest. Party AI never aims them (as in real time).
  if (!g.auto_party && engine::MultiFireLeft(cur) > 0 && engine::GameCursorMode() == sym::kCursorModeTarget) {
    if (g.end_requested) {
      Log("tbc: %s: %d missiles not aimed: dropped (End Turn)", Who(cur).c_str(), engine::MultiFireLeft(cur));
      engine::MultiFireDrop(cur);
    } else {
      st.end_turn = false;
    }
  }
  bool wait_spell = false;
  if (st.end_turn || g.end_requested) {
    // The turn does not end before its spell has finished for this round (economy.h TurnWaitsForSpell).
    const Pending pend = CasterProjectiles(cur, s.area);
    SpellWaitIn in;
    in.casting = g.econ.acting && IsCastAction(g.econ.act_action);
    in.cast_this_turn = g.econ.action_used && g.econ.act_action != 3;  // a melee swing has no projectile
    in.since_action = g.since_action;
    in.flying = pend.flying;
    in.lingering = pend.lingering;
    for (int32_t id : g.in) in.onsets += engine::ResolvingOnsets(id);
    in.waited = g.resolve_ticks;
    in.since_flying = g.since_flying;
    {
      const int32_t owner = engine::GameFreezeOwner();
      if (owner != -1)
        for (const auto& o : engine::AllObjects())
          if (o.id == owner && o.source == cur) in.cinematic = true;
    }
    const SpellWait w = TurnWaitsForSpell(in);
    if (w == SpellWait::Capped && !g.resolving_capped) {
      g.resolving_capped = true;
      int32_t cip = 0;
      int16_t aura = 0;
      bool crun = false;
      engine::CastState(cur, &cip, &aura, &crun);
      const std::string res = engine::CurrentSpell(cur);
      Log("tbc: %s: WARNING spell hold cap: turn ends after %u ticks (spell %s, action %d, cast flag %d, aura %d, running %d; "
          "casting %d, %d flying, %d pulsing, %d onsets)", Who(cur).c_str(), g.resolve_ticks, res.empty() ? "-" : res.c_str(),
          s.action, cip, aura, crun ? 1 : 0, in.casting ? 1 : 0, in.flying, in.lingering, in.onsets);
      CLog("SPELL", Name(cur) + ": spell " + (res.empty() ? std::string("-") : res) +
                        " still not finished after 4 rounds: turn ends anyway (aura " + std::to_string(aura) + ")");
      for (const auto& o : engine::AllObjects())
        if (o.source == cur && o.area == s.area && o.type == 0)
          Log("tbc: %s: still up: projectile %X (AIUpdate %llX): %s", Who(cur).c_str(), static_cast<unsigned>(o.id),
              static_cast<unsigned long long>(engine::ProjectileUpdateFn(o.id)), engine::CinematicProbe(o.id).c_str());
    }
    if (w != SpellWait::None && w != SpellWait::Capped) {
      wait_spell = true;
      if (g.keep_queue && !g.econ.party && st.end_turn && !in.casting && cur != g.nudged && !g.freeze_ai) {
        g.freeze_ai = true;
        CLog("QUEUE", Name(cur) + ": turn over, its script waits while its spell lands");
      }
      g.resolve_ticks += dt;
      if (w != g.resolving_why) {
        g.resolving_why = w;
        if (w != SpellWait::Appearing) {
          const std::string res = engine::CurrentSpell(cur);
          Log("tbc: %s: turn waits for its spell %s (%s: casting %d, %d flying, %d pulsing, %d onsets)", Who(cur).c_str(),
              res.empty() ? "-" : res.c_str(), SpellWaitName(w), in.casting ? 1 : 0, in.flying, in.lingering, in.onsets);
          CLog("SPELL", Name(cur) + ": turn waits for its spell " + (res.empty() ? std::string("-") : res) + " (" +
                            SpellWaitName(w) + ")");
        }
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
                    g.econ.action_used ? "used" : "unused", g.econ.walked_ft, g.econ.max_ft, g.econ.total);
      CLog("END", Name(cur) + b);
    }
    Log("tbc: %s ends turn (%s, action %s, %.1f/%.1f ft left, busy %u ticks, total %u) hp:%s",
        Who(cur).c_str(), g.end_requested ? "End Turn" : "economy",
        g.econ.action_used ? "used" : "unused", g.econ.move_ft, g.econ.max_ft, g.clock.busy,
        g.econ.total, hps.c_str());
    int64_t hp_total = 0;
    for (int32_t id : g.in) {
      engine::SpriteInfo o;
      if (engine::GetSprite(id, &o)) hp_total += o.hp;
    }
    const bool cast = g.cast_feedback && engine::SpellCasts(cur) > g.turn_casts;
    g.stalemate.Turn(g.econ.walked_ft >= 1.0, g.econ.swings > 0, cast, hp_total, static_cast<uint32_t>(g.in.size()));
    g.dir->EndTurn(cur);
    if (!g.dir->Running()) {
      EndFight("one side is gone");
      return;
    }
    if (g.stalemate.Round(g.dir->Round(), static_cast<uint32_t>(g.stalemate_rounds > 0 ? g.stalemate_rounds : 0))) {
      char why[96];
      std::snprintf(why, sizeof why, "stalemate: %u whole rounds with no walk, swing, cast or HP change", g.stalemate.quiet);
      g.stale_hold.at.clear();
      for (int32_t id : g.in) {
        engine::SpriteInfo o;
        if (engine::GetSprite(id, &o)) g.stale_hold.at.push_back({id, o.x, o.y, o.hp});
      }
      g.stale_logged = false;
      EndFight(why);
      return;
    }
  }
  LockSelection();
  if (g.rolls) TakeRolls();
  LogHpChanges();
  FlushSounds();
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
// PSTF_Side(id[, quiet]) -> its fight side (0 not in the fight, 1 party side, 2 hostile side), its current
// action's target id (-1 none), its action id, its EA, its script name.
int __cdecl LuaSide(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const bool quiet = engine::LuaNumberArg(L, 2, 0) != 0;
  const Combatant* c = g.running && g.dir ? g.dir->Find(id) : nullptr;
  const int side = c ? (c->r.side == Side::Hostile ? 2 : 1) : 0;
  engine::SpriteInfo s;
  const bool ok = engine::GetSprite(id, &s);
  if (!quiet)
    Log("lua: PSTF_Side(%X) -> side %d, target %X, action %u, ea %u", static_cast<unsigned>(id), side,
        static_cast<unsigned>(ok ? s.target : -1), ok ? s.action : 0, ok ? s.ea : 0);
  engine::LuaPushNumber(L, side);
  engine::LuaPushNumber(L, ok ? s.target : -1);
  engine::LuaPushNumber(L, ok ? s.action : 0);
  engine::LuaPushNumber(L, ok ? s.ea : -1);
  engine::LuaPushString(L, engine::ScriptName(id).c_str());
  return 5;
}
// PSTF_MakeEnemy(id) -> the engine's Enemy() on that creature.
// PSTF_GroundFx(kind, id) -> 1: plays a maneuver ground effect at the creature's feet (0 run burst, 1 run trail,
// 2 withdraw smoke, 3 withdraw trail, 4 knock-down dust), as UseManeuver/RunDust/ApplyTrips do. For screenshots.
int __cdecl LuaGroundFx(engine::LuaState* L) {
  const int k = static_cast<int>(engine::LuaNumberArg(L, 1, 0));
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 2, -1));
  static const State::FxKind kinds[] = {State::FxKind::RunBurst, State::FxKind::RunTrail, State::FxKind::Smoke,
                                        State::FxKind::SmokeTrail, State::FxKind::KnockDust};
  const bool ok = k >= 0 && k < 5;
  if (ok) AddFxAtFeet(kinds[k], id);
  Log("lua: PSTF_GroundFx(%d, %X) -> %d (ground_fx %d)", k, static_cast<unsigned>(id), ok ? 1 : 0, g.ground_fx ? 1 : 0);
  engine::LuaPushBool(L, ok);
  return 1;
}
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
// PSTF_SetHp(id, hp[, max]): test helper (keeps a target alive across a spell suite) -> 1 when the engine took it.
// The engine clamps current HP to max HP when it re-evaluates effects: max (> 0) sets the base max HP too.
int __cdecl LuaSetHp(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const int hp = static_cast<int>(engine::LuaNumberArg(L, 2, -1));
  const int max_hp = static_cast<int>(engine::LuaNumberArg(L, 3, 0));
  const bool ok = engine::SetHpForTest(id, hp, max_hp);
  Log("lua: PSTF_SetHp(%X, %d, max %d) -> %d", static_cast<unsigned>(id), hp, max_hp, ok ? 1 : 0);
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
// PSTF_SaveBanner(i) -> on (0/1), ui x, ui y (above the head), icon frame, opacity, good (0/1), text: banner i
// (1-based) for the UI menu PSTF_SAVE<i>, at the creature's drawn position (called every drawn frame).
// PSTF_SaveBanner() -> shown, made success, made failed, engine line (0 = the engine's "Save vs." line is off).
int __cdecl LuaSaveBanner(engine::LuaState* L) {
  const int i = static_cast<int>(engine::LuaNumberArg(L, 1, 0));
  if (i <= 0) {
    engine::LuaPushNumber(L, static_cast<double>(g.banners.size()));
    engine::LuaPushNumber(L, g.banners_good);
    engine::LuaPushNumber(L, g.banners_bad);
    engine::LuaPushNumber(L, g.save_silenced ? 0 : 1);
    Log("lua: PSTF_SaveBanner() -> shown %zu, success %d, failed %d, engine line %s", g.banners.size(), g.banners_good,
        g.banners_bad, g.save_silenced ? "off" : "on");
    return 4;
  }
  const uint32_t now = GetTickCount();
  const State::Banner* b = i <= static_cast<int>(g.banners.size()) ? &g.banners[i - 1] : nullptr;
  engine::SpriteInfo o;
  int ux = 0, uy = 0;
  bool on = b && now - b->t0 < kSaveLifeMs && engine::GetSprite(b->id, &o);
  if (on && b->slot >= 0) {  // its place in the combat text stack
    on = engine::WorldToUi(b->ax, b->ay, &ux, &uy);
    uy -= b->slot * kCtLinePx;  // slots stack upward from just above the head
  } else if (on) {
    int wx = o.x, wy = o.y;
    engine::InterpDrawPos(b->id, &wx, &wy);
    const int hgt = engine::SpriteTextHeight(b->id);
    on = engine::WorldToUi(wx, wy - (hgt > 0 ? hgt : 90) - kSaveAboveHeadPx, &ux, &uy);
  }
  engine::LuaPushNumber(L, on ? 1 : 0);
  engine::LuaPushNumber(L, ux);
  engine::LuaPushNumber(L, uy);
  engine::LuaPushNumber(L, on ? SaveIconFrame(now - b->t0) : 0);
  engine::LuaPushNumber(L, on ? SaveBannerAlpha(now - b->t0) : 0.0);
  engine::LuaPushNumber(L, on && b->good ? 1 : 0);
  engine::LuaPushString(L, on ? b->text.c_str() : "");
  return 7;
}

// PSTF_CombatText(i) -> on, ui x, ui y (the line's middle), opacity, strike share, kind, n, text1, colour1 .. text4,
// colour4, creature id, slot: combat text line i (1-based) for the UI menu PSTF_CT<i>, at its fixed world point (every drawn frame).
// PSTF_CombatText() -> lines, engine floaters seen, floaters taken over, lines added.
int __cdecl LuaCombatText(engine::LuaState* L) {
  const int i = static_cast<int>(engine::LuaNumberArg(L, 1, 0));
  if (i <= 0) {
    engine::LuaPushNumber(L, static_cast<double>(g.ct.Lines().size()));
    engine::LuaPushNumber(L, g.ct_floaters);
    engine::LuaPushNumber(L, g.ct_swallowed);
    engine::LuaPushNumber(L, g.ct_added);
    Log("lua: PSTF_CombatText() -> lines %zu, floaters %d, taken %d, added %d", g.ct.Lines().size(), g.ct_floaters,
        g.ct_swallowed, g.ct_added);
    return 4;
  }
  const uint32_t now = GetTickCount();
  const auto& ls = g.ct.Lines();
  const CtLine* l = i <= static_cast<int>(ls.size()) ? &ls[i - 1] : nullptr;
  bool on = l && now - l->t0 < kCtLifeMs && !l->segs.empty() && !l->segs[0].text.empty();
  int ux = 0, uy = 0;
  if (on) {
    engine::SpriteInfo o;
    const auto a = g.ct_area.find(l->id);
    on = engine::GetSprite(l->id, &o) && a != g.ct_area.end() && a->second == o.area && engine::WorldToUi(l->ax, l->ay, &ux, &uy);
    uy -= l->slot * kCtLinePx;  // slots stack upward from just above the head
  }
  engine::LuaPushNumber(L, on ? 1 : 0);
  engine::LuaPushNumber(L, ux);
  engine::LuaPushNumber(L, uy);
  engine::LuaPushNumber(L, on ? CtAlpha(now - l->t0) : 0.0);
  engine::LuaPushNumber(L, on && l->kind == CtKind::StatusOff ? CtStrike(now - l->t0) : 0.0);
  engine::LuaPushNumber(L, on ? static_cast<int>(l->kind) : 0);
  const int n = on ? static_cast<int>(l->segs.size()) : 0;
  engine::LuaPushNumber(L, n);
  for (int k = 0; k < kCtSegs; ++k) {
    engine::LuaPushString(L, k < n ? l->segs[k].text.c_str() : "");
    engine::LuaPushNumber(L, k < n ? l->segs[k].color : 0);
  }
  engine::LuaPushNumber(L, on ? l->id : -1);
  engine::LuaPushNumber(L, on ? l->slot : -1);
  return 9 + 2 * kCtSegs;
}

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

// PSTF_Cursor() -> the game's cursor mode (20: a hovering Missile of Patience waits for a target).
int __cdecl LuaCursor(engine::LuaState* L) {
  engine::LuaPushNumber(L, engine::GameCursorMode());
  return 1;
}
// PSTF_AreaLists(id) -> "front,back": how often the creature is in its area's front / back object lists (probe).
int __cdecl LuaAreaLists(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  int f = 0, b = 0;
  engine::AreaListCount(id, &f, &b);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%d,%d", f, b);
  Log("lua: PSTF_AreaLists(%X) -> %s", static_cast<unsigned>(id), buf);
  engine::LuaPushString(L, buf);
  return 1;
}
// PSTF_ProjectilesOf(caster) -> "id AIUpdate: stage ...;" for each of its projectiles (cinematic spell probe).
int __cdecl LuaProjectilesOf(engine::LuaState* L) {
  const auto c = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  std::string out;
  for (const auto& o : engine::AllObjects()) {
    if (o.source != c || o.type != 0) continue;
    char b[64];
    std::snprintf(b, sizeof b, "%X %llX: ", static_cast<unsigned>(o.id),
                  static_cast<unsigned long long>(engine::ProjectileUpdateFn(o.id)));
    out += b + engine::CinematicProbe(o.id) + ";";
  }
  Log("lua: PSTF_ProjectilesOf(%X) -> %s", static_cast<unsigned>(c), out.c_str());
  engine::LuaPushString(L, out.c_str());
  return 1;
}
// PSTF_MultiLeft(caster) -> missiles of Force Missiles / Elemental Strike / Ice Knife still to aim (test probe).
int __cdecl LuaMultiLeft(engine::LuaState* L) {
  const auto c = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  int left = 0, spell = 0;
  engine::MultiFireRaw(c, &left, &spell);
  engine::LuaPushNumber(L, engine::MultiFireLeft(c));
  engine::LuaPushNumber(L, left);
  engine::LuaPushNumber(L, spell);
  return 3;
}
// PSTF_MultiFire(caster, target[, spell]) -> true: one aimed missile, as the player's click on the target (test
// probe; spell = 2207/2405/2410 sets the readied spell number a bar cast would have).
int __cdecl LuaMultiFire(engine::LuaState* L) {
  const auto c = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1)), t = static_cast<int32_t>(engine::LuaNumberArg(L, 2, -1));
  const int before = engine::MultiFireLeft(c);
  const bool ok = engine::MultiFire(c, t, static_cast<int>(engine::LuaNumberArg(L, 3, 0)));
  Log("lua: PSTF_MultiFire(%X, %X) -> %d (left %d -> %d, cursor %d)", static_cast<unsigned>(c), static_cast<unsigned>(t),
      ok ? 1 : 0, before, engine::MultiFireLeft(c), engine::GameCursorMode());
  engine::LuaPushBool(L, ok);
  return 1;
}
// PSTF_MopFire(caster, target) -> true: a target click for the caster's hovering Missile of Patience (test probe).
int __cdecl LuaMopFire(engine::LuaState* L) {
  const auto c = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1)), t = static_cast<int32_t>(engine::LuaNumberArg(L, 2, -1));
  const bool ok = engine::MopClickTarget(c, t);
  Log("lua: PSTF_MopFire(%X, %X) -> %d (cursor %d)", static_cast<unsigned>(c), static_cast<unsigned>(t), ok ? 1 : 0, engine::GameCursorMode());
  engine::LuaPushBool(L, ok);
  return 1;
}
// PSTF_BarCount() -> entries in the engine's list behind the bar (after a SetState; more than 12 = it scrolls).
int __cdecl LuaBarCount(engine::LuaState* L) {
  engine::LuaPushNumber(L, engine::ActionBarListCount());
  return 1;
}
// PSTF_BarScroll(index) -> first, last slot (0-based) that hold list entries index.. after the engine's
// UpdateButtons refilled the bar there (a long list: slots 1..10, 2..10 in states 113/114); -1, -1 if not.
int __cdecl LuaBarScroll(engine::LuaState* L) {
  const int index = static_cast<int>(engine::LuaNumberArg(L, 1, 0));
  const int state = engine::ActionBarStateId();
  const bool ok = engine::ActionBarScroll(index);
  const int first = !ok ? -1 : (state == 113 || state == 114) ? 2 : 1;
  engine::LuaPushNumber(L, first);
  engine::LuaPushNumber(L, ok ? 10 : -1);
  return 2;
}

// PSTF_Maneuver(name) -> true if the current creature took it: 'run', 'withdraw', 'block', 'trip' (toggles),
// 'knockdown'. The hotbar's maneuver buttons call it.
int __cdecl LuaManeuver(engine::LuaState* L) {
  const std::string n = engine::LuaStringArg(L, 1);
  const AiMove m = n == "run" ? AiMove::Run : n == "withdraw" ? AiMove::Withdraw : n == "block" ? AiMove::Block
                   : n == "trip" ? AiMove::Trip : n == "knockdown" ? AiMove::Knockdown : AiMove::None;
  const bool ok = UseManeuver(g.current, m);
  Log("lua: PSTF_Maneuver(%s) -> %d", n.c_str(), ok ? 1 : 0);
  engine::LuaPushBool(L, ok);
  return 1;
}
// PSTF_Stance(id) -> "ran withdrawn stacks takedown prone reaction_used" (0/1, stacks, 0 none 1 trip 2 knockdown).
int __cdecl LuaStance(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, g.current));
  const auto it = g.stance.find(id);
  const Stance s = it == g.stance.end() ? Stance{} : it->second;
  char b[64];
  std::snprintf(b, sizeof b, "%d %d %d %d %d %d", s.ran, s.withdrawn, s.block_stacks, static_cast<int>(s.takedown), s.prone,
                s.reaction_used);
  Log("lua: PSTF_Stance(%X) -> %s", static_cast<unsigned>(id), b);
  engine::LuaPushString(L, b);
  return 1;
}
// PSTF_MoveStats() -> "runs withdraws blocks block_tries blocked takedowns takedown_hits prone aoo aoo_swung ai_moves stood
// short_runs" (short_runs: AI runs instead of stopping short of its target).
int __cdecl LuaMoveStats(engine::LuaState* L) {
  const auto& m = g.ms;
  char b[160];
  std::snprintf(b, sizeof b, "%d %d %d %d %d %d %d %d %d %d %d %d %d", m.runs, m.withdraws, m.blocks, m.block_tries, m.blocked,
                m.takedowns, m.takedown_hits, m.prone, m.aoo, m.aoo_swung, m.ai_moves, m.stood, m.short_runs);
  engine::LuaPushString(L, b);
  return 1;
}
// PSTF_AnimProbe(id) -> "run_flag cycle frame sequence" of a PST character animation (-1s otherwise).
int __cdecl LuaAnimProbe(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const std::string s = engine::AnimProbe(id);
  engine::LuaPushString(L, s.c_str());
  return 1;
}
// PSTF_FindActor(scriptName[, nth]) -> id of the nth (1-based) creature in the party's area with that script name
// (case-insensitive; CreateCreature gives a spawn its CRE's), living ones only, -1 none. Scenario tests find their actors with it.
int __cdecl LuaFindActor(engine::LuaState* L) {
  std::string want = engine::LuaStringArg(L, 1);
  const int nth = static_cast<int>(engine::LuaNumberArg(L, 2, 1));
  for (auto& c : want) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  engine::SpriteInfo me;
  const auto party = engine::PartyIds();
  const bool have = !party.empty() && engine::GetSprite(party[0], &me);
  int seen = 0;
  int32_t found = -1;
  for (const auto& s : engine::AllSprites()) {
    if ((have && s.area != me.area) || !s.alive) continue;  // a corpse keeps its script name
    std::string n = engine::ScriptName(s.id);
    for (auto& c : n) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (n == want && ++seen == nth) { found = s.id; break; }
  }
  Log("lua: PSTF_FindActor(%s, %d) -> %X", want.c_str(), nth, static_cast<unsigned>(found));
  engine::LuaPushNumber(L, found);
  return 1;
}
// PSTF_ActorRefs() -> "id,nameStrref;id,nameStrref;..." for every living creature in the party's area (Lua S.find
// matches Infinity_FetchString(strref) against a creature's shown name when no script name fits).
int __cdecl LuaActorRefs(engine::LuaState* L) {
  engine::SpriteInfo me;
  const auto party = engine::PartyIds();
  const bool have = !party.empty() && engine::GetSprite(party[0], &me);
  std::string t;
  for (const auto& s : engine::AllSprites()) {
    if ((have && s.area != me.area) || !s.alive) continue;
    t += std::to_string(s.id) + "," + std::to_string(s.name_ref) + ";";
  }
  engine::LuaPushString(L, t.c_str());
  return 1;
}
// PSTF_Actors([radius]) -> count; logs every creature in the party's area (within radius px of the protagonist, 0 = all):
// id, script name, EA, hp, position, action, fight side. For picking a scenario's actors in a new save.
int __cdecl LuaActors(engine::LuaState* L) {
  const double r = engine::LuaNumberArg(L, 1, 0);
  engine::SpriteInfo me;
  const auto party = engine::PartyIds();
  const bool have = !party.empty() && engine::GetSprite(party[0], &me);
  int n = 0;
  for (const auto& s : engine::AllSprites()) {
    if (have && s.area != me.area) continue;
    if (have && r > 0 && std::hypot(double(s.x - me.x), double(s.y - me.y)) > r) continue;
    const Combatant* c = g.dir ? g.dir->Find(s.id) : nullptr;
    Log("lua: actor %X %-12s ea %3u hp %4d at %5d,%5d action %-14s %s%s", static_cast<unsigned>(s.id),
        engine::ScriptName(s.id).c_str(), s.ea, s.hp, s.x, s.y, ActionName(s.action).c_str(), s.alive ? "" : "dead ",
        c ? (c->r.side == Side::Hostile ? "fight:hostile" : "fight:party") : "");
    ++n;
  }
  engine::LuaPushNumber(L, n);
  return 1;
}
// PSTF_InDialog() -> 1 while a dialogue runs (a party banter holds the world: scenario tests step it closed).
int __cdecl LuaInDialog(engine::LuaState* L) {
  engine::LuaPushNumber(L, engine::DialogActive() ? 1 : 0);
  return 1;
}
// PSTF_AnimId(id) -> the creature's animation id (a polymorph changes it: Vhailor, Soego), -1 none.
int __cdecl LuaAnimId(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const int a = engine::SpriteAnimId(id);
  if (engine::LuaNumberArg(L, 2, 0) == 0) Log("lua: PSTF_AnimId(%X) -> %d", static_cast<unsigned>(id), a);
  engine::LuaPushNumber(L, a);
  return 1;
}
// PSTF_Queue(id) -> "Spell, Wait, IncrementGlobal" (current action first; "" = none), queued count, state actions in it.
int __cdecl LuaQueue(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const std::vector<uint16_t> q = engine::ActionQueue(id);
  int lost = 0;
  const std::string t = q.empty() ? "" : QueueText(q.data(), static_cast<int>(q.size()), &lost);
  if (engine::LuaNumberArg(L, 2, 0) == 0) Log("lua: PSTF_Queue(%X) -> [%s] state %d", static_cast<unsigned>(id), t.c_str(), lost);
  engine::LuaPushString(L, t.c_str());
  engine::LuaPushNumber(L, q.empty() ? 0 : static_cast<double>(q.size() - 1));
  engine::LuaPushNumber(L, lost);
  return 3;
}
// PSTF_Timers(id) -> "1:12 3:5" (script timer id:seconds left; StartTimer/StartRandomTimer), count.
int __cdecl LuaTimers(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  std::string t;
  const auto timers = engine::ScriptTimers(id);
  for (const auto& tm : timers) t += (t.empty() ? "" : " ") + std::to_string(tm.first) + ":" + std::to_string(tm.second);
  if (engine::LuaNumberArg(L, 2, 0) == 0) Log("lua: PSTF_Timers(%X) -> %s", static_cast<unsigned>(id), t.c_str());
  engine::LuaPushString(L, t.c_str());
  engine::LuaPushNumber(L, static_cast<double>(timers.size()));
  return 2;
}
// PSTF_Action(id) -> current action id (ACTION.IDS), 1 when it has a current or queued action, 1 with a walk path
// (the log line adds SpellPST's cast-in-progress flag and counter; a second argument 1 = no log line).
int __cdecl LuaAction(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  engine::SpriteInfo o;
  const bool ok = engine::GetSprite(id, &o);
  int32_t cast = -1;
  int16_t counter = -1;
  engine::CastState(id, &cast, &counter);  // counter = the casting aura (-1: may cast)
  if (engine::LuaNumberArg(L, 2, 0) == 0)  // PSTF_Action(id, 1): quiet (a per-frame poll)
    Log("lua: PSTF_Action(%X) -> %d busy %d path %d cast %d counter %d", static_cast<unsigned>(id), ok ? o.action : -1, ok && o.busy,
        ok && o.has_path, cast, counter);
  engine::LuaPushNumber(L, ok ? o.action : -1);
  engine::LuaPushNumber(L, ok && o.busy ? 1 : 0);
  engine::LuaPushNumber(L, ok && o.has_path ? 1 : 0);
  return 3;
}
// PSTF_Casts(id) -> spells the engine really cast for it (its "casts <spell>" feedback; env PSTF_FEEDBACK_LOG=1, else -1).
int __cdecl LuaCasts(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  engine::LuaPushNumber(L, engine::SpellCasts(id));
  return 1;
}
// PSTF_Unapply(id, res) -> effects of spell `res` removed from it (engine UnapplySpell), then its attached spell
// objects end (-> also their count): the all-spells suite gives every spell a clean caster.
int __cdecl LuaUnapply(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const std::string res = engine::LuaStringArg(L, 2);
  const int n = engine::UnapplySpell(id, res.c_str());
  const int a = engine::EndAttached(id);
  const int o = engine::ExpireOrphans(id);
  Log("lua: PSTF_Unapply(%X, %s) -> %d effects, %d attached, %d parentless", static_cast<unsigned>(id), res.c_str(), n,
      a, o);
  engine::LuaPushNumber(L, n);
  engine::LuaPushNumber(L, a);
  return 2;
}
// PSTF_Attached(id) -> number of PST attached spell objects; logs ticks to each one's end.
int __cdecl LuaAttached(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  std::vector<int32_t> left;
  const int n = engine::AttachedLeft(id, &left);
  std::string t;
  for (int32_t v : left) t += " " + std::to_string(v);
  Log("lua: PSTF_Attached(%X) -> %d:%s", static_cast<unsigned>(id), n, t.c_str());
  engine::LuaPushNumber(L, n);
  engine::LuaPushNumber(L, left.empty() ? 0 : left[0]);
  return 2;
}
// PSTF_Pose(id) -> phase (0 standing, 1 falling, 2 down, 3 rising), frames back from the last die frame.
int __cdecl LuaPose(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const auto it = g.poses.find(id);
  engine::LuaPushNumber(L, it == g.poses.end() ? 0 : static_cast<int>(it->second.phase));
  engine::LuaPushNumber(L, it == g.poses.end() ? -1 : it->second.Back());
  return 2;
}
// PSTF_State(id[, quiet]) -> general state of its derived stats (STATE.IDS bits), -1 none.
int __cdecl LuaSpriteState(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const int64_t s = engine::SpriteState(id);
  if (engine::LuaNumberArg(L, 2, 0) == 0)  // 1 = quiet
    Log("lua: PSTF_State(%X) -> %llX", static_cast<unsigned>(id), static_cast<long long>(s));
  engine::LuaPushNumber(L, static_cast<double>(s));
  return 1;
}
// PSTF_Aoo() -> reactor id, mover id of the attack of opportunity in progress (-1, -1 none).
int __cdecl LuaAoo(engine::LuaState* L) {
  engine::LuaPushNumber(L, g.aoo.reactor);
  engine::LuaPushNumber(L, g.aoo.mover);
  return 2;
}
// PSTF_Class(id) -> CLASS.IDS, SPECIFIC.IDS of a creature (-1 unknown).
int __cdecl LuaClass(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  uint8_t c = 0, s = 0;
  const bool ok = engine::SpriteClass(id, &c, &s);
  Log("lua: PSTF_Class(%X) -> %d %d", static_cast<unsigned>(id), ok ? c : -1, ok ? s : -1);
  engine::LuaPushNumber(L, ok ? c : -1);
  engine::LuaPushNumber(L, ok ? s : -1);
  return 2;
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
// PSTF_CenterAt(x, y): centre the view on world point x,y (showcase recordings: a point to click on).
int __cdecl LuaCenterAt(engine::LuaState* L) {
  g.cam_t0 = 0;
  engine::CenterView(static_cast<int>(engine::LuaNumberArg(L, 1, 0)), static_cast<int>(engine::LuaNumberArg(L, 2, 0)));
  return 0;
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
  for (const auto& a : engine::AttachedObjects(id)) raw += "attached:" + std::to_string(a.left) + " ";
  const auto icons = StatusIconsOf(id, kStatusPortraitPx, kStatusIcons);
  std::string out;
  for (const auto& v : icons)
    out += v.parent + " " + v.bam + " " + std::to_string(v.name_ref) + " r" + std::to_string(v.rounds) + (v.debuff ? " debuff" : " buff") +
           " frame" + std::to_string(v.frame.size()) + ";";
  if (engine::LuaNumberArg(L, 2, 0) == 0)  // 1 = quiet
    Log("lua: PSTF_Status(%X) -> %zu: %s | effects %s", static_cast<unsigned>(id), icons.size(), out.c_str(), raw.c_str());
  engine::LuaPushNumber(L, static_cast<double>(icons.size()));
  engine::LuaPushString(L, out.c_str());  // "RES BAM strref rN buff|debuff frameK;" per icon
  return 2;
}

// PSTF_Stat(id, stat[, stat...]) -> one string "v1,v2,..." of the derived stats (STATS.IDS number: 2 AC, 7 THAC0,
// 9..13 saves, 32 LUCK, 36 STR, 42 CHR, ...) as the engine reads them (CDerivedStats::GetAtOffset), effects included;
// -32768 = no such creature. One string, not one value per stat: a C function may push only LUA_MINSTACK (20)
// values without lua_checkstack (27 corrupted the heap). Quiet (no log line): the spell suite reads it every frame.
int __cdecl LuaStat(engine::LuaState* L) {
  const auto id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  std::string out;
  for (int i = 2; i < 66; ++i) {
    const int st = static_cast<int>(engine::LuaNumberArg(L, i, 0));
    if (st <= 0) break;
    const int32_t v = engine::DerivedStat(id, st);
    if (!out.empty()) out += ',';
    out += std::to_string(v == INT32_MIN ? -32768 : v);
  }
  engine::LuaPushString(L, out.c_str());
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
  g.save_banner = IniBool(ini, "tbc.save_banner", true);
  g.sight = IniBool(ini, "tbc.combat_sight", true);
  g.sight_radius = IniInt(ini, "tbc.sight_radius", g.sight_radius);
  g.select_voice = IniBool(ini, "tbc.select_voice", true);
  g.no_bored = IniBool(ini, "tbc.no_bored", true);
  g.maneuvers = IniBool(ini, "tbc.maneuvers", true);
  g.opportunity = IniBool(ini, "tbc.opportunity_attacks", true);
  g.no_push = IniBool(ini, "tbc.no_push", true);
  g.keep_queue = IniBool(ini, "tbc.keep_queue", true);
  g.script_timers = IniBool(ini, "tbc.script_timers", true);
  g.stalemate_rounds = IniInt(ini, "tbc.stalemate_rounds", 3);
  g.mop_limit = IniInt(ini, "tbc.mop_shots", 3);
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
  for (const auto& s : sym::kData)
    if (s.rva == sym::kRvaSlotPendingTriggers && CheckSymbol(s)) {
      const uint64_t ppt = ReplacePointer(s, reinterpret_cast<void*>(&HookPendingTriggers));
      if (ppt == sym::kImageBase + sym::kRvaPendingTriggers) g_pending = reinterpret_cast<PendingFn>(ppt);
      else if (ppt) ReplacePointer(s, reinterpret_cast<void*>(ppt));
    }
  if (!g_pending) {
    Log("tbc: ProcessPendingTriggers slot differs from the manifest: held creatures' triggers run at once");
  }
  if (!AddTickHandler(&OnTick)) return false;
  // Presentation, so it belongs to the draw pass, not to world ticks (CameraStep). If that hook cannot be
  // installed, the camera still follows, just once per tick as before.
  g.cam_pre = g.camera ? engine::InstallWorldPreDraw(&CameraStep) : false;
  if (g.camera && !g.cam_pre) AddTickHandler(&CameraTick);
  if (g.round_effects && !engine::InstallEffectClock(&FrozenCombatant)) g.round_effects = false;
  if (g.round_effects) engine::SetOnsetRule(&OnsetKind), engine::SetOnsetSync(&OnsetSync);
  // A spell's projectile (a flying knife, a cloud) moves only in its caster's turn.
  if (g.round_effects) engine::SetProjectileGate(&ProjectileRuns);
  g.items = engine::InstallItemUseHooks();
  g.mop_hook = g.mop_limit > 0 && engine::InstallMopShotHook(&MopShot);
  {
    char* v = nullptr;
    size_t n = 0;
    const bool log = _dupenv_s(&v, &n, "PSTF_FEEDBACK_LOG") == 0 && v && v[0] == '1';
    free(v);
    // casts the economy cannot see (no casting time) still spend the action: count the engine's cast feedback
    g.cast_feedback = engine::InstallFeedbackHook(log);
  }
  if (IniBool(ini, "tbc.hud", true)) {
    if (engine::HudAvailable() && engine::InstallMenuHook()) {
      const bool bg3 = IniBool(ini, "tbc.bg3_layout", true);
      engine::AddMenuText(BuildHudMenu(bg3, g.party_max));
      if (bg3) engine::AddMenuTransform(&Bg3Transform);
      g.hud = true;
      g.draw = engine::InstallWorldDraw(&DrawOverlay);
      // maneuver ground effects under the creatures ([tbc] ground_fx=0: over them, engine lines)
      g.ground_fx = g.draw && IniBool(ini, "tbc.ground_fx", true) && engine::InstallWorldPreDraw(&FxPreDraw) &&
                    engine::InstallWorldFloor(&DrawGroundFx);
      g.rolls = engine::InstallRollHooks();
      if (g.save_banner && g.rolls) {
        engine::AddMenuText(BuildSaveBannerMenu());
        g.save_silenced = engine::SilenceSaveFeedback();
      } else {
        g.save_banner = false;
      }
      if (IniBool(ini, "tbc.combat_text", true) && engine::InstallFloaterHook(&OnFloater)) {
        g.ctext = true;
        engine::AddMenuText(BuildCombatTextMenu());
        Log("tbc: combat text on (BG3: still lines, numbers by damage type, statuses)");
      } else {
        g.ctext = false;
      }
      if (!g.draw) Log("tbc: world overlay not installed; no movement line");
    } else {
      Log("tbc: HUD functions differ from the manifest; turn-based HUD off (combat still on)");
    }
  }
  if (g.maneuvers) {
    // Trip, knockdown and block live in the engine's melee swing; knocked down = opcode 0 AC -4 (+4 to hit it).
    if (!g.rolls) g.rolls = engine::InstallRollHooks();
    if (g.rolls && engine::EffectsAvailable()) {
      engine::SetMeleeHitFilter(&MeleeThac0, &MeleeOutcome);
      // The trip swing's pace and the knocked-down poses live in every sprite's per-tick update.
      if (!engine::InstallSpriteUpdate(&SpriteUpdate)) Log("tbc: sprite update hook differs; trip pacing and prone poses off");
    } else {
      Log("tbc: maneuver hooks differ from the manifest (rolls %d, effects %d); maneuvers off", g.rolls ? 1 : 0,
          engine::EffectsAvailable() ? 1 : 0);
      g.maneuvers = false;
    }
  }
  if (g.no_push && !engine::InstallBumpGuard(&BumpGuard)) Log("tbc: bump calls differ from the manifest; pushing stays on");
  dev::AddLuaFunction("PSTF_Maneuver", &LuaManeuver);
  dev::AddLuaFunction("PSTF_AnimProbe", &LuaAnimProbe);
  dev::AddLuaFunction("PSTF_Action", &LuaAction);
  dev::AddLuaFunction("PSTF_Queue", &LuaQueue);
  dev::AddLuaFunction("PSTF_AnimId", &LuaAnimId);
  dev::AddLuaFunction("PSTF_InDialog", &LuaInDialog);
  dev::AddLuaFunction("PSTF_FindActor", &LuaFindActor);
  dev::AddLuaFunction("PSTF_Actors", &LuaActors);
  dev::AddLuaFunction("PSTF_ActorRefs", &LuaActorRefs);
  dev::AddLuaFunction("PSTF_Timers", &LuaTimers);
  dev::AddLuaFunction("PSTF_Casts", &LuaCasts);
  dev::AddLuaFunction("PSTF_Unapply", &LuaUnapply);
  dev::AddLuaFunction("PSTF_Attached", &LuaAttached);
  dev::AddLuaFunction("PSTF_Pose", &LuaPose);
  dev::AddLuaFunction("PSTF_Stance", &LuaStance);
  dev::AddLuaFunction("PSTF_Aoo", &LuaAoo);
  dev::AddLuaFunction("PSTF_State", &LuaSpriteState);
  dev::AddLuaFunction("PSTF_MoveStats", &LuaMoveStats);
  dev::AddLuaFunction("PSTF_Class", &LuaClass);
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
  dev::AddLuaFunction("PSTF_Stat", &LuaStat);
  dev::AddLuaFunction("PSTF_Bar", &LuaBar);
  dev::AddLuaFunction("PSTF_SaveBanner", &LuaSaveBanner);
  dev::AddLuaFunction("PSTF_CombatText", &LuaCombatText);
  dev::AddLuaFunction("PSTF_Near", &LuaNear);
  dev::AddLuaFunction("PSTF_BarSlots", &LuaBarSlots);
  dev::AddLuaFunction("PSTF_BarCount", &LuaBarCount);
  dev::AddLuaFunction("PSTF_Cursor", &LuaCursor);
  dev::AddLuaFunction("PSTF_MopFire", &LuaMopFire);
  dev::AddLuaFunction("PSTF_MultiLeft", &LuaMultiLeft);
  dev::AddLuaFunction("PSTF_ProjectilesOf", &LuaProjectilesOf);
  dev::AddLuaFunction("PSTF_AreaLists", &LuaAreaLists);
  dev::AddLuaFunction("PSTF_MultiFire", &LuaMultiFire);
  dev::AddLuaFunction("PSTF_BarScroll", &LuaBarScroll);
  dev::AddLuaFunction("PSTF_BarHex", &LuaBarHex);
  dev::AddLuaFunction("PSTF_TbcCurrent", &LuaTbcCurrent);
  dev::AddLuaFunction("PSTF_MakeEnemy", &LuaMakeEnemy);
  dev::AddLuaFunction("PSTF_GroundFx", &LuaGroundFx);
  dev::AddLuaFunction("PSTF_WorldTicks", &LuaWorldTicks);
  dev::AddLuaFunction("PSTF_SetName", &LuaSetName);
  dev::AddLuaFunction("PSTF_EvalAs", &LuaEvalAs);
  dev::AddLuaFunction("PSTF_FightId", &LuaFightId);
  dev::AddLuaFunction("PSTF_Side", &LuaSide);
  dev::AddLuaFunction("PSTF_SetHp", &LuaSetHp);
  dev::AddLuaFunction("PSTF_DiceAt", &LuaDiceAt);
  dev::AddLuaFunction("PSTF_HoverAt", &LuaHoverAt);
  dev::AddLuaFunction("PSTF_RollProbe", &LuaRollProbe);
  dev::AddLuaFunction("PSTF_CenterOn", &LuaCenterOn);
  dev::AddLuaFunction("PSTF_CenterAt", &LuaCenterAt);
  dev::AddLuaFunction("PSTF_IconSize", &LuaIconSize);
  dev::AddLuaFunction("PSTF_Status", &LuaStatus);
  dev::AddLuaFunction("PSTF_TbcAttacks", &LuaTbcAttacks);
  dev::AddLuaFunction("PSTF_SearchByte", &LuaSearchByte);
  dev::AddLuaFunction("PSTF_PreviewPts", &LuaPreviewPts);
  dev::AddLuaFunction("PSTF_EnginePath", &LuaEnginePath);
  Log("tbc: installed (ProcessAI slot, 2 walk calls, tick); radii %d/%d/%d, end turn key %X%s, "
      "move %.0f ft, hud %s, camera %s, maneuvers %s",
      g.radii.start, g.radii.join, g.radii.leave, g.end_turn_vk, g.auto_party ? ", auto party" : "",
      g.move_feet, g.hud ? "on" : "off",
      !g.camera ? "off" : (!g.cam_pre ? "tick handler" : (g.cam_smooth ? "eased in draw pass" : "tick rate")),
      !g.maneuvers ? "off" : g.opportunity ? "on, attacks of opportunity" : "on");
  return true;
}

}  // namespace pstf::tbc
