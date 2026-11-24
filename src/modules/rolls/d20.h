// SPDX-License-Identifier: GPL-2.0-or-later
// The BG3-style d20 check shared by every roll panel (dialogue ability rolls, lockpicking, bashing): the
// rule and the panel's timeline, pure (no engine access).
//   success = d20 + modifier >= DC; a natural 20 always succeeds, a natural 1 always fails.
#pragma once
#include <cstdint>

namespace pstf::rolls {

// floor((ability - 10) / 2), the D&D ability modifier.
inline int Modifier(int ability) {
  const int x = ability - 10;
  return x >= 0 ? x / 2 : -((-x + 1) / 2);  // floor
}

enum class Outcome { Fail, Success, CritFail, CritSuccess };
inline Outcome Resolve(int d20, int modifier, int dc) {
  if (d20 >= 20) return Outcome::CritSuccess;
  if (d20 <= 1) return Outcome::CritFail;
  return d20 + modifier >= dc ? Outcome::Success : Outcome::Fail;
}
inline bool Succeeded(Outcome o) { return o == Outcome::Success || o == Outcome::CritSuccess; }
inline const char* OutcomeName(Outcome o) {
  return o == Outcome::CritSuccess ? "critical success"
         : o == Outcome::Success   ? "success"
         : o == Outcome::CritFail  ? "critical failure"
                                   : "failure";
}

// Chance in percent that d20 + modifier >= dc (natural 1 and 20 included): for tables and the docs.
inline int ChancePercent(int modifier, int dc) {
  int wins = 0;
  for (int d = 1; d <= 20; ++d) wins += Succeeded(Resolve(d, modifier, dc)) ? 1 : 0;
  return wins * 5;
}

// The roll panel's timeline (ms after the Roll click), taken from the BG3 reference clip and the two
// result sounds (check_success.wav / check_failed.wav start at the throw and share the first 5 s):
// the die leaves at once, lands at ~1.85 s (the sound's landing hit at 2.0 s), shows its natural
// number, the bonus lights up at 3.0 s and flies into the die, the die shows the total at 4.3 s (the
// sound's bonus cue at 4.4 s), the bonus row fades at 5.0 s, the die pops at the result hit (success
// 5.3 s, failure 5.15 s), the banner follows (success 6.25 s, failure 7.0 s, the failure sound's big hit)
// and Continue appears 0.4 s after it.
struct RollTiming {
  static constexpr uint32_t kLand = 1850, kNatural = 2000, kBonusLit = 3000, kBonusFly = 3400, kTotal = 4300,
                            kBonusFade = 5000, kPopOk = 5250, kPopFail = 5100, kBannerOk = 6250,
                            kBannerFail = 7000, kContinueAfter = 400, kPop = 360;
  static uint32_t Pop(bool ok) { return ok ? kPopOk : kPopFail; }
  static uint32_t Banner(bool ok) { return ok ? kBannerOk : kBannerFail; }
  static uint32_t Continue(bool ok) { return Banner(ok) + kContinueAfter; }
};

// PFD20.BAM frame for a resting die showing n (clamped to -9..40) at pop scale s (0..2).
inline int RestFrame(int n, int s) {
  if (n < -9) n = -9;
  if (n > 40) n = 40;
  if (s < 0) s = 0;
  if (s > 2) s = 2;
  return 24 + s * 50 + (n + 9);
}

// PFDCARD.BAM ability cards (STR DEX CON INT WIS CHA).
constexpr int kCardStr = 0, kCardDex = 1, kCardCon = 2, kCardInt = 3, kCardWis = 4, kCardChr = 5;

}  // namespace pstf::rolls
