// SPDX-License-Identifier: GPL-2.0-or-later
// Tiny test harness (header-only, no dependencies). Prints only failures and a summary.
#pragma once
#include <cstdio>
#include <vector>

namespace mt {
struct Case {
  const char* name;
  void (*fn)();
};
inline std::vector<Case>& Registry() {
  static std::vector<Case> r;
  return r;
}
inline int& Failures() {
  static int f = 0;
  return f;
}
struct Reg {
  Reg(const char* n, void (*f)()) { Registry().push_back({n, f}); }
};
}  // namespace mt

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST_CASE(name)                                         \
  static void MT_CAT(mt_fn_, __LINE__)();                       \
  static mt::Reg MT_CAT(mt_reg_, __LINE__)(name, &MT_CAT(mt_fn_, __LINE__)); \
  static void MT_CAT(mt_fn_, __LINE__)()
#define CHECK(expr)                                                              \
  do {                                                                           \
    if (!(expr)) {                                                               \
      ++mt::Failures();                                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                \
    }                                                                            \
  } while (0)
