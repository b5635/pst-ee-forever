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

// Cut-offs. CSoundMixer::ClearChannel: every voice on the channel stops (what CSound::Play itself does
// for a type-3 channel before a new sound). False if the manifest bytes differ or there is no mixer.
bool ClearChannelAvailable();
bool ClearChannel(int channel);
// Voices the mixer has on `channel` now (probe; -1 unreadable).
int VoicesOn(int channel);
// Builds a STR_RES as CGameTrigger::OnActionButton does, calls `fetch` on it with the engine's TLK,
// destroys it. Probe for tests (plays nothing by itself). False if the manifest bytes differ.
bool WithFetchedStrref(uint32_t strref, TlkFetchFn fetch);

}  // namespace pstf::engine
