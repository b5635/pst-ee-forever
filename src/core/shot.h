// SPDX-License-Identifier: GPL-2.0-or-later
// Screenshot probe: reads the game's own OpenGL frame (glReadPixels, game thread only, inside the
// frame tick where the engine's GL context is current) and writes a half-size 24-bit BMP.
#pragma once
#include <string>

namespace pstf {

// Read-framebuffer binding found by the last SaveScreenshot (0 = default, as expected).
extern int g_last_read_fbo;

// Returns "" on success, else the reason.
std::string SaveScreenshot(const std::string& path);

// The game window's client width in screen pixels (the window of the current GL context), 0 if unknown.
// UI coordinates are smaller with Scale UI: UI px = screen px * Infinity_GetScreenSize() width / this.
int GameClientWidth();

}  // namespace pstf
