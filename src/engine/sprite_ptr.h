// SPDX-License-Identifier: GPL-2.0-or-later
// Adapter-internal: raw engine pointers for files under src/engine only (never for modules).
#pragma once
#include <cstdint>

namespace pstf::engine {
uint64_t LookupSprite(int32_t id);  // CGameSprite* by id, 0 if none
uint64_t GameObject();              // CInfGame*, 0 if none
bool PlausiblePtr(uint64_t p);      // user-mode heap pointer (free object slots hold -1)
}  // namespace pstf::engine
