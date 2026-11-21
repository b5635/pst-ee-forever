// SPDX-License-Identifier: GPL-2.0-or-later
// Dev autoload: pure state machine (unit-tested). Start screen stable -> "Continue" -> world screen
// stable -> run the test Lua file once. Fed once per frame with the active screen.
#pragma once
#include <cstdint>

namespace pstf::dev {

enum class Scr : uint8_t { None, Start, Load, World, Other };
enum class Act : uint8_t { None, Continue, RunScript, Fail };

struct Autoload {
  // Frames, not ms: the tick rate is the frame rate (Baldur.lua "Maximum Frame Rate", 10..90).
  static constexpr uint32_t kStartStable = 30;   // start menu fully up (and UI Lua loaded)
  static constexpr uint32_t kWorldStable = 60;   // area loaded, first AI updates done
  static constexpr uint32_t kTimeout = 90 * 120; // 2 min at the highest frame rate

  enum class Phase : uint8_t { WaitStart, Loading, Settle, Done, Failed } phase = Phase::WaitStart;
  uint32_t frames = 0;
  uint32_t stable = 0;

  Act Step(Scr s) {
    if (phase == Phase::Done || phase == Phase::Failed) return Act::None;
    if (++frames > kTimeout) {
      phase = Phase::Failed;
      return Act::Fail;
    }
    switch (phase) {
      case Phase::WaitStart:
        stable = s == Scr::Start ? stable + 1 : 0;
        if (stable < kStartStable) return Act::None;
        phase = Phase::Loading;
        stable = 0;
        return Act::Continue;
      case Phase::Loading:
      case Phase::Settle:
        if (s != Scr::World) {
          stable = 0;
          return Act::None;
        }
        phase = Phase::Settle;
        if (++stable < kWorldStable) return Act::None;
        phase = Phase::Done;
        return Act::RunScript;
      default:
        return Act::None;
    }
  }
  void ContinueFailed() { phase = Phase::Failed; }
};

}  // namespace pstf::dev
