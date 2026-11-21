// SPDX-License-Identifier: GPL-2.0-or-later
// Crash diagnostics. The engine writes its own minidumps (Documents\Infinity Engine - Enhanced
// Edition\crash, read them with tools/crash.py or cdb); this adds a line in OUR log for every fatal-kind
// exception (first chance): code, address as module+RVA, registers and the code pointers on the
// stack. pst_forever.dll RVAs resolve with build/Release/pst_forever.map (tools/crash.py --rva).
#pragma once
#include <cstdint>

namespace pstf {

void InstallCrashLog();

// Runs fn(arg) and swallows a hardware exception (SEH). Returns false if it faulted; the fault was
// logged by the crash log as well. For our own per-frame code: a bug turns the feature off instead of
// taking the game down.
bool GuardedCall(void (*fn)(uint64_t), uint64_t arg, uint32_t* code = nullptr);

}  // namespace pstf
