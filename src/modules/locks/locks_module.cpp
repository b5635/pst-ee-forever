// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/locks/locks_module.h"

#include <cstdio>
#include <map>
#include <string>
#include <tuple>

#include "core/log.h"
#include "core/tick.h"
#include "engine/locks.h"
#include "engine/lua.h"
#include "engine/menu.h"
#include "engine/sprites.h"
#include "engine/world.h"
#include "modules/dev/lua_bridge.h"
#include "modules/locks/lock_rules.h"
#include "modules/rolls/roll_panel.h"

namespace pstf::locks {
namespace {

using engine::LockAct;
using engine::LockStep;
using engine::LockTry;

enum class State { Opening, Open, Rolled, Resolved, Cancelled };

struct Attempt {
  LockAct act = LockAct::Pick;
  int32_t actor = -1, target = -1;
  bool door = false;
  int difficulty = 0, dc = 0, mod = 0;
  int blocked_hours = 0;  // > 0: a failure blocks this try; the panel only says so
  State state = State::Opening;
  rolls::D20 roll;
};

using Key = std::tuple<int, int32_t, int32_t>;  // act, actor, target

struct G {
  bool on = false, sounds = true;
  int retry_hours = 1;
  bool have = false;
  Attempt cur;
  std::map<Key, uint32_t> fails;  // world tick of the last failure
} g;

const char* ActName(LockAct a) { return a == LockAct::Pick ? "pick" : "bash"; }
const char* StateName(State s) {
  switch (s) {
    case State::Opening: return "opening";
    case State::Open: return "open";
    case State::Rolled: return "rolled";
    case State::Resolved: return "resolved";
    case State::Cancelled: return "cancelled";
  }
  return "?";
}

uint32_t Now() {
  uint32_t t = 0;
  engine::WorldTime(&t, nullptr);
  return t;
}

bool InParty(int32_t id) {
  for (int32_t p : engine::PartyIds())
    if (p == id) return true;
  return false;
}

bool Same(const Attempt& a, const LockTry& t) { return a.act == t.act && a.actor == t.actor && a.target == t.target; }

LockStep OnLock(const LockTry& t) {
  if (!g.on) return LockStep::Vanilla;
  {
    // one line per approach (the engine calls every AI tick while the actor walks up)
    static Key last{-1, -1, -1};
    static bool last_range = false;
    const Key k{static_cast<int>(t.act), t.actor, t.target};
    if (k != last || t.in_range != last_range)
      Log("locks: %s of %s %d by %d: %s, %s, difficulty %d", ActName(t.act), t.door ? "door" : "container", t.target,
          t.actor, t.in_range ? "at the lock" : "walking up", t.locked ? "locked" : "not locked", t.difficulty);
    last = k;
    last_range = t.in_range;
  }
  if (!t.in_range || !Rollable(t.locked, t.difficulty) || !InParty(t.actor)) return LockStep::Vanilla;
  if (g.have) {
    if (!Same(g.cur, t)) return LockStep::Wait;  // one panel at a time: this one waits its turn
    switch (g.cur.state) {
      case State::Opening:
      case State::Open:
      case State::Rolled:
        return LockStep::Wait;
      case State::Cancelled:
        g.have = false;
        Log("locks: %s of %d by %d cancelled", ActName(t.act), t.target, t.actor);
        return LockStep::Cancel;
      case State::Resolved: {
        g.have = false;
        const bool ok = rolls::Succeeded(g.cur.roll.outcome);
        const Key k{static_cast<int>(t.act), t.actor, t.target};
        if (ok)
          g.fails.erase(k);
        else
          g.fails[k] = Now();
        Log("locks: %s of %d by %d handed to the engine: %s", ActName(t.act), t.target, t.actor, ok ? "open" : "fail");
        return ok ? LockStep::Succeed : LockStep::Fail;
      }
    }
  }
  Attempt a;
  a.act = t.act;
  a.actor = t.actor;
  a.target = t.target;
  a.door = t.door;
  a.difficulty = t.difficulty;
  if (t.act == LockAct::Pick) {
    a.dc = PickDc(t.difficulty);
    a.mod = PickModifier(t.lockpicking, t.luck);
  } else {
    a.dc = BashDc(t.difficulty);
    a.mod = BashModifier(t.str, t.str_extra, t.luck);
  }
  const auto f = g.fails.find(Key{static_cast<int>(t.act), t.actor, t.target});
  if (f != g.fails.end()) a.blocked_hours = RetryHoursLeft(f->second, Now(), g.retry_hours);
  g.cur = a;
  g.have = true;
  Log("locks: %s %s %d (difficulty %d) by %d: DC %d, modifier %+d (lockpicking %d, STR %d/%d, luck %d)%s",
      ActName(t.act), t.door ? "door" : "container", t.target, t.difficulty, t.actor, a.dc, a.mod, t.lockpicking, t.str,
      t.str_extra, t.luck, a.blocked_hours ? ", blocked by a failure" : "");
  return LockStep::Wait;
}

// --- Lua ------------------------------------------------------------------------------------------------
const char* kLua = R"LUA(
function pstfLockOpen(c)
	c.roll = function() return PSTF_LockRoll() end
	c.done = function() PSTF_LockDone() end
	c.back = function() PSTF_LockBack() end
	c.alive = function() return PSTF_LockAlive() end
	c.pause = true
	if not pstfDrOpen(c) then PSTF_LockBack() end
end
)LUA";

bool Live() {
  engine::SpriteInfo info;
  return g.have && engine::GetSprite(g.cur.actor, &info);
}

// PSTF_LockRoll() -> natural, total, outcome (0 fail, 1 success, 2 critical fail, 3 critical success); -1 when
// nothing can be rolled.
int __cdecl LuaRoll(engine::LuaState* L) {
  if (!g.have || g.cur.state != State::Open || g.cur.blocked_hours) {
    engine::LuaPushNumber(L, -1);
    return 1;
  }
  g.cur.roll = rolls::RollD20(g.cur.mod, g.cur.dc);
  g.cur.state = State::Rolled;
  Log("locks: roll %s %d: d20 %d %+d = %d vs DC %d -> %s", ActName(g.cur.act), g.cur.target, g.cur.roll.natural,
      g.cur.mod, g.cur.roll.total, g.cur.dc, rolls::OutcomeName(g.cur.roll.outcome));
  if (g.sounds) rolls::PlayRollSound(rolls::Succeeded(g.cur.roll.outcome), "locks");
  engine::LuaPushNumber(L, g.cur.roll.natural);
  engine::LuaPushNumber(L, g.cur.roll.total);
  engine::LuaPushNumber(L, static_cast<int>(g.cur.roll.outcome));
  return 3;
}

// PSTF_LockDone(): Continue after a roll; the next engine call hands the result over.
int __cdecl LuaDone(engine::LuaState*) {
  if (g.have) g.cur.state = g.cur.state == State::Rolled ? State::Resolved : State::Cancelled;
  return 0;
}

// PSTF_LockBack(): Back before a roll (or no panel): the action ends without a try.
int __cdecl LuaBack(engine::LuaState*) {
  if (g.have && g.cur.state != State::Rolled) g.cur.state = State::Cancelled;
  return 0;
}

int __cdecl LuaAlive(engine::LuaState* L) {
  engine::LuaPushBool(L, Live());
  return 1;
}

// Probe: PSTF_LockState() -> "state=open act=pick actor=.. target=.. difficulty=.. dc=.. mod=.. blocked=.. nat=..
// total=.. outcome=.. fails=N" ("state=none fails=N" without an attempt).
int __cdecl LuaState(engine::LuaState* L) {
  char b[256];
  if (g.have)
    std::snprintf(b, sizeof b, "state=%s act=%s actor=%d target=%d difficulty=%d dc=%d mod=%d blocked=%d nat=%d total=%d outcome=%d fails=%zu",
                  StateName(g.cur.state), ActName(g.cur.act), g.cur.actor, g.cur.target, g.cur.difficulty, g.cur.dc,
                  g.cur.mod, g.cur.blocked_hours, g.cur.roll.natural, g.cur.roll.total,
                  static_cast<int>(g.cur.roll.outcome), g.fails.size());
  else
    std::snprintf(b, sizeof b, "state=none fails=%zu", g.fails.size());
  static std::string last;
  if (last != b) Log("lua: PSTF_LockState -> %s", b);  // tests poll it every frame: log changes only
  last = b;
  engine::LuaPushString(L, b);
  return 1;
}

// Probe: PSTF_Locks(actor, radius px) -> "id:door|box:locked:difficulty:x,y:name;..." near the actor.
int __cdecl LuaLocks(engine::LuaState* L) {
  const std::string s = engine::LockObjects(static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1)),
                                            static_cast<int>(engine::LuaNumberArg(L, 2, 400)));
  Log("lua: PSTF_Locks -> %s", s.c_str());
  engine::LuaPushString(L, s.c_str());
  return 1;
}

// Test probe: PSTF_SetLock(id, locked 0|1, difficulty) -> true when written.
int __cdecl LuaSetLock(engine::LuaState* L) {
  const int32_t id = static_cast<int32_t>(engine::LuaNumberArg(L, 1, -1));
  const bool locked = engine::LuaNumberArg(L, 2, 1) != 0;
  const int diff = static_cast<int>(engine::LuaNumberArg(L, 3, 0));
  const bool ok = engine::SetLock(id, locked, diff);
  Log("lua: PSTF_SetLock(%d, %d, %d) -> %s", id, locked ? 1 : 0, diff, ok ? "ok" : "no such door/container");
  engine::LuaPushBool(L, ok);
  return 1;
}

// Test probe: PSTF_LockForget(): no failure blocks a retry.
int __cdecl LuaForget(engine::LuaState*) {
  g.fails.clear();
  return 0;
}

void Tick(uint64_t) {
  if (!g.have) return;
  if (g.cur.state == State::Opening) {
    const bool pick = g.cur.act == LockAct::Pick;
    std::string note = "nil";
    if (g.cur.blocked_hours) {
      char n[128];
      std::snprintf(n, sizeof n, "You failed this lock. Try again in %d hour%s (or after resting).", g.cur.blocked_hours,
                    g.cur.blocked_hours == 1 ? "" : "s");
      note = engine::LuaQuote(n);
    }
    char c[384];
    std::snprintf(c, sizeof c, "pstfLockOpen{ title = %s, ab = %s, card = %d, mod = %d, dc = %d, note = %s }",
                  engine::LuaQuote(pick ? "Lockpicking Check" : (g.cur.door ? "Bash the Door" : "Bash the Lock")).c_str(),
                  engine::LuaQuote(pick ? "Lockpicking" : "Strength").c_str(), pick ? rolls::kCardDex : rolls::kCardStr,
                  g.cur.mod, g.cur.dc, note.c_str());
    g.cur.state = State::Open;
    const std::string err = engine::LuaDoString(c);
    if (!err.empty()) {
      Log("locks: panel failed: %s", err.c_str());
      g.cur.state = State::Cancelled;
    }
    return;
  }
  if (g.cur.state == State::Open || g.cur.state == State::Rolled) {
    // The panel went away without Continue/Back (closed by something else): do not leave the actor waiting.
    if (!Live()) {
      g.have = false;
      return;
    }
    engine::LuaDoString("if not pstfDr.open then PSTF_LockBack() end");
  }
}

}  // namespace

void InstallLockRolls(const IniMap& ini) {
  if (!IniBool(ini, "locks.rolls", false)) {
    Log("locks: lock rolls disabled in ini");
    return;
  }
  g.sounds = IniBool(ini, "locks.roll_sounds", true);
  g.retry_hours = IniInt(ini, "locks.retry_hours", 1);
  if (!rolls::InstallRollPanel()) {
    Log("locks: no roll panel; lock rolls off");
    return;
  }
  if (!engine::WorldApiAvailable() || !engine::InstallLockHooks(&OnLock)) {
    Log("locks: lock functions differ from the manifest; lock rolls off");
    return;
  }
  engine::AddMenuText("`\n" + std::string(kLua) + "\n`\n");
  dev::AddLuaFunction("PSTF_LockRoll", &LuaRoll);
  dev::AddLuaFunction("PSTF_LockDone", &LuaDone);
  dev::AddLuaFunction("PSTF_LockBack", &LuaBack);
  dev::AddLuaFunction("PSTF_LockAlive", &LuaAlive);
  dev::AddLuaFunction("PSTF_LockState", &LuaState);
  dev::AddLuaFunction("PSTF_Locks", &LuaLocks);
  dev::AddLuaFunction("PSTF_SetLock", &LuaSetLock);
  dev::AddLuaFunction("PSTF_LockForget", &LuaForget);
  AddTickHandler(&Tick);
  g.on = true;
  Log("locks: installed (sounds %s, retry after %d game hour(s))", g.sounds ? "on" : "off", g.retry_hours);
}

}  // namespace pstf::locks
