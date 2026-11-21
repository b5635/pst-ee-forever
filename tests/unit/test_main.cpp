// SPDX-License-Identifier: GPL-2.0-or-later
#include "mini_test.h"

int main() {
  for (const auto& c : mt::Registry()) c.fn();
  std::printf("unit: %zu cases, %d failures\n", mt::Registry().size(), mt::Failures());
  return mt::Failures() ? 1 : 0;
}
