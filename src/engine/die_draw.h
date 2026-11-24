// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter: draws the roll panel's d20 (modules/rolls/die3d.h) with our own GL program, in the
// middle of the engine's UI pass. Called from the die item's `frame lua` (drawItem evaluates it while it
// draws that item), so everything the menu drew before it is flushed under the die and every item after
// it (sparks, the flying bonus, the banner) lands on top, as with the BAM. Game thread only.
#pragma once
#include <cstdint>
#include <string>

#include "modules/rolls/die3d.h"

namespace pstf::engine {

// The number atlas (tools/make_dice_bams.py d20_numbers.bin); read on the first draw.
void SetDieAtlasPath(const std::wstring& path);

// Draws `fr` centred at (cx, cy) in UI coordinates with the BAM's 150 px box `box` wide (radius 52/150
// of it, times fr.scale). ui_w x ui_h = Infinity_GetScreenSize (the UI's size in the current viewport).
// False (nothing drawn) when GL, the shaders or the atlas are unavailable: the caller shows the BAM.
bool DrawDie(const rolls::die3d::Frame& fr, float cx, float cy, float box, float ui_w, float ui_h);

// Draw calls that drew / failed since start (probe).
void DieDrawStats(int* drawn, int* failed, std::string* why);
// Probe: the next draw reads back the pixel at the die's centre (after the die, before later items).
void ArmDieProbe();
bool DieProbePixel(uint8_t rgb[3]);  // false until an armed draw happened

}  // namespace pstf::engine
