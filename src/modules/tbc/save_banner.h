// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Saving-throw banner ([tbc] save_banner): "[icon] Save Success: 18 (Death)" / "[icon] Save Failed: 12 (Death)"
// above the creature that rolled, for every save (the engine only said "Save vs. death : 18" for a save made).
// Timings measured frame by frame from the reference clip (30 fps): the banner fades in, the icon (PFSVOK /
// PFSVBAD, tools/make_save_bams.py) tumbles a d20 and turns to the verdict at 0.70 s, the banner holds where it
// is (no rise) and fades out at 2.8-3.0 s. The success/fail sound goes with the verdict.
#include <cstdint>
#include <string>

namespace pstf::tbc {

constexpr uint32_t kSaveFadeInMs = 150;
constexpr uint32_t kSaveVerdictMs = 700;   // icon frame 21: the frame turns red / cyan
constexpr uint32_t kSaveFadeOutMs = 2800;  // fully shown until here
constexpr uint32_t kSaveLifeMs = 3000;
constexpr int kSaveIconFrames = 32;        // 0..30 = t 0..1.0 s at 30 fps, 31 = the settled icon
constexpr uint32_t kSaveMergeMs = 300;     // one spell's several effects share one roll: one banner
constexpr int kSaveBanners = 6;            // on screen at once (UI menus PSTF_SAVE1..6)
constexpr int kSaveAboveHeadPx = 28;       // world px between the overhead-text point and the banner's middle

inline int SaveIconFrame(uint32_t ms) {
  const uint32_t f = ms * 30 / 1000;
  return f >= static_cast<uint32_t>(kSaveIconFrames - 1) ? kSaveIconFrames - 1 : static_cast<int>(f);
}

// Opacity of the whole banner (icon and text), 0..1.
inline double SaveBannerAlpha(uint32_t ms) {
  if (ms >= kSaveLifeMs) return 0.0;
  if (ms < kSaveFadeInMs) return static_cast<double>(ms) / kSaveFadeInMs;
  if (ms >= kSaveFadeOutMs) return static_cast<double>(kSaveLifeMs - ms) / (kSaveLifeMs - kSaveFadeOutMs);
  return 1.0;
}

// CheckSave's save bits (effect +0x40): 0 spell, 1 breath, 2 death (paralysation, poison), 3 wands, 4 polymorph.
inline const char* SaveTypeName(int bit) {
  switch (bit) {
    case 0: return "Spell";
    case 1: return "Breath";
    case 2: return "Death";
    case 3: return "Wands";
    case 4: return "Polymorph";
    default: return "Save";
  }
}

// `total`: the d20 plus the effect's save bonus, the number the engine printed.
inline std::string SaveBannerText(bool saved, int total, int save_bit) {
  return std::string(saved ? "Save Success: " : "Save Failed: ") + std::to_string(total) + " (" +
         SaveTypeName(save_bit) + ")";
}

// A new save of a creature whose banner started `since_ms` ago: the same roll seen again (another effect of the
// same spell) when it is that recent.
inline bool SaveBannerSameRoll(uint32_t since_ms) { return since_ms < kSaveMergeMs; }

}  // namespace pstf::tbc
