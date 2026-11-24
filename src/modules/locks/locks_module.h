// SPDX-License-Identifier: GPL-2.0-or-later
// Module: lock rolls (BG3 style). A party member who reaches a locked door or container to pick it (thief
// ability) or bash it (attack cursor) stops at the lock and the d20 roll panel opens (the world pauses); the
// result is handed to the engine's own PickLock/BashDoor, which opens or fails the lock with its triggers,
// sounds, feedback and XP. Rules: modules/locks/lock_rules.h. Key-only locks (difficulty 100) and everyone
// outside the party stay vanilla. Ini [locks] rolls=1 (0 = vanilla: no hooks).
#pragma once
#include "core/ini.h"

namespace pstf::locks {

void InstallLockRolls(const IniMap& ini);

}  // namespace pstf::locks
