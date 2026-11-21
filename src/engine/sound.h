// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter for TLK fetches and CSound. The only place that knows these layouts.
// Pointers passed in are valid for the current call only; never store them.
#pragma once
#include <cstdint>

namespace pstf::engine {

struct StrRes;  // engine STR_RES (opaque)

// Signature of CTlkTable::Fetch on Windows x64 (see symbols.h).
using TlkFetchFn = char(__fastcall*)(void* tlk, uint32_t strref, StrRes* out, int no_prefix);

bool SoundApiAvailable();  // manifest bytes of every CSound function we call match

// Facts about a fetched STR_RES.
uint32_t TextLength(const StrRes* r);  // CString length
bool HasSound(const StrRes* r);        // TLK entry carried a sound resref
uint32_t PlayTimeMs(StrRes* r);        // CSound::GetPlayTime

// Plays the STR_RES sound the way the engine plays its own non-positional voice lines
// (CScreenInventory::SetErrorString, lin 3F5DA0): SetChannel(NARRATIO=0, no area),
// SetFireForget(1), Play(0). Fire-and-forget: survives the STR_RES destructor.
bool PlayNarration(StrRes* r);

}  // namespace pstf::engine
