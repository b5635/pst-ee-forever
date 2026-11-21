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

uint8_t* AllocTrampoline() {
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
  if (g_tramp_used + 16 > 0x1000) return nullptr;
  uint8_t* t = g_tramp_page + g_tramp_used;
  g_tramp_used += 16;
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

}  // namespace pstf
