// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: draw in the world pass, with the engine's own primitives (symbols.h "movement line").
// The callback runs on the game thread right after CGameArea::Render, inside the zoom-scaled pass, so
// world-space lines land under the UI and scale with zoom like sprites do.
#pragma once
#include <cstdint>

namespace pstf::engine {

using WorldDrawFn = void (*)();
bool InstallWorldDraw(WorldDrawFn fn);

// Only valid inside the WorldDrawFn callback. World coordinates (pixels). color = 0xBBGGRR.
// half_width in pixels; the engine draws a strip with bevelled ends (alpha at least 0xAA).
void DrawWorldLine(int x1, int y1, int x2, int y2, uint32_t color, float half_width);

}  // namespace pstf::engine
