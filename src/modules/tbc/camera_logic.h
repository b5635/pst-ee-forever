// SPDX-License-Identifier: GPL-2.0-or-later
// Pure camera-follow math for [tbc] camera_follow (tbc_module.cpp CameraStep). No engine access, so it
// is testable without the game (tests/unit/test_logic.cpp).
#pragma once
#include <cmath>
#include <cstdint>

namespace pstf::tbc {

// The view centre following a creature, in world points. While the camera is off the creature it eases
// toward it with a real-time constant (f = 1 - exp(-dt/tau), GemRB's SmoothCamera): the same gap closes
// in the same wall-clock time at any frame rate. The position is kept in floating point: truncating each
// step to whole pixels stalls the camera on short steps (a 1.3 px gap times f = 0.2 is 0 px) and makes it
// lurch on the next frame. Once within lock_px of the creature it locks on: from then on the view centre
// IS the creature's shown position, so the creature stands still on screen while it walks and the world
// glides under it (any lag at all shows as the creature wobbling against the view).
struct CameraEase {
  double x = 0.0, y = 0.0;  // current view centre
  bool locked = false;

  void Reset(int cx, int cy) {
    x = cx;
    y = cy;
    locked = false;
  }

  // Move dt_ms of real time toward (tx, ty).
  void Step(int tx, int ty, double dt_ms, double tau_ms, double lock_px) {
    if (!locked) {
      if (dt_ms <= 0.0) return;
      const double f = tau_ms > 0.0 ? 1.0 - std::exp(-dt_ms / tau_ms) : 1.0;
      x += (tx - x) * f;
      y += (ty - y) * f;
      if (std::fabs(tx - x) > lock_px || std::fabs(ty - y) > lock_px) return;
      locked = true;
    }
    x = tx;
    y = ty;
  }

  int X() const { return static_cast<int>(std::floor(x + 0.5)); }
  int Y() const { return static_cast<int>(std::floor(y + 0.5)); }

  // A target this far off the centre is shown at once: no easing across half an area.
  bool Distant(int tx, int ty, int reach) const {
    const double dx = tx - x, dy = ty - y;
    return dx * dx + dy * dy > static_cast<double>(reach) * static_cast<double>(reach);
  }
};

}  // namespace pstf::tbc
