// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/sound.h"

#include <cstring>

#include "core/patch.h"
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

constexpr int kChannelNarration = 0;  // SNDCHANN.2DA row 0: NARRATIO, VOLUME 100, 3D_SOUND 0

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
  reinterpret_cast<SetChannelFn>(Va(sym::kRvaSetChannel))(snd, kChannelNarration, nullptr);
  reinterpret_cast<SetFireForgetFn>(Va(sym::kRvaSetFireForget))(snd, 1);
  return reinterpret_cast<PlayFn>(Va(sym::kRvaPlay))(snd, 0) != 0;
}

}  // namespace pstf::engine
