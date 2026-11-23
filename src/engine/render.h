// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: draw in the world pass, with the engine's own primitives (symbols.h "movement line").
// The callback runs on the game thread right after CGameArea::Render, inside the zoom-scaled pass, so
// world-space lines land under the UI and scale with zoom like sprites do.
#pragma once
#include <cstdint>

namespace pstf::engine {

using WorldDrawFn = void (*)();
bool InstallWorldDraw(WorldDrawFn fn);

// Presentation hook that runs inside the draw pass BEFORE CGameArea::Render, so what it changes (the
// camera) lands on the frame being drawn. Game thread only, guarded like the overlay.
bool InstallWorldPreDraw(WorldDrawFn fn);

// The CGameArea being drawn: valid from the pre-draw hooks through the post-draw callback (0 outside).
uint64_t WorldPassArea();
// Draw space -> area: area = draw + off (off = view - rect.left; CGameSprite::RenderActions 1403DF4F3).
void WorldPassOffset(float* ox, float* oy);

// Only valid inside the WorldDrawFn callback. World coordinates (pixels). color = 0xBBGGRR.
// half_width in pixels; the engine draws a strip with bevelled ends (alpha at least 0xAA).
void DrawWorldLine(int x1, int y1, int x2, int y2, uint32_t color, float half_width);

}  // namespace pstf::engine
