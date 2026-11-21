// SPDX-License-Identifier: GPL-2.0-or-later
// Screenshot probe: reads the game's own OpenGL frame (glReadPixels, game thread only, inside the
// frame tick where the engine's GL context is current) and writes a half-size 24-bit BMP.
#pragma once
#include <string>

namespace pstf {

// Returns "" on success, else the reason.
std::string SaveScreenshot(const std::string& path);

}  // namespace pstf
