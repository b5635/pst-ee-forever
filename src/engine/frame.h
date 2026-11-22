// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: sub-tick frame interpolation (symbols.h "frame interpolation"). Game thread only.
// The world update keeps running at EE's own tick rate (1000/fps ms); the loop runs faster, and the
// iterations between ticks draw only — with every mover shifted to its interpolated position.
// Nothing here changes what logic, scripts or saves can observe: the engine's positions stay intact;
// a draw hook shifts pos for the duration of one engine draw call and puts the old values back.
#pragma once
#include <cstdint>

namespace pstf::engine {

// Installs the loop and draw hooks. frame_fps = how often the loop may iterate (the display rate).
// False when any manifest byte differs: then nothing is patched and the game stays exactly vanilla.
bool InstallFrameInterpolation(uint32_t frame_fps);

// Probes (safe off the game thread? no: game thread only, read-only counters).
bool InterpActive();
bool InterpTickFrame();      // the current loop iteration ran the world update (false = draw only)
uint64_t InterpFrames();     // loop iterations since install
uint64_t InterpTicks();      // world updates since install
uint32_t InterpTickMs();     // EE's tick interval, 1000/fps
float InterpAlpha();         // fraction of the current tick elapsed, [0, 1]
int InterpEngineFps();       // EE's fps global

}  // namespace pstf::engine
