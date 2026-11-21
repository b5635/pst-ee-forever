// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/version_guard.h"

#include <windows.h>
#include <bcrypt.h>

#include <vector>

#include "core/log.h"
#include "engine/symbols.h"

namespace pstf {

std::string Sha256File(const wchar_t* path) {
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return {};
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  std::string hex;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
      BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
    std::vector<unsigned char> buf(1 << 20);
    DWORD got = 0;
    bool ok = true;
    while (ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) && got > 0)
      ok &= BCryptHashData(h, buf.data(), got, 0) == 0;
    unsigned char digest[32];
    if (ok && BCryptFinishHash(h, digest, 32, 0) == 0) {
      static const char* k = "0123456789abcdef";
      for (unsigned char b : digest) {
        hex += k[b >> 4];
        hex += k[b & 15];
      }
    }
  }
  if (h) BCryptDestroyHash(h);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(f);
  return hex;
}

bool IsSupportedExe() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  const std::string got = Sha256File(path);
  if (got != sym::kExeSha256) {
    Log("version guard: Torment.exe sha256 %s is not the supported 3.2.2.0 build; nothing installed",
        got.c_str());
    return false;
  }
  return true;
}

}  // namespace pstf
