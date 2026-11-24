-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for the voice cut-offs ([voice] cut, src/modules/voice).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\voice_cut.lua
--      -Env @{PSTF_DLG_TEST='1'; PSTF_TBC_NOFIGHT='1'} -Until 'voice_cut: ok|TEST FAIL' -Close -Timeout 200
-- 1. examine 828 (Mortuary corpse, VV828) plays on NARRATIO; examining 856 while it plays leaves one voice.
-- 2. a dialogue opening (DMORTE, voiced) while 828 plays: no voice left on NARRATIO.
-- 3. replies clicked quickly until the dialogue closes: the dialogue voice (channel 7) is gone right after.
-- Toggle check: [voice] cut=0 fails at 2 (the examine line runs on) or 3.
local function note(s) PSTF_Note(s) end
local function fail(msg) note('TEST FAIL: ' .. msg) pstfDlgTest = nil end
local function now() return Infinity_GetClockTicks() end

local phase, t, speaker, frames = 'start', now(), -1, 0
local dlgSeen, tries = 0, 0
local function go(p) phase, t, frames = p, now(), 0 end
local function ch(n) return PSTF_VoiceCount(n) end

function pstfDlgTest()
	local ms = now() - t
	frames = frames + 1
	if phase == 'start' then
		if ms < 1500 then return end
		for s = 1, 7 do
			local id = PSTF_PartyId(s)
			if id ~= -1 then speaker = id end
		end
		if speaker == -1 then return fail('need a party member besides the Nameless One') end
		if ch(0) ~= 0 then return fail('NARRATIO busy before the test: ' .. ch(0)) end
		if not PSTF_Examine(828) then return fail('PSTF_Examine could not build a STR_RES') end
		go('first')
	elseif phase == 'first' then
		if frames < 3 then return end
		local n = ch(0)
		note('voice_cut: examine 828 -> NARRATIO voices ' .. n)
		if n ~= 1 then return fail('examine 828 should leave 1 voice on NARRATIO, has ' .. n) end
		PSTF_Examine(856)
		go('second')
	elseif phase == 'second' then
		if frames < 3 then return end
		local n = ch(0)
		note('voice_cut: examine 856 over 828 -> NARRATIO voices ' .. n)
		if n ~= 1 then return fail('a new examine should cut the old one: ' .. n .. ' voices on NARRATIO') end
		PSTF_Examine(828)  -- a different line than the last: plays
		go('talk')
	elseif phase == 'talk' then
		if frames < 3 then return end
		if ch(0) ~= 1 then return fail('examine 828 again did not play (' .. ch(0) .. ')') end
		PSTF_EvalAs(speaker, 'StartDialogOverride("DMORTE",Protagonist)', 1)
		go('wait_dlg')
	elseif phase == 'wait_dlg' then
		if PSTF_DlgNode() == 0 then
			if ms > 6000 then return fail('the dialogue did not open') end
			return
		end
		if frames < 30 then return end  -- the opening line is playing
		local n0, n7 = ch(0), ch(7)
		note('voice_cut: dialogue up -> NARRATIO ' .. n0 .. ', dialogue channel ' .. n7)
		if n0 ~= 0 then return fail('the dialogue opened but the examine line still plays (' .. n0 .. ')') end
		if n7 < 1 then return fail('no dialogue voice on channel 7 (DMORTE line unvoiced?)') end
		go('leave')
	elseif phase == 'leave' then
		if PSTF_DlgNode() == 0 then
			if frames < 3 then return end
			local n7 = ch(7)
			note('voice_cut: dialogue closed after ' .. tries .. ' clicks, voiced lines seen ' .. dlgSeen ..
				' -> dialogue channel ' .. n7)
			if dlgSeen == 0 then return fail('no voiced line was playing when the dialogue closed') end
			if n7 ~= 0 then return fail('the dialogue closed but its voice still plays (' .. n7 .. ')') end
			note('voice_cut: ok')
			pstfDlgTest = nil
			return
		end
		if ms < 350 then return end
		dlgSeen = ch(7) > 0 and 1 or 0  -- the line on screen when we click
		local last
		for _, v in ipairs(dialogTable or {}) do if v.marker ~= nil and not v.pstfRoll then last = v.marker end end
		if last and tries % 2 == 0 then pstfDlgReplyClick(last) else worldScreen:StepDialog() end
		tries = tries + 1
		t, frames = now(), 0
		if tries > 40 then return fail('could not leave the dialogue') end
	end
end
