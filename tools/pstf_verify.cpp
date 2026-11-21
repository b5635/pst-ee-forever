// SPDX-License-Identifier: GPL-2.0-or-later
// L0 static hook verification: every manifest symbol against the on-disk Torment.exe.
// Usage: pstf_verify <path to Torment.exe>. Prints only failures and a summary.
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/patch_math.h"
#include "engine/symbols.h"

namespace pstf {
std::string Sha256File(const wchar_t* path);
}

using namespace pstf;

namespace {

struct Image {
  std::vector<uint8_t> d;
  const IMAGE_NT_HEADERS64* nt = nullptr;

  const uint8_t* At(uint32_t rva, size_t n, bool* in_text = nullptr) const {
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
      if (rva >= sec->VirtualAddress && rva + n <= sec->VirtualAddress + sec->Misc.VirtualSize) {
        if (in_text) *in_text = (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        size_t off = sec->PointerToRawData + (rva - sec->VirtualAddress);
        return off + n <= d.size() ? d.data() + off : nullptr;
      }
    }
    return nullptr;
  }
};

int g_fail = 0;
void Fail(const char* what, const char* name) {
  ++g_fail;
  std::printf("FAIL %s: %s\n", name, what);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::printf("usage: pstf_verify <Torment.exe>\n");
    return 2;
  }
  std::ifstream f(argv[1], std::ios::binary);
  Image img;
  img.d.assign(std::istreambuf_iterator<char>(f), {});
  if (img.d.size() < 0x1000) {
    std::printf("FAIL cannot read exe\n");
    return 2;
  }
  if (Sha256File(argv[1]) != sym::kExeSha256) Fail("sha256 is not GOG 3.2.2.0", "exe");
  img.nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
      img.d.data() + reinterpret_cast<const IMAGE_DOS_HEADER*>(img.d.data())->e_lfanew);
  if (img.nt->OptionalHeader.ImageBase != sym::kImageBase) Fail("image base", "exe");
  if (img.nt->OptionalHeader.AddressOfEntryPoint != sym::kEntryRva) Fail("entry rva", "exe");
  if (img.nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
    Fail("ASLR enabled", "exe");

  for (const auto& s : sym::kAll) {
    bool text = false;
    const uint8_t* p = img.At(s.rva, s.len, &text);
    if (!p) {
      Fail("rva outside image", s.name);
      continue;
    }
    if (!text) Fail("not in an executable section", s.name);
    if (!BytesEqual(p, s.bytes, s.len)) Fail("bytes differ", s.name);
  }

  // Cross-checks: the call site really calls Fetch; Play's wrapper reaches the CSoundImp body.
  if (auto* p = img.At(sym::kRvaExamineFetchCall, 5)) {
    auto t = Rel32Target(sym::kImageBase + sym::kRvaExamineFetchCall, p);
    if (!t || *t != sym::kImageBase + sym::kRvaTlkFetch) Fail("call target is not Fetch", "examine");
  }
  if (auto* p = img.At(sym::kRvaPlay + 4, 5)) {
    auto t = Rel32Target(sym::kImageBase + sym::kRvaPlay + 4, p);
    if (!t || *t != 0x140494BF0ull) Fail("wrapper does not jump to 140494BF0", "CSound::Play");
  }

  std::printf("verify: %zu symbols, %d failures\n", std::size(sym::kAll), g_fail);
  return g_fail ? 1 : 0;
}
