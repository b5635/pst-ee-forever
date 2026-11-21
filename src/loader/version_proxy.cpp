// SPDX-License-Identifier: GPL-2.0-or-later
// version.dll proxy. DllMain only: forward exports to System32 and patch the exe entry point
// (after checking its bytes) with a jump to EntryStub. EntryStub runs on the main thread,
// restores the entry bytes, loads pst_forever.dll, calls PSTF_Init and continues into the CRT.
// If anything is missing or different, the game runs vanilla.
#include <windows.h>

#include <cstring>

#include "core/patch_math.h"
#include "engine/symbols.h"

extern "C" void* g_real[17] = {};

namespace {

const char* const kNames[17] = {
    "GetFileVersionInfoA",      "GetFileVersionInfoByHandle", "GetFileVersionInfoExA",
    "GetFileVersionInfoExW",    "GetFileVersionInfoSizeA",    "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",   "GetFileVersionInfoW",
    "VerFindFileA",             "VerFindFileW",               "VerInstallFileA",
    "VerInstallFileW",          "VerLanguageNameA",           "VerLanguageNameW",
    "VerQueryValueA",           "VerQueryValueW"};

uint8_t g_saved_entry[pstf::kAbsJmpLen];
uint8_t* g_entry = nullptr;

bool WriteCode(void* dst, const void* src, size_t n) {
  DWORD old;
  if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(dst, src, n);
  VirtualProtect(dst, n, old, &old);
  FlushInstructionCache(GetCurrentProcess(), dst, n);
  return true;
}

void LoadCore() {
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return;
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash || (slash - path) + 20 >= MAX_PATH) return;
  wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"pst_forever.dll");
  HMODULE core = LoadLibraryW(path);
  if (!core) return;
  using InitFn = int(__cdecl*)();
  if (auto init = reinterpret_cast<InitFn>(GetProcAddress(core, "PSTF_Init"))) init();
}

// Replaces the exe entry point for one call.
int __cdecl EntryStub() {
  WriteCode(g_entry, g_saved_entry, sizeof g_saved_entry);
  LoadCore();
  using EntryFn = int(__cdecl*)();
  return reinterpret_cast<EntryFn>(g_entry)();
}

void LoadReal() {
  wchar_t path[MAX_PATH];
  UINT n = GetSystemDirectoryW(path, MAX_PATH);
  if (n == 0 || n + 13 >= MAX_PATH) return;
  wcscat_s(path, L"\\version.dll");
  HMODULE real = LoadLibraryW(path);
  if (!real) return;
  for (int i = 0; i < 17; ++i) g_real[i] = reinterpret_cast<void*>(GetProcAddress(real, kNames[i]));
}

void PatchEntry() {
  auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  if (reinterpret_cast<uint64_t>(base) != pstf::sym::kImageBase) return;
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->OptionalHeader.AddressOfEntryPoint != pstf::sym::kEntryRva) return;
  uint8_t* entry = base + pstf::sym::kEntryRva;
  if (!pstf::BytesEqual(entry, pstf::sym::kEntryBytes, sizeof pstf::sym::kEntryBytes)) return;
  std::memcpy(g_saved_entry, entry, sizeof g_saved_entry);
  uint8_t jmp[pstf::kAbsJmpLen];
  pstf::WriteAbsJmp(jmp, reinterpret_cast<uint64_t>(&EntryStub));
  g_entry = entry;
  WriteCode(entry, jmp, sizeof jmp);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);
    LoadReal();
    PatchEntry();
  }
  return TRUE;
}
