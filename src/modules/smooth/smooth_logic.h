// SPDX-License-Identifier: GPL-2.0-or-later
// Pure sub-tick interpolation math (unit-tested, no engine access). In spirit of GemRB's
// TickInterpolation (D:\gemrb\gemrb\core\Interpolation.h/.cpp, GPL-2.0-or-later): the world advances
// in fixed ticks, frames are drawn as often as the display allows, and each mover is drawn sliding along
// its last observed step. Logic never sees any of this.
//
// Unlike GemRB, Torment:EE does not move a walker on every tick: CGameSprite::AIUpdateWalk lands a step
// every other world tick (trace of a walk at fps 30: +5 px, +0, +5, +0, ... — smooth_lock.lua). Sliding
// over one tick would slide for one tick and stand still for the next (15 Hz judder). So each mover
// slides over the interval its steps actually arrive at (its cadence), measured per mover in ticks.
// Cost: movement is displayed one step interval behind the state (2 ticks for a walker).
#pragma once
#include <cmath>
#include <cstdint>

namespace pstf::smooth {

// Tick pacing by accumulated real time (ms, fractional). Short waits overshoot, so comparing against the
// last tick drifts the tick rate below the engine's own and with it every tick-counted thing (walk speed,
// effect durations, songs, day/night). Accumulating elapsed time and spending tick_ms per world update
// keeps the average rate exact; a long stall is capped so loading does not burst dozens of ticks.
// Alpha() is the leftover time as a fraction of a tick: how far the display is into the current tick.
struct TickPacer {
  double last_real = 0.0;
  double budget = 0.0;
  bool started = false;

  bool Due(double now_ms, double tick_ms) {
    if (tick_ms <= 0.0) return true;
    if (!started) {
      started = true;
      last_real = now_ms;
    } else if (now_ms > last_real) {
      budget += now_ms - last_real;
      last_real = now_ms;
    }
    const double cap = 4.0 * tick_ms;
    if (budget > cap) budget = cap;
    if (budget >= tick_ms) {
      budget -= tick_ms;
      return true;
    }
    return false;
  }

  float Alpha(double tick_ms) const {
    if (tick_ms <= 0.0) return 0.0f;
    const double a = budget / tick_ms;
    return a < 0.0 ? 0.0f : (a > 1.0 ? 1.0f : float(a));
  }
};

// Ticks a mover's step may take to arrive and still count as one continuous motion. A longer gap is a
// mover that stood still and starts again: it slides over its previous cadence instead.
constexpr uint32_t kMaxSpan = 4;

// What one drawn object remembers: the step it is sliding along. a = where the slide started (what was
// shown when the step arrived), b = the latest observed position, observed on tick tb; the slide takes
// span ticks. Serials are world-update counters; 0 = never observed.
struct Track {
  int32_t id = -1;
  float ax = 0.0f, ay = 0.0f;
  float bx = 0.0f, by = 0.0f;
  uint32_t tb = 0;    // tick the latest step was observed on
  uint32_t seen = 0;  // last tick this object was observed on
  uint32_t span = 1;  // ticks the current slide takes (the mover's cadence)

  void Reset(int32_t obj, float x, float y, uint32_t serial) {
    id = obj;
    ax = bx = x;
    ay = by = y;
    tb = seen = serial;
    span = 1;
  }

  // Shown position at tick time (serial + alpha): a -> b over span ticks from tb, then b.
  void At(uint32_t serial, float alpha, float* x, float* y) const {
    float t = (float(serial - tb) + alpha) / float(span ? span : 1u);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    *x = ax + (bx - ax) * t;
    *y = ay + (by - ay) * t;
  }

  // Called once per world update (serial = that update) with the object's exact position after it.
  // A fresh object, one missing on the previous tick, or a jump past snap (teleport, area change)
  // shows at its real position at once.
  void Observe(int32_t obj, float x, float y, uint32_t serial, float snap) {
    if (obj != id || seen == 0 || serial != seen + 1) {
      Reset(obj, x, y, serial);
      return;
    }
    seen = serial;
    if (x == bx && y == by) return;
    const float dx = x - bx, dy = y - by;
    if (dx > snap || dx < -snap || dy > snap || dy < -snap) {
      Reset(obj, x, y, serial);
      return;
    }
    const uint32_t gap = serial - tb;
    // The slide starts where the display is now, so a step arriving early or late never jumps.
    At(serial, 0.0f, &ax, &ay);
    bx = x;
    by = y;
    if (gap >= 1 && gap <= kMaxSpan) span = gap;
    tb = serial;
  }

  bool Current(int32_t obj, uint32_t serial) const { return obj == id && seen != 0 && seen == serial; }
};

// The whole pixel a shown position lands on. The engine's own integer position is the floor of its
// exact position (AIUpdateWalk: acc >> 10), so a mover at rest is shown exactly where the engine has it.
inline int Pixel(float v) { return int(std::floor(v)); }

}  // namespace pstf::smooth
