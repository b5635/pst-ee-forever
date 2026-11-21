// SPDX-License-Identifier: GPL-2.0-or-later
// Pure helpers for code patching (no Windows calls; unit-tested).
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace pstf {

// Target of an E8/E9 rel32 instruction at `site` whose 5 bytes are `insn`.
std::optional<uint64_t> Rel32Target(uint64_t site, const uint8_t insn[5]);

// rel32 for a 5-byte call/jmp at `site` reaching `target`; nullopt if out of range.
std::optional<int32_t> Rel32For(uint64_t site, uint64_t target);

// 14-byte absolute jump: FF 25 00000000 <imm64>.
constexpr size_t kAbsJmpLen = 14;
void WriteAbsJmp(uint8_t out[kAbsJmpLen], uint64_t target);

bool BytesEqual(const uint8_t* a, const uint8_t* b, size_t n);

}  // namespace pstf
