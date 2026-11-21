// SPDX-License-Identifier: GPL-2.0-or-later
// Per-frame tick on the game thread: runs after CChitin::SynchronousUpdate, once per main-loop
// iteration (redirected call in CBaldurChitin::SynchronousUpdate, see symbols.h "frame tick").
#pragma once
#include <cstdint>

namespace pstf {

using TickFn = void (*)(uint64_t frame);

// Installs the hook on first use. False if the manifest bytes differ (then no tick ever runs).
bool AddTickHandler(TickFn fn);

}  // namespace pstf
