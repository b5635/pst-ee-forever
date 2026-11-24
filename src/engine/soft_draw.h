// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: soft, translucent strips in area pixels, drawn with our own GL program (the engine's
// DrawWorldLine draws at alpha 0xAA at least and above every sprite). Call from a world callback: at the
// floor draw point (gl_world.h InstallWorldFloor: under every creature, lit by the lighting pass like the
// ground) or later in the pass. Game thread only.
#pragma once
#include <cstdint>

namespace pstf::engine {

struct SoftSeg {
  float x1, y1, x2, y2;  // area px
  uint32_t bgr;          // 0x00BBGGRR
  float alpha;           // 0..1 at the strip's centre line; the edges fade to 0
  float half_width;      // area px
};

// Draws n strips (normal alpha blend, or additive with `add`). False when GL is unavailable.
bool DrawSoftSegments(const SoftSeg* s, int n, bool add = false);

}  // namespace pstf::engine
