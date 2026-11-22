// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: sub-tick frame interpolation (symbols.h "frame interpolation"). Game thread only.
// The world update keeps running at EE's own tick rate (1000/fps ms); the loop runs at the display rate,
// and the iterations between ticks draw only — with every mover shifted to where it is shown (sliding
// along its last step over its own cadence, smooth_logic.h) and the lock camera following that.
// Nothing here changes what logic, scripts or saves can observe: the engine's positions stay intact;
// a draw hook shifts pos for the duration of one engine draw call and puts the old values back.
#pragma once
#include <cstdint>

namespace pstf::engine {

// Installs the loop and draw hooks. frame_fps = how often the loop may iterate (the display rate).
// False when any manifest byte differs: then nothing is patched and the game stays exactly vanilla.
// frame_fps 0 = the display's refresh rate. lock_follow: the engine's lock camera follows the shown
// position (else it keeps stepping once per tick).
bool InstallFrameInterpolation(uint32_t frame_fps, bool lock_follow);

// Where a creature is SHOWN on this frame: its interpolated draw position while interpolation is on, the
// engine's integer position when it is off. Game thread only; reads nothing but this creature.
bool InterpDrawPos(int32_t id, int* x, int* y);

// Trace probe: log one row per loop iteration for creature id, for the next frames iterations.
// The result once the trace is complete: frames drawn, frames of the walk (first to last frame the shown
// position changed), frames of the walk it stood still, the largest wobble against the view (px), and the
// longest hold (ms between two frames that moved it).
void InterpTrace(int32_t id, int frames);
struct TraceResult {
  bool done = false;
  int frames = 0, walk = 0, still = 0, wobble = 0;
  int hold_ms = 0;  // longest time the shown position stood still inside the walk
};
TraceResult InterpTraceResult();
// Test probe: the world screen's scroll lock (the HUD's lock-camera button) on creature id (-1 = off).
bool LockCamera(int32_t id);

// Probes (safe off the game thread? no: game thread only, read-only counters).
bool InterpActive();
bool InterpTickFrame();      // the current loop iteration ran the world update (false = draw only)
uint64_t InterpFrames();     // loop iterations since install
uint64_t InterpTicks();      // world updates since install
uint64_t InterpLockFrames(); // frames the lock camera followed the shown position
uint32_t InterpTickMs();     // EE's tick interval, 1000/fps
float InterpAlpha();         // fraction of the current tick elapsed, [0, 1]
int InterpEngineFps();       // EE's fps global

}  // namespace pstf::engine
