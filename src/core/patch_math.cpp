// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/patch_math.h"

#include <cstring>

namespace pstf {

std::optional<uint64_t> Rel32Target(uint64_t site, const uint8_t insn[5]) {
  if (insn[0] != 0xE8 && insn[0] != 0xE9) return std::nullopt;
  int32_t rel;
  std::memcpy(&rel, insn + 1, 4);
  return site + 5 + static_cast<int64_t>(rel);
}

std::optional<int32_t> Rel32For(uint64_t site, uint64_t target) {
  const int64_t d = static_cast<int64_t>(target - (site + 5));
  if (d < INT32_MIN || d > INT32_MAX) return std::nullopt;
  return static_cast<int32_t>(d);
}

void WriteAbsJmp(uint8_t out[kAbsJmpLen], uint64_t target) {
  out[0] = 0xFF;
  out[1] = 0x25;
  out[2] = out[3] = out[4] = out[5] = 0;
  std::memcpy(out + 6, &target, 8);
}

bool BytesEqual(const uint8_t* a, const uint8_t* b, size_t n) { return std::memcmp(a, b, n) == 0; }

}  // namespace pstf
