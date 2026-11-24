// SPDX-License-Identifier: GPL-2.0-or-later
// Turn-based effect timing rules (pure, unit-tested).
#pragma once
#include <cstdint>
#include <map>

namespace pstf::tbc {

constexpr int32_t kRoundTicks5s = 75;  // 5 game seconds x 15 ticks: one combat round of effect time

// A delayed effect (duration types 6/7/8) seen for the first time with `left` ticks to its onset:
//   - on a 5-second step (75, 150, ...: PST's own damage-over-time series, e.g. Adder's Kiss' later
//     hits) it is round-clocked: it lands at the start of the victim's turn that many rounds on;
//   - otherwise, if it is less than a round and a half away (Cone of Cold's 100-tick onset, a
//     projectile's impact), it is the spell resolving: it runs on world time now and the caster's turn
//     waits for it.
inline bool OnsetResolvesNow(int32_t left) {
  // A few ticks away is now (Adder's Kiss' op374 casting feature sits 3 ticks out: the old
  // "within 3 of a 75-tick step" test read it as step 0 and deferred the spell a round).
  if (left <= 3) return true;
  if (left >= kRoundTicks5s + kRoundTicks5s / 2) return false;
  const int32_t m = left % kRoundTicks5s;
  const bool on_step = m <= 3 || m >= kRoundTicks5s - 3;  // a tick or two may pass before we see it
  return !on_step;
}

// PST:EE opcodes that only show or play something (glows, colour pulses, sounds, BAM/VVC plays): a
// delayed one of these is presentation and plays on world time, never deferred to a later turn
// (Aid's sparkle SP21SAID from the Whispering Flask is op369 with a 5-second delay).
inline bool IsPresentationOpcode(int op) {
  switch (op) {
    case 9: case 50: case 51: case 52: case 61: case 140: case 141: case 174: case 215:
    case 369: case 370: case 371: case 372: case 373:
      return true;
    default:
      return false;
  }
}

// combat.h OnsetFn: 0 = round-clocked, 1 = the spell resolving now, 2 = presentation (world time).
inline int OnsetKind(int32_t left, int opcode) {
  if (IsPresentationOpcode(opcode)) return 2;
  return OnsetResolvesNow(left) ? 1 : 0;
}

// The casting aura: after a cast starts, CGameSprite::DoAction counts the caster's AI ticks (lin 4A26A0 l.39-52,
// win 1404079D0: word +0x46A8) and clears it (-1) at ROUND_LENGTH unless a cast still runs (+0x46B0); SpellPST
// refuses a new spell while the counter is >= 0. In a fight the caster's AI runs on its own turns only, so the
// round clock steps the counter by the round that passed between its turns, as it steps effects.
inline int16_t StepCastAura(int16_t counter, uint32_t ticks, uint32_t round_length, bool casting) {
  if (counter < 0) return counter;  // -1: aura clean (-2.. unused)
  const uint32_t c = static_cast<uint32_t>(counter) + ticks;
  if (c >= round_length && !casting) return -1;
  return static_cast<int16_t>(c > 0x7FFF ? 0x7FFF : c);
}

// Lingering spells (area clouds, Swarm Curse) pulse on a tick countdown once they have landed. Each
// gets one round of ticks per round of its caster: kFirst from landing (the first pulse belongs to the
// round it was cast in), then kRound from the start of each of the caster's turns. Swarm Curse pulses
// every 75 ticks of its counter, so every 75-tick window holds exactly one pulse (120 gave two).
struct PulseBudget {
  static constexpr int32_t kFirst = kRoundTicks5s, kRound = kRoundTicks5s;  // any 75 ticks hold exactly one swarm pulse
  std::map<int32_t, int32_t> left;  // projectile id -> AIUpdate ticks it may still run this round
  bool Run(int32_t proj) {
    auto it = left.try_emplace(proj, kFirst).first;
    if (it->second <= 0) return false;
    --it->second;
    return true;
  }
  bool Spent(int32_t proj) const {
    const auto it = left.find(proj);
    return it != left.end() && it->second <= 0;
  }
  void NewRound(int32_t proj) { left[proj] = kRound; }
  void Clear() { left.clear(); }
};

}  // namespace pstf::tbc
