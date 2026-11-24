// SPDX-License-Identifier: GPL-2.0-or-later
// Examine voice-over. CGameTrigger::OnActionButton (info point branch) fetches the TLK entry
// into a STR_RES, shows the text as overhead text and in the log, and drops the sound.
// We redirect that one Fetch call: the real Fetch runs unchanged, then we play its sound
// exactly like the engine plays other non-positional voice lines. Text, timing, scripts: untouched.
//
// [voice] cut: a new examine line cuts the one still playing, a dialogue opening cuts the examine
// line, a dialogue closing cuts its own voice (vanilla lets the last NPC line run on). Each cut is the
// engine's CSoundMixer::ClearChannel, what CSound::Play does itself for a "clear" channel.
#include "modules/voice/examine_voice.h"

#include <windows.h>

#include "core/log.h"
#include "core/patch.h"
#include "core/tick.h"
#include "engine/dialog.h"
#include "engine/lua.h"
#include "engine/sound.h"
#include "engine/symbols.h"
#include "modules/dev/lua_bridge.h"
#include "modules/voice/voice_logic.h"

namespace pstf::voice {
namespace {

engine::TlkFetchFn g_fetch = nullptr;
ReplayGuard g_guard;
bool g_cut = false;  // [voice] cut installed
DialogWatch g_dialog;

char __fastcall ExamineFetch(void* tlk, uint32_t strref, engine::StrRes* out, int no_prefix) {
  const char ret = g_fetch(tlk, strref, out, no_prefix);
  // Same condition as the engine's display (text non-empty), plus a sound in the TLK entry.
  if (engine::TextLength(out) == 0 || !engine::HasSound(out)) return ret;
  const uint32_t ms = engine::PlayTimeMs(out);
  if (!ShouldPlay(g_guard, strref, GetTickCount64(), ms)) return ret;
  if (g_cut) engine::ClearChannel(sym::kChannelNarration);  // the previous examine line stops
  const bool ok = engine::PlayNarration(out);
  Log("voice: examine strref %u (%u ms) %s", strref, ms, ok ? "played" : "play failed");
  return ret;
}

void CutTick(uint64_t) {
  switch (StepDialogWatch(g_dialog, engine::DialogActive())) {
    case DialogEdge::kStarted:
      engine::ClearChannel(sym::kChannelNarration);
      Log("voice: dialogue started, examine voice cut");
      break;
    case DialogEdge::kEnded:
      engine::ClearChannel(sym::kChannelDialog);
      Log("voice: dialogue ended, dialogue voice cut");
      break;
    case DialogEdge::kNone:
      break;
  }
}

// Probe: PSTF_VoiceCount(channel) -> voices the mixer has on that channel (0 NARRATIO, 7 dialogue).
int __cdecl LuaVoiceCount(engine::LuaState* L) {
  engine::LuaPushNumber(L, engine::VoicesOn(static_cast<int>(engine::LuaNumberArg(L, 1, 0))));
  return 1;
}

// Probe: PSTF_Examine(strref) -> plays strref through the examine path (as a click on an info point
// whose text it is: replay guard, cut, log line). False if the STR_RES could not be built.
int __cdecl LuaExamine(engine::LuaState* L) {
  const auto strref = static_cast<uint32_t>(engine::LuaNumberArg(L, 1, 0));
  engine::LuaPushBool(L, g_fetch && engine::WithFetchedStrref(strref, &ExamineFetch));
  return 1;
}

}  // namespace

bool InstallVoiceCuts() {
  if (!engine::ClearChannelAvailable()) {
    Log("voice: CSoundMixer::ClearChannel differs from the manifest; voice cuts off");
    return false;
  }
  if (!AddTickHandler(&CutTick)) return false;
  g_cut = true;
  Log("voice: cuts installed (examine on new examine / dialogue start, dialogue voice on dialogue end)");
  return true;
}

void RegisterVoiceProbes() {
  dev::AddLuaFunction("PSTF_VoiceCount", &LuaVoiceCount);
  dev::AddLuaFunction("PSTF_Examine", &LuaExamine);
}

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
