-- SPDX-License-Identifier: GPL-2.0-or-later
-- Short clip "voice": voiced examine text and voiced companion dialogue (8-member save 000000200, the Hive).
-- 1. the view centres on the nearest voiced examine point of AR0400; the real mouse glides there and clicks it
--    (record.ps1 -Clicks -DrawMouse acts on the 'showcase_click: DX,DY' line), the examine line plays on NARRATIO;
-- 2. Morte talks (DMORTE): each voiced line plays out, then the first reply is clicked, three lines, then away;
-- 3. the same with Annah (DANNAH).
-- Env PSTF_DLG_TEST=1 (pstfDlgTest runs every main-loop iteration, in dialogue too), PSTF_TBC_NOFIGHT=1.
-- Waits 15 s first (launch overlays). Ends with 'short_voice: done'.
local WAIT = 15000
-- AR0400 voiced examine points: strref, a point inside the polygon (from the ARE; VV<strref> plays)
local points = {{7210, 2323, 1456}, {7216, 3095, 1123}, {7216, 3421, 1236}, {7189, 4003, 1257}, {7220, 3721, 905},
	{7221, 2108, 1485}, {7221, 1717, 1548}, {7221, 2564, 1655}, {7221, 2888, 1318}, {7182, 4232, 953}, {7209, 1385, 2033}}
local t0, phase, t, tries, lines = nil, 'wait', 0, 0, 0
local closed = nil
local target, who = nil, nil
-- name, dialogue, the reply to pick first (plain text match; else the first reply)
local talkers = {{'Morte', 'DMORTE', 'how did you die'}, {'Annah', 'DANNAH'}}
local function replyMatching(pat)
	if not pat then return nil end
	for _, v in ipairs(dialogTable or {}) do
		if v.marker ~= nil and not v.pstfRoll and tostring(v.text or ''):lower():find(pat, 1, true) then return v.marker end
	end
end
local talker = 1
local nudges = {{0, 0}, {20, 10}, {-20, -10}, {0, 25}, {30, -20}}
local function now() return Infinity_GetClockTicks() end
local function go(p) phase, t = p, now() end
local function note(s) PSTF_Note('short_voice: ' .. s) end
local function slotOf(name)
	local i = 0
	for v in PSTF_PartyNames():gmatch('[^,]+') do
		if v:lower() == name:lower() then return i end
		i = i + 1
	end
	return -1
end
local function firstReply()
	for _, v in ipairs(dialogTable or {}) do if v.marker ~= nil and not v.pstfRoll then return v.marker end end
end
local function lastReply()
	local last
	for _, v in ipairs(dialogTable or {}) do if v.marker ~= nil and not v.pstfRoll then last = v.marker end end
	return last
end

function pstfDlgTest()
	local ms = now() - t
	if not t0 then t0 = now() go('wait') return end
	if phase == 'wait' then
		if now() - t0 < WAIT then return end
		local x, y = PSTF_SpritePos(PSTF_PartyId(0), 1)
		local best, bd = nil, 1e9
		for _, p in ipairs(points) do
			local d = (p[2] - x) ^ 2 + (p[3] - y) ^ 2
			if d < bd then best, bd = p, d end
		end
		target = best
		note(string.format('examine %d at %d,%d (Nameless at %d,%d)', best[1], best[2], best[3], x, y))
		PSTF_CenterAt(best[2], best[3])
		go('click')
	elseif phase == 'click' then
		if ms < 1200 then return end
		local vx, vy = PSTF_ViewCenter()
		local n = nudges[tries + 1]
		PSTF_Note(string.format('showcase_click: %d,%d', target[2] - vx + n[1], target[3] - vy + n[2]))
		tries = tries + 1
		go('heard')
	elseif phase == 'heard' then
		if PSTF_VoiceCount(0) > 0 then note('examine voice playing after ' .. tries .. ' clicks') go('listen') return end
		if ms < 2500 then return end
		if tries >= #nudges then note('examine: no voice (skipped)') go('talk') return end
		go('click')
	elseif phase == 'listen' then
		if PSTF_VoiceCount(0) > 0 and ms < 15000 then return end
		if ms < 800 then return end
		go('talk')
	elseif phase == 'talk' then
		if ms < 1000 then return end
		local tk = talkers[talker]
		if not tk then note('done') pstfDlgTest = nil return end
		local s = slotOf(tk[1])
		if s < 0 then note('no ' .. tk[1] .. ' in the party: ' .. PSTF_PartyNames()) talker = talker + 1 return end
		who = PSTF_PartyId(s)
		PSTF_CenterOn(who)
		-- the companion's own current dialogue file (its in-party talk), as when the player clicks it
		PSTF_EvalAs(who, 'StartDialogNoSet(Protagonist)', 1)
		closed = nil
		lines = 0
		note(tk[1] .. ' talks')
		go('dlg')
	elseif phase == 'dlg' then
		if PSTF_DlgNode() == 0 then
			-- a reply may hand over to another dialogue file: closed only after 1.5 s without a node
			closed = closed or now()
			if lines == 0 and ms < 8000 then return end
			if now() - closed < 1500 then return end
			if lines == 0 then note(talkers[talker][1] .. ': the dialogue did not open') end
			talker = talker + 1
			go('talk')
			return
		end
		if closed then closed = nil t = now() ms = 0 end
		-- let the voiced line play out (channel 7), then a short beat to read the replies
		if ms < 900 or (PSTF_VoiceCount(7) > 0 and ms < 14000) then return end
		if ms < 1500 then return end
		lines = lines + 1
		local r
		if lines == 1 then r = replyMatching(talkers[talker][3]) end
		if not r then r = (lines >= 3) and lastReply() or firstReply() end
		note(string.format('%s line %d, reply %s', talkers[talker][1], lines, tostring(r)))
		if r then pstfDlgReplyClick(r) else worldScreen:StepDialog() end
		if lines > 8 then worldScreen:StepDialog() end
		go('dlg')
	end
end
