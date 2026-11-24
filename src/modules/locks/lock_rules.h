// SPDX-License-Identifier: GPL-2.0-or-later
// Lock rolls: lockpicking and bashing as BG3-style d20 checks (rules pure, no engine access).
//
// The engine's own test (CGameSprite::PickLock / BashDoor): skill (+ STR's bend bars for a bash) + a d10
// shifted by Luck must beat the lock's difficulty (0..99); 100 never opens (key or script only). Here:
//   Lockpicking  DC = 10 + difficulty / 5     modifier = LOCKPICKING / 5 + LUCK
//   Bashing      DC = 12 + difficulty / 4     modifier = floor((STR - 10) / 2) + exceptional STR + LUCK
// (exceptional: 18/01-50 +1, 18/51-99 +2, 18/00 +3). Natural 20 always opens, natural 1 always fails.
// Picking is the easier way in: its DC climbs a third slower and a trained thief's bonus outgrows any
// strength. The shipped locks are 2..60 (containers) and 0..20 (doors; most doors are 100 = key only):
// Annah (45 lockpicking: +9) opens a 40 lock (DC 18) on a 9+ (60 %), Vhailor (18/51: +6) bashes it (DC 22) on a
// 16+ (25 %), the Nameless One untrained (0 / STR 12: +0 / +1) on 18+ (15 %) / 21 (5 %, natural 20).
// A failed attempt: that character cannot try that lock again before `retry_hours` of game time pass
// (resting does it); 0 = try again at once.
#pragma once
#include "modules/rolls/d20.h"

namespace pstf::locks {

constexpr int kKeyOnly = 100;  // the engine never opens it with a roll
constexpr int kTicksPerHour = 4500;  // CTimerWorld TIMESCALE_MIN_PER_HOUR 5 * SEC_PER_MIN 60 * MSEC_PER_SEC 15

// A lock the panel offers: locked and not key-only.
inline bool Rollable(bool locked, int difficulty) { return locked && difficulty >= 0 && difficulty < kKeyOnly; }

inline int PickDc(int difficulty) { return 10 + difficulty / 5; }
inline int PickModifier(int lockpicking, int luck) { return (lockpicking > 0 ? lockpicking : 0) / 5 + luck; }

inline int ExceptionalStr(int str, int str_extra) {
  if (str != 18 || str_extra <= 0) return 0;
  return str_extra >= 100 ? 3 : str_extra > 50 ? 2 : 1;
}
inline int BashDc(int difficulty) { return 12 + difficulty / 4; }
inline int BashModifier(int str, int str_extra, int luck) {
  return rolls::Modifier(str) + ExceptionalStr(str, str_extra) + luck;
}

// A failure at world tick `failed_at` still blocks a new try at `now` (unsigned world ticks).
inline bool RetryBlocked(uint32_t failed_at, uint32_t now, int retry_hours) {
  if (retry_hours <= 0) return false;
  return now - failed_at < static_cast<uint32_t>(retry_hours) * kTicksPerHour;
}
// Whole game hours left before a retry (rounded up, at least 1 while blocked).
inline int RetryHoursLeft(uint32_t failed_at, uint32_t now, int retry_hours) {
  if (!RetryBlocked(failed_at, now, retry_hours)) return 0;
  const uint32_t left = static_cast<uint32_t>(retry_hours) * kTicksPerHour - (now - failed_at);
  return static_cast<int>((left + kTicksPerHour - 1) / kTicksPerHour);
}

}  // namespace pstf::locks
