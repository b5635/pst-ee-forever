// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

#include "engine/symbols.h"

namespace pstf {

inline uint8_t* Va(uint32_t rva) { return reinterpret_cast<uint8_t*>(sym::kImageBase + rva); }

// True if the in-memory bytes at the symbol match the manifest (logs a mismatch).
bool CheckSymbol(const sym::Symbol& s);

// Redirects the 5-byte `call rel32` at `site_rva` to `detour` through a near trampoline.
// Verifies the manifest bytes first. Returns the original call target, or 0 on failure.
uint64_t RedirectCall(const sym::Symbol& site, void* detour);

// Replaces the 8-byte pointer at `slot` (e.g. a vtable entry) after checking the manifest bytes.
// Returns the old pointer, or 0 on failure.
uint64_t ReplacePointer(const sym::Symbol& slot, void* repl);

// Executable memory within +-2 GB of the exe (rel32 reach), for code stubs. nullptr when full.
uint8_t* AllocCode(size_t n);

// Writes `n` bytes at the symbol's address after checking its manifest bytes (n <= s.len).
bool PatchCode(const sym::Symbol& s, const uint8_t* bytes, size_t n);

// Hooks a function's prologue: the first `copy_len` bytes of the manifest entry (whole
// instructions, no RIP-relative operands) move to a trampoline that jumps back; the prologue
// becomes `jmp rel32` to a near stub that jumps to `detour`. Returns the trampoline (call it to run
// the original), or 0 on failure.
uint64_t InlineHook(const sym::Symbol& prologue, size_t copy_len, void* detour);

}  // namespace pstf
