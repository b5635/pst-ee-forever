// SPDX-License-Identifier: GPL-2.0-or-later
// L0 tests for the saving-throw banner (src/modules/tbc/save_banner.h). Timings from the reference clip at 30 fps:
// visible from 0, verdict (frame turns red / cyan) at frame 21 = 0.70 s, hand settled by 0.9 s, fade-out over the
// last 0.2 s of 3.0 s; frames of tools/make_save_bams.py (32: 0..30 at 30 fps, 31 settled).
#include "mini_test.h"
#include "modules/tbc/save_banner.h"

using namespace pstf::tbc;

TEST_CASE("save banner text: success and failure, the engine's number, the save's name") {
  CHECK(SaveBannerText(true, 18, 2) == "Save Success: 18 (Death)");
  CHECK(SaveBannerText(false, 12, 2) == "Save Failed: 12 (Death)");
  CHECK(SaveBannerText(false, 3, 0) == "Save Failed: 3 (Spell)");
  CHECK(SaveBannerText(true, 20, 1) == "Save Success: 20 (Breath)");
  CHECK(SaveBannerText(true, 9, 3) == "Save Success: 9 (Wands)");
  CHECK(SaveBannerText(true, 15, 4) == "Save Success: 15 (Polymorph)");
  CHECK(SaveBannerText(true, 15, -1) == "Save Success: 15 (Save)");
}

TEST_CASE("save banner icon frames follow the clip at 30 fps, then hold") {
  CHECK(SaveIconFrame(0) == 0);
  CHECK(SaveIconFrame(kSaveVerdictMs) == 21);
  CHECK(SaveIconFrame(1000) == 30);
  CHECK(SaveIconFrame(1034) == 31);
  CHECK(SaveIconFrame(2500) == 31);
}

TEST_CASE("save banner sound: at once by default, else after the delay") {
  CHECK(SaveSoundDue(0, 0));
  CHECK(SaveSoundDue(0, -5));
  CHECK(!SaveSoundDue(699, 700));
  CHECK(SaveSoundDue(700, 700));
}

TEST_CASE("save banner: fades in, holds still, fades out, gone at 3 s") {
  CHECK(SaveBannerAlpha(0) == 0.0);
  CHECK(SaveBannerAlpha(75) == 0.5);
  CHECK(SaveBannerAlpha(150) == 1.0);
  CHECK(SaveBannerAlpha(2799) == 1.0);
  CHECK(SaveBannerAlpha(2900) == 0.5);
  CHECK(SaveBannerAlpha(3000) == 0.0);
  CHECK(SaveBannerAlpha(5000) == 0.0);
}

TEST_CASE("save banner: one spell's effects share a roll, a later save gets its own banner") {
  CHECK(SaveBannerSameRoll(0));
  CHECK(SaveBannerSameRoll(299));
  CHECK(!SaveBannerSameRoll(300));
}
