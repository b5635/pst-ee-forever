// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace pstf {
namespace {
FILE* g_log = nullptr;
std::string g_dir;
}  // namespace

const char* DataDir() {
  if (g_dir.empty()) {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string p(path, n);
    g_dir = p.substr(0, p.find_last_of('\\')) + "\\pst_forever";
  }
  return g_dir.c_str();
}

void LogOpen() {
  std::string dir = DataDir();
  CreateDirectoryA(dir.c_str(), nullptr);
  CreateDirectoryA((dir + "\\logs").c_str(), nullptr);
  g_log = _fsopen((dir + "\\logs\\pst_forever.log").c_str(), "w", _SH_DENYWR);
}

void Log(const char* fmt, ...) {
  if (!g_log) return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  std::fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(g_log, fmt, ap);
  va_end(ap);
  std::fputc('\n', g_log);
  std::fflush(g_log);
}

}  // namespace pstf
