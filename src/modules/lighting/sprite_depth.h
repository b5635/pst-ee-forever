// SPDX-License-Identifier: GPL-2.0-or-later
// Sprite depth files (SPD1, tools/sprite_depth/FORMAT.md): per character BAM frame, the height of the
// visible surface above the billboard plane toward the camera, in screen px at 1x. Pure logic.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pstf::lighting {

struct DepFrame {
  uint16_t w = 0, h = 0;
  int16_t cx = 0, cy = 0;  // BAM frame centre
  float scale = 0;         // px of height per sample unit
  uint32_t offset = 0, size = 0;
};

struct DepFile {
  bool u16 = false;
  std::vector<DepFrame> frames;
  std::vector<uint16_t> cycle_count, cycle_first;
  std::vector<uint16_t> lookup;
  std::vector<uint8_t> bytes;  // the whole file (zlib streams are read from it on demand)
  // BAM frame entry of (cycle, frame index inside the cycle) as CVidCell keeps them; -1 if out of range.
  int FrameIndex(int cycle, int frame) const;
  // Heights of a frame entry in px, w*h, rows top-down. False on a bad stream; an empty stream (size 0)
  // gives all zeros.
  bool Heights(int index, std::vector<float>* out) const;
};

// Parses the header, frame table, cycle table and lookup; false on any inconsistency.
bool ParseDep(std::vector<uint8_t> bytes, DepFile* out);

// Normal of a sprite pixel from its height field (x right, y toward the camera, z up: the lighting
// model's frame, docs/VISUALS_PLAN.md 2): the billboard faces the camera (0, 1, 0) and the height's
// gradient tilts it; `strength` scales the tilt; mirrored draws flip x. Central differences, edges clamped.
// Mirrored by the sprite info shader.
void SpriteNormal(const std::vector<float>& h, int w, int hgt, int x, int y, bool mirror, float strength,
                  float n[3]);

}  // namespace pstf::lighting
