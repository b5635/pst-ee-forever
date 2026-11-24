// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace pstf::voice {

// [voice] examine=1: play the TLK sound of info-point ("examine") text when it is clicked.
// Returns true if installed.
bool InstallExamineVoice();

// [voice] cut=1: a new examine line or a dialogue opening stops the examine line; a dialogue closing
// stops the dialogue voice. Returns true if installed.
bool InstallVoiceCuts();

// Lua probes PSTF_VoiceCount(channel), PSTF_Examine(strref) (needs the Lua bridge).
void RegisterVoiceProbes();

}  // namespace pstf::voice
