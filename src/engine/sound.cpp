// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/sound.h"

#include <cstring>
#include <initializer_list>

#include "core/patch.h"
#include "engine/sprite_ptr.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {

// STR_RES layout: CString m_szText at +0x00, CSound m_cSound at +0x08.
//   lin CTlkTable::Fetch 673C70: "v6 = (char*)a3 + 8" passed to CSound::SetResRef.
//   win CGameTrigger::OnActionButton 1402196D3/1402196E7: CSound at rbp-258h, STR_RES at rbp-260h.
constexpr size_t kStrResSound = 0x08;
// MFC-style CString: data pointer; length is the int at data-8.
//   win 140219701..140219705: "mov rax,[STR_RES]; cmp [rax-8], esi; jz (skip display)".
constexpr ptrdiff_t kCStringLength = -8;
// CSound -> CSoundImp* at +0x18 (every win CSound wrapper starts "mov reg,[rcx+18h]");
// CSoundImp CResRef at +0x10 (win Play body 140494C44 "lea rcx,[rdi+10h]; call CResRef::GetResRef").
constexpr size_t kSoundImp = 0x18;
constexpr size_t kImpResRef = 0x10;

using SetChannelFn = int(__fastcall*)(void* snd, int channel, void* area);
using SetFireForgetFn = int(__fastcall*)(void* snd, int on);
using PlayFn = int(__fastcall*)(void* snd, int replay);
using GetPlayTimeFn = uint32_t(__fastcall*)(void* snd);

// SNDCHANN.2DA row 0: NARRATIO, VOLUME 100, 3D_SOUND 0 (sym::kChannelNarration).
using ClearChannelFn = void(__fastcall*)(void* mixer, int channel);
using VoiceChannelFn = int(__fastcall*)(void* voice);
using SoundCtorFn = void*(__fastcall*)(void* snd);
using SoundDtorFn = void(__fastcall*)(void* snd);
using CStringDtorFn = void(__fastcall*)(void* str);

bool Checked(std::initializer_list<uint32_t> rvas) {
  for (uint32_t rva : rvas)
    for (const auto& s : sym::kAll)
      if (s.rva == rva && !CheckSymbol(s)) return false;
  return true;
}

void* Mixer() {
  void* m = *reinterpret_cast<void* const*>(sym::kVaSoundMixer);
  return PlausiblePtr(reinterpret_cast<uint64_t>(m)) ? m : nullptr;
}

uint8_t* Sound(StrRes* r) { return reinterpret_cast<uint8_t*>(r) + kStrResSound; }

}  // namespace

bool SoundApiAvailable() {
  static constexpr uint32_t kNeeded[] = {sym::kRvaSetChannel, sym::kRvaSetFireForget, sym::kRvaPlay,
                                         sym::kRvaGetPlayTime};
  for (uint32_t rva : kNeeded) {
    for (const auto& s : sym::kAll)
      if (s.rva == rva && !CheckSymbol(s)) return false;
  }
  return true;
}

uint32_t TextLength(const StrRes* r) {
  const char* text = *reinterpret_cast<char* const*>(r);
  if (!text) return 0;
  int len;
  std::memcpy(&len, text + kCStringLength, sizeof len);
  return len > 0 ? static_cast<uint32_t>(len) : 0;
}

bool HasSound(const StrRes* r) {
  auto* snd = reinterpret_cast<const uint8_t*>(r) + kStrResSound;
  auto* imp = *reinterpret_cast<const uint8_t* const*>(snd + kSoundImp);
  return imp && imp[kImpResRef] != 0;
}

uint32_t PlayTimeMs(StrRes* r) {
  return reinterpret_cast<GetPlayTimeFn>(Va(sym::kRvaGetPlayTime))(Sound(r));
}

bool PlayNarration(StrRes* r) {
  void* snd = Sound(r);
  reinterpret_cast<SetChannelFn>(Va(sym::kRvaSetChannel))(snd, sym::kChannelNarration, nullptr);
  reinterpret_cast<SetFireForgetFn>(Va(sym::kRvaSetFireForget))(snd, 1);
  return reinterpret_cast<PlayFn>(Va(sym::kRvaPlay))(snd, 0) != 0;
}

bool ClearChannelAvailable() {
  static const bool ok = Checked({sym::kRvaClearChannel, sym::kRvaPlayMixerLoad});
  return ok;
}

bool ClearChannel(int channel) {
  if (!ClearChannelAvailable()) return false;
  void* m = Mixer();
  if (!m) return false;
  reinterpret_cast<ClearChannelFn>(Va(sym::kRvaClearChannel))(m, channel);
  return true;
}

int VoicesOn(int channel) {
  static const bool ok = Checked({sym::kRvaVoiceGetChannel, sym::kRvaPlayMixerLoad});
  void* m = Mixer();
  if (!ok || !m) return -1;
  const uint64_t imp = *reinterpret_cast<const uint64_t*>(m);
  if (!PlausiblePtr(imp)) return -1;
  auto get = reinterpret_cast<VoiceChannelFn>(Va(sym::kRvaVoiceGetChannel));
  int n = 0, guard = 0;
  // CPtrList node: next +0, prev +8, data +0x10 (win 140496000..140496007).
  for (uint64_t node = *reinterpret_cast<const uint64_t*>(imp + sym::kOffMixerVoiceHead);
       node && guard < 4096; node = *reinterpret_cast<const uint64_t*>(node), ++guard) {
    if (!PlausiblePtr(node)) return -1;
    void* v = *reinterpret_cast<void* const*>(node + 0x10);
    if (v && get(v) == channel) ++n;
  }
  return n;
}

bool WithFetchedStrref(uint32_t strref, TlkFetchFn fetch) {
  static const bool ok = Checked({sym::kRvaPchNilLoad, sym::kRvaSoundCtor, sym::kRvaSoundDtor,
                                  sym::kRvaCStringDtor, sym::kRvaTlkFetch});
  const uint64_t chitin = *reinterpret_cast<const uint64_t*>(sym::kVaBaldurChitin);
  if (!ok || !fetch || !PlausiblePtr(chitin)) return false;
  alignas(16) uint8_t res[0x100] = {};  // CString +0, CSound +8 (the engine's frame gives it 8+)
  std::memcpy(res, reinterpret_cast<const void*>(sym::kVaPchNil), 8);
  reinterpret_cast<SoundCtorFn>(Va(sym::kRvaSoundCtor))(res + kStrResSound);
  fetch(reinterpret_cast<void*>(chitin + sym::kOffChitinTlk), strref, reinterpret_cast<StrRes*>(res), 0);
  reinterpret_cast<SoundDtorFn>(Va(sym::kRvaSoundDtor))(res + kStrResSound);
  reinterpret_cast<CStringDtorFn>(Va(sym::kRvaCStringDtor))(res);
  return true;
}

}  // namespace pstf::engine
