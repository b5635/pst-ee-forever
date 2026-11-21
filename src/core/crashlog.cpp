// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/crashlog.h"

#include <windows.h>

#include <cstdio>

#include "core/log.h"

namespace pstf {
namespace {

volatile LONG g_logged = 0;

// "Torment.exe+3B6D20" style; no allocation (we may be in a bad state).
void Where(uint64_t va, char* out, size_t n) {
  HMODULE m = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(va), &m) &&
      m) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(m, path, MAX_PATH);
    const char* base = path;
    for (const char* p = path; *p; ++p)
      if (*p == '\\' || *p == '/') base = p + 1;
    std::snprintf(out, n, "%s+%llX", base, static_cast<unsigned long long>(va - reinterpret_cast<uint64_t>(m)));
  } else {
    std::snprintf(out, n, "%llX", static_cast<unsigned long long>(va));
  }
}

bool IsCode(uint64_t va) {
  MEMORY_BASIC_INFORMATION mi;
  if (va < 0x10000 || !VirtualQuery(reinterpret_cast<void*>(va), &mi, sizeof mi)) return false;
  return mi.State == MEM_COMMIT && mi.Type == MEM_IMAGE &&
         (mi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE | PAGE_EXECUTE_READWRITE)) != 0;
}

bool Fatal(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case 0xC0000409:  // fast fail
      return true;
    default:
      return false;
  }
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* ep) {
  const EXCEPTION_RECORD* er = ep->ExceptionRecord;
  if (!Fatal(er->ExceptionCode) || InterlockedIncrement(&g_logged) > 8) return EXCEPTION_CONTINUE_SEARCH;
  const CONTEXT* c = ep->ContextRecord;
  char at[160];
  Where(reinterpret_cast<uint64_t>(er->ExceptionAddress), at, sizeof at);
  Log("CRASH (first chance) %08lX at %s, thread %lu%s", er->ExceptionCode, at, GetCurrentThreadId(),
      er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2
          ? (er->ExceptionInformation[0] == 1 ? ", write" : er->ExceptionInformation[0] == 8 ? ", execute" : ", read")
          : "");
  if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
    Log("  address %llX", static_cast<unsigned long long>(er->ExceptionInformation[1]));
  Log("  rax=%llX rbx=%llX rcx=%llX rdx=%llX rsi=%llX rdi=%llX", c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi);
  Log("  rsp=%llX rbp=%llX r8=%llX r9=%llX r12=%llX r13=%llX r14=%llX r15=%llX", c->Rsp, c->Rbp, c->R8, c->R9,
      c->R12, c->R13, c->R14, c->R15);
  // Stack scan: code pointers above rsp (callers, in order; a few stale values too).
  const uint64_t* sp = reinterpret_cast<const uint64_t*>(c->Rsp);
  MEMORY_BASIC_INFORMATION mi;
  if (VirtualQuery(sp, &mi, sizeof mi) && mi.State == MEM_COMMIT) {
    const uint64_t end = reinterpret_cast<uint64_t>(mi.BaseAddress) + mi.RegionSize;
    int shown = 0;
    for (const uint64_t* p = sp; reinterpret_cast<uint64_t>(p + 1) <= end && p < sp + 1024 && shown < 24; ++p) {
      if (!IsCode(*p)) continue;
      Where(*p, at, sizeof at);
      Log("  stack +%03llX %s", static_cast<unsigned long long>((p - sp) * 8), at);
      ++shown;
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallCrashLog() {
  if (AddVectoredExceptionHandler(1, &Handler)) Log("crashlog: installed (engine dumps: Documents\\Infinity Engine - Enhanced Edition\\crash)");
}

bool GuardedCall(void (*fn)(uint64_t), uint64_t arg, uint32_t* code) {
  __try {
    fn(arg);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    if (code) *code = GetExceptionCode();
    return false;
  }
}

}  // namespace pstf
