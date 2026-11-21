// SPDX-License-Identifier: GPL-2.0-or-later
// Examine voice-over. CGameTrigger::OnActionButton (info point branch) fetches the TLK entry
// into a STR_RES, shows the text as overhead text and in the log, and drops the sound.
// We redirect that one Fetch call: the real Fetch runs unchanged, then we play its sound
// exactly like the engine plays other non-positional voice lines. Text, timing, scripts: untouched.
#include "modules/voice/examine_voice.h"

#include <windows.h>

#include "core/log.h"
#include "core/patch.h"
#include "engine/sound.h"
#include "engine/symbols.h"
#include "modules/voice/voice_logic.h"

namespace pstf::voice {
namespace {

engine::TlkFetchFn g_fetch = nullptr;
ReplayGuard g_guard;

char __fastcall ExamineFetch(void* tlk, uint32_t strref, engine::StrRes* out, int no_prefix) {
  const char ret = g_fetch(tlk, strref, out, no_prefix);
  // Same condition as the engine's display (text non-empty), plus a sound in the TLK entry.
  if (engine::TextLength(out) == 0 || !engine::HasSound(out)) return ret;
  const uint32_t ms = engine::PlayTimeMs(out);
  if (!ShouldPlay(g_guard, strref, GetTickCount64(), ms)) return ret;
  const bool ok = engine::PlayNarration(out);
  Log("voice: examine strref %u (%u ms) %s", strref, ms, ok ? "played" : "play failed");
  return ret;
}

}  // namespace

bool InstallExamineVoice() {
  if (!engine::SoundApiAvailable()) {
    Log("voice: CSound functions differ from the manifest; examine voice off");
    return false;
  }
  for (const auto& s : sym::kAll) {
    if (s.rva == sym::kRvaTlkFetch && !CheckSymbol(s)) return false;
    if (s.rva != sym::kRvaExamineFetchCall) continue;
    const uint64_t original = RedirectCall(s, reinterpret_cast<void*>(&ExamineFetch));
    if (original != sym::kImageBase + sym::kRvaTlkFetch) {
      Log("voice: examine call site not patched (target %llX); examine voice off",
          static_cast<unsigned long long>(original));
      return false;
    }
    g_fetch = reinterpret_cast<engine::TlkFetchFn>(original);
    Log("voice: examine voice installed at %llX",
        static_cast<unsigned long long>(sym::kImageBase + s.rva));
    return true;
  }
  return false;
}

}  // namespace pstf::voice
