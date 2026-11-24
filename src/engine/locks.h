// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter for lockpicking and bashing (symbols.h "Lock rolls"). Game thread only.
//
// The two action calls in CGameSprite::ExecuteAction (PickLock, BashDoor) go through a callback first. It
// sees the attempt (who, what, the lock, whether the engine would roll on this call) and answers:
//   Vanilla  the engine's own action (walks, rolls its d10, opens or fails),
//   Wait     the action stays (ACTION_NORMAL) without running: the actor stands at the lock,
//   Cancel   the action ends (ACTION_DONE) without a roll,
//   Succeed / Fail  the engine's action runs with its random number replaced, so it opens or fails the
//            lock with its own triggers, sounds, feedback, XP and multiplayer messages.
#pragma once
#include <cstdint>
#include <string>

namespace pstf::engine {

enum class LockAct { Pick, Bash };
enum class LockStep { Vanilla, Wait, Cancel, Succeed, Fail };

struct LockTry {
  LockAct act = LockAct::Pick;
  int32_t actor = -1, target = -1;
  bool door = false;
  bool locked = false;
  int difficulty = 0;     // 0..100 (100: only a key or a script opens it)
  bool in_range = false;  // the engine would roll on this call (else it walks first)
  int lockpicking = 0, luck = 0, str = 0, str_extra = 0;  // the actor's live stats
};

using LockFn = LockStep (*)(const LockTry&);
bool InstallLockHooks(LockFn fn);

// The engine's own work point and range test, pure (for the unit tests).
struct Pt {
  int x = 0, y = 0;
};
// CGameDoor::GetMoveDest: the nearer of the door's two places to stand, distance dx^2 + 16*dy^2/9.
inline Pt NearerStand(Pt from, Pt a, Pt b) {
  const auto d = [&](Pt p) { return (from.x - p.x) * (from.x - p.x) + 16 * (from.y - p.y) * (from.y - p.y) / 9; };
  return d(a) < d(b) ? a : b;
}
// Squared distance in search-map cells (16 x 12 px), each coordinate divided on its own as the engine does.
inline int CellDist2(Pt a, Pt b) {
  const int dx = a.x / 16 - b.x / 16, dy = a.y / 12 - b.y / 12;
  return dx * dx + dy * dy;
}

// Probes (tests): "id:door|box:locked:difficulty:x,y:name;..." for every door and container in the actor's area
// within `radius` px of it; SetLock writes the lock state of door/container `id` (flags bit and difficulty).
std::string LockObjects(int32_t actor, int radius);
bool SetLock(int32_t id, bool locked, int difficulty);

}  // namespace pstf::engine
