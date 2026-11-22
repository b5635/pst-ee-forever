// SPDX-License-Identifier: GPL-2.0-or-later
// Pure sub-tick interpolation math (unit-tested, no engine access). Ported in spirit from GemRB's
// TickInterpolation (D:\gemrb\gemrb\core\Interpolation.h/.cpp, GPL-2.0-or-later): the world advances
// in fixed ticks, frames are drawn as often as the display allows, and each mover is drawn between
// where it was at the start of the current tick and where it is now. Logic never sees any of this.
// Cost: movement is displayed one tick behind the state.
#pragma once
#include <cstdint>

namespace pstf::smooth {

// Rounds away from zero (positions can be negative).
inline int Round(float v) { return v >= 0.0f ? int(v + 0.5f) : int(v - 0.5f); }

// The tick clock: stamped once per world update, read once per drawn frame.
struct TickClock {
  uint32_t serial = 0;     // tick counter; 0 means "never captured"
  uint64_t last_tick = 0;  // ms of the tick that opened the current interval
  uint32_t tick_ms = 33;   // 1000 / engine fps
  bool enabled = false;

  void BeginTick(uint64_t now_ms, uint32_t interval_ms) {
    ++serial;
    if (serial == 0) ++serial;  // wraparound: 0 stays reserved for "never captured"
    last_tick = now_ms;
    tick_ms = interval_ms ? interval_ms : 1u;
  }

  // Fraction of the current tick that has passed, [0, 1].
  float Alpha(uint64_t now_ms) const {
    if (!enabled || now_ms <= last_tick) return 0.0f;
    const float a = float(now_ms - last_tick) / float(tick_ms);
    return a > 1.0f ? 1.0f : a;
  }
};

// Tick pacing by accumulated real time, not by comparing against the last tick. Short waits overshoot
// (Sleep wakes a little late), so an instant comparison drifts the tick rate below the engine's own and
// with it every tick-counted thing: walk speed, effect durations, songs, the day/night cycle. Accumulating
// elapsed time and spending tick_ms per world update keeps the average rate exact; a long stall is capped
// so loading does not burst dozens of ticks.
struct TickBudget {
  uint64_t last_real = 0;
  uint64_t budget = 0;
  bool started = false;

  bool Due(uint64_t now_ms, uint32_t tick_ms) {
    if (!started) {
      started = true;
      last_real = now_ms;
    } else if (now_ms > last_real) {
      budget += now_ms - last_real;
      last_real = now_ms;
    }
    const uint64_t cap = 4ull * (tick_ms ? tick_ms : 1u);
    if (budget > cap) budget = cap;
    if (tick_ms == 0) return true;
    if (budget >= tick_ms) {
      budget -= tick_ms;
      return true;
    }
    return false;
  }
};

// What one drawn object remembers between ticks.
struct DrawPos {
  float prev_x = 0.0f;
  float prev_y = 0.0f;
  uint32_t serial = 0;

  void Capture(float x, float y, uint32_t tick_serial) {
    prev_x = x;
    prev_y = y;
    serial = tick_serial;
  }
};

struct DrawShift {
  int dx = 0;
  int dy = 0;
};

// Integer shift from the position the engine draws at (draw_x, draw_y) to where the object is shown
// this frame: round(prev + (cur - prev) * alpha). Zero when there is nothing to do: not captured on
// the current tick (a fresh object or a new area), not moving, or a jump past snap (teleport, area
// change). cur is the exact position (including a carried fraction), so a mover whose drawn integer
// position has not changed still interpolates.
inline DrawShift InterpOffset(const DrawPos& p, float cur_x, float cur_y, int draw_x, int draw_y,
                              uint32_t tick_serial, float alpha, float snap) {
  DrawShift out;
  if (tick_serial == 0 || p.serial != tick_serial) return out;
  const float dx = cur_x - p.prev_x;
  const float dy = cur_y - p.prev_y;
  if (dx == 0.0f && dy == 0.0f) return out;
  if (dx > snap || dx < -snap || dy > snap || dy < -snap) return out;
  out.dx = Round(p.prev_x + dx * alpha) - draw_x;
  out.dy = Round(p.prev_y + dy * alpha) - draw_y;
  return out;
}

}  // namespace pstf::smooth
