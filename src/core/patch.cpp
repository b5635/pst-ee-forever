// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/patch.h"

#include <windows.h>

#include <cstring>

#include "core/log.h"
#include "core/patch_math.h"

namespace pstf {
namespace {

// One page of trampolines within +-2 GB of the exe (rel32 reach).
uint8_t* g_tramp_page = nullptr;
size_t g_tramp_used = 0;

uint8_t* AllocTrampoline(size_t n = 16) {
  n = (n + 15) & ~static_cast<size_t>(15);
  if (!g_tramp_page) {
    // Search downward from just below the image base in 64 KB steps (allocation granularity).
    for (uint64_t a = sym::kImageBase - 0x10000; a > sym::kImageBase - 0x70000000ull; a -= 0x10000) {
      g_tramp_page = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(a), 0x1000,
                                                        MEM_RESERVE | MEM_COMMIT,
                                                        PAGE_EXECUTE_READWRITE));
      if (g_tramp_page) break;
    }
    if (!g_tramp_page) return nullptr;
  }
  if (g_tramp_used + n > 0x1000) return nullptr;
  uint8_t* t = g_tramp_page + g_tramp_used;
  g_tramp_used += n;
  return t;
}

bool WriteCode(void* dst, const void* src, size_t n) {
  DWORD old;
  if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(dst, src, n);
  VirtualProtect(dst, n, old, &old);
  FlushInstructionCache(GetCurrentProcess(), dst, n);
  return true;
}

}  // namespace

uint8_t* AllocCode(size_t n) { return AllocTrampoline(n); }

bool PatchCode(const sym::Symbol& s, const uint8_t* bytes, size_t n) {
  if (n > s.len || !CheckSymbol(s)) return false;
  return WriteCode(Va(s.rva), bytes, n);
}

uint64_t InlineHook(const sym::Symbol& prologue, size_t copy_len, void* detour) {
  if (copy_len < 5 || copy_len > prologue.len || !CheckSymbol(prologue)) return 0;
  uint8_t* p = Va(prologue.rva);
  uint8_t* tramp = AllocTrampoline(copy_len + kAbsJmpLen);
  uint8_t* stub = AllocTrampoline(kAbsJmpLen);
  if (!tramp || !stub) {
    Log("patch: %s: no room for an inline hook", prologue.name);
    return 0;
  }
  std::memcpy(tramp, p, copy_len);
  WriteAbsJmp(tramp + copy_len, reinterpret_cast<uint64_t>(p + copy_len));
  WriteAbsJmp(stub, reinterpret_cast<uint64_t>(detour));
  const auto rel = Rel32For(reinterpret_cast<uint64_t>(p), reinterpret_cast<uint64_t>(stub));
  if (!rel) return 0;
  uint8_t insn[32];
  std::memset(insn, 0x90, sizeof insn);
  insn[0] = 0xE9;
  std::memcpy(insn + 1, &*rel, 4);
  if (!WriteCode(p, insn, copy_len)) return 0;
  return reinterpret_cast<uint64_t>(tramp);
}

bool CheckSymbol(const sym::Symbol& s) {
  if (BytesEqual(Va(s.rva), s.bytes, s.len)) return true;
  Log("manifest: %s at %llX: bytes differ from the manifest (%s)", s.name,
      static_cast<unsigned long long>(sym::kImageBase + s.rva), s.evidence);
  return false;
}

uint64_t RedirectCall(const sym::Symbol& site, void* detour) {
  if (site.len != 5 || !CheckSymbol(site)) return 0;
  uint8_t* p = Va(site.rva);
  const uint64_t va = reinterpret_cast<uint64_t>(p);
  auto original = Rel32Target(va, p);
  uint8_t* tramp = AllocTrampoline();
  if (!original || !tramp) {
    Log("patch: %s: no trampoline", site.name);
    return 0;
  }
  WriteAbsJmp(tramp, reinterpret_cast<uint64_t>(detour));
  auto rel = Rel32For(va, reinterpret_cast<uint64_t>(tramp));
  if (!rel) return 0;
  uint8_t insn[5] = {0xE8};
  std::memcpy(insn + 1, &*rel, 4);
  if (!WriteCode(p, insn, 5)) return 0;
  return *original;
}

uint64_t ReplacePointer(const sym::Symbol& slot, void* repl) {
  if (slot.len != 8 || !CheckSymbol(slot)) return 0;
  uint64_t old = 0;
  std::memcpy(&old, Va(slot.rva), 8);
  const uint64_t v = reinterpret_cast<uint64_t>(repl);
  if (!WriteCode(Va(slot.rva), &v, 8)) return 0;
  return old;
}

}  // namespace pstf
