-- SPDX-License-Identifier: GPL-2.0-or-later
-- 8-member party ([party] enabled=1 size=8), real flows on the Hive save (000000126):
--   * every companion not in the party is created next to TNO and joins with the engine's own JoinParty
--     action (what their dialogues run): the party reaches 8, each member has its own portrait slot;
--   * a 9th creature cannot join (the party is full at 8, as at 6 in vanilla);
--   * member 7 leaves (LeaveParty): the waiting 9th takes the place (the engine's own join queue);
--     the 9th leaves, member 7 joins again: the order closes the gap, the fixed slot is reused;
--   * party-wide item actions reach member 8 (PartyHasItem, TakePartyItem); triggers NumInParty*, InParty,
--     InPartySlot see slots 6/7; the game quick-saves with 8 (party8_load.lua loads it).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\party8.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'party8: ok|TEST FAIL' -Close -Timeout 200
local comp = { {"MORTE","Morte"}, {"ANNAH","Annah"}, {"DAKKON","Dakkon"}, {"GRACE","Grace"},
               {"NORDOM","Nordom"}, {"IGNUS","Ignus"}, {"VHAIL","Vhail"} }
local f = 0
local done = false
local function fail(msg) PSTF_Note('TEST FAIL: party8: ' .. msg) pstfTestTick = nil end
local function parse()
	local s = PSTF_Party()
	local count = tonumber(s:match('count=(%-?%d+)'))
	local order, fixed = {}, {}
	for v in s:match('order=([^ ]+)'):gmatch('[^,]+') do order[#order + 1] = tonumber(v) end
	for v in s:match('fixed=([^ ]+)'):gmatch('[^,]+') do fixed[#fixed + 1] = tonumber(v) end
	return count, order, fixed
end
local function inParty(names, script) return (',' .. names:upper() .. ','):find(',' .. script:upper() .. ',', 1, true) ~= nil end
local function checkSlots(want)
	local count, order, fixed = parse()
	if count ~= want then return 'count ' .. tostring(count) .. ' want ' .. want end
	local seen = {}
	for i = 1, 8 do
		local id = order[i]
		if i <= count then
			if id == -1 then return 'order slot ' .. (i - 1) .. ' empty' end
			local found = false
			for k = 1, 8 do if fixed[k] == id then found = true end end
			if not found then return 'member ' .. id .. ' has no portrait slot' end
			if seen[id] then return 'member ' .. id .. ' twice' end
			seen[id] = true
		elseif id ~= -1 then
			return 'order slot ' .. (i - 1) .. ' not empty past the count'
		end
	end
	local nfixed = 0
	for k = 1, 8 do if fixed[k] ~= -1 then nfixed = nfixed + 1 end end
	if nfixed ~= count then return nfixed .. ' portrait slots used, count ' .. count end
	return nil
end
local missing = {}
local x, y
local leaver
local saveFrom, saved, dialogs = nil, nil, 0
function pstfTestTick()
	f = f + 1
	if f == 2 then
		local names = PSTF_PartyNames()
		local _, _ = parse()
		x, y = PSTF_SpritePos(PSTF_PartyId(0))
		for _, c in ipairs(comp) do
			if not inParty(names, c[2]) then missing[#missing + 1] = c end
		end
		PSTF_Note('party8: start ' .. names .. ', joining ' .. #missing)
		for i, c in ipairs(missing) do
			C:Eval('CreateCreature("' .. c[1] .. '",[' .. (x + 30 * i) .. '.' .. (y + 20) .. '],0)', 0)
		end
	elseif f == 6 then
		for _, c in ipairs(missing) do C:Eval('ActionOverride("' .. c[2] .. '",JoinParty())', 0) end
	elseif f == 40 then
		local names = PSTF_PartyNames()
		PSTF_Note('party8: after joins ' .. names)
		local err = checkSlots(8)
		if err then return fail('after joins: ' .. err) end
		-- script triggers as dialogues and scripts evaluate them (TRIGGER.IDS)
		local tno = PSTF_PartyId(0)
		local _, order = parse()
		local checks = {
			{0x406a, 8, '', 1, 'NumInParty(8)'}, {0x406b, 7, '', 1, 'NumInPartyGT(7)'},
			{0x406b, 8, '', 0, 'NumInPartyGT(8)'}, {0x406c, 8, '', 0, 'NumInPartyLT(8)'},
			{0x4043, 0, 'Nordom', 1, 'InParty("Nordom")'}, {0x4043, 0, 'Vhail', 1, 'InParty("Vhail")'},
			{0x4090, 7, 'Nordom', 1, 'InPartySlot("Nordom",7)'}, {0x4090, 6, 'Grace', 1, 'InPartySlot("Grace",6)'},
			{0x4090, 5, 'Grace', 0, 'InPartySlot("Grace",5)'},
		}
		for _, c in ipairs(checks) do
			local r = PSTF_Trigger(tno, c[1], c[2], c[3])
			if r ~= c[4] then return fail(c[5] .. ' -> ' .. tostring(r) .. ', want ' .. c[4]) end
		end
		PSTF_Note('party8: triggers ok (NumInParty 8, InParty, InPartySlot 6/7)')
		-- party-wide item actions reach member 8: Nordom gets a Copper Earring, PartyHasItem sees it,
		-- TakePartyItem takes it from him
		if PSTF_Trigger(tno, 0x4042, 0, '', 'COPEAR') ~= 0 then return fail('party already has COPEAR') end
		C:Eval('GiveItemCreate("COPEAR","Nordom",1,0,0)', 0)
	elseif f == 50 then
		local tno = PSTF_PartyId(0)
		if PSTF_Trigger(tno, 0x4061, 0, 'Nordom', 'COPEAR') ~= 1 then return fail('Nordom did not get COPEAR') end
		if PSTF_Trigger(tno, 0x4042, 0, '', 'COPEAR') ~= 1 then return fail('PartyHasItem misses member 8') end
		C:Eval('TakePartyItem("COPEAR")', 0)
	elseif f == 41 + 20 then
		if PSTF_Trigger(PSTF_PartyId(0), 0x4061, 0, 'Nordom', 'COPEAR') ~= 0 then return fail('TakePartyItem missed member 8') end
		PSTF_Note('party8: party items ok (PartyHasItem / TakePartyItem reach member 8)')
		-- a 9th: a second Morte (MORTEF) is created and told to join; the party stays at 8
		C:Eval('CreateCreature("MORTEF",[' .. x .. '.' .. (y + 60) .. '],0)', 0)
	elseif f == 64 then
		C:Eval('ActionOverride("MorteF",JoinParty())', 0)
	elseif f == 84 then
		local err = checkSlots(8)
		if err then return fail('after a 9th tried to join: ' .. err) end
		local _, order = parse()
		local n = {}
		for v in PSTF_PartyNames():gmatch('[^,]+') do n[#n + 1] = v end
		leaver = n[7]
		if not leaver or leaver == '?' then return fail('no script name for member 7') end
		PSTF_Note('party8: full at 8 ok; member 7 = ' .. leaver .. ' leaves')
		C:Eval('ActionOverride("' .. leaver .. '",LeaveParty())', 0)
	elseif f == 114 then
		-- the engine's waiting joiner (the 9th, kept in CInfGame +0xA258 while the party was full) takes
		-- the free place: still 8, the leaver is out, MorteF is in
		local err = checkSlots(8)
		if err then return fail('after member 7 left: ' .. err) end
		local names = PSTF_PartyNames()
		if inParty(names, leaver) or not inParty(names, 'MorteF') then return fail('waiting joiner: ' .. names) end
		PSTF_Note('party8: leave + waiting joiner ok ' .. names)
		C:Eval('ActionOverride("MorteF",LeaveParty())', 0)
	elseif f == 134 then
		local err = checkSlots(7)
		if err then return fail('after the 9th left: ' .. err) end
		if PSTF_Trigger(PSTF_PartyId(0), 0x406a, 7, '') ~= 1 then return fail('NumInParty(7) after leaving') end
		if PSTF_Trigger(PSTF_PartyId(0), 0x4043, 0, leaver) ~= 0 then return fail('InParty(leaver) after leaving') end
		C:Eval('ActionOverride("' .. leaver .. '",JoinParty())', 0)
	elseif f == 164 then
		local err = checkSlots(8)
		if err then return fail('after rejoin: ' .. err) end
		PSTF_Note('party8: rejoin ok ' .. PSTF_PartyNames())
		saveFrom = f
	elseif saveFrom and not saved then
		-- save with 8 (quick save, 000000001-Quick-Save; tests\ingame\party8_load.lua loads it back).
		-- Party banters start dialogues (Grace's interjections fire with 7+); step through them first:
		-- the engine refuses to save during a dialogue, as in vanilla.
		local ok = PSTF_CanSave()
		if ok == 1 then
			worldScreen:OnQuickSaveButtonClick(false)
			saved = f
			PSTF_Note('party8: quick save requested ' .. PSTF_PartyNames())
		else
			dialogs = dialogs + 1
			pcall(function()
				if dialogResponses and dialogResponses[1] then worldScreen:OnDialogReplyClick(dialogResponses[1].marker)
				else worldScreen:StepDialog() end
			end)
			if f - saveFrom > 600 then return fail('could not save within 600 ticks') end
		end
	elseif saved and f == saved + 60 then
		PSTF_Note('party8: saved after ' .. dialogs .. ' dialogue steps')
		PSTF_Note('party8: ok')
		pstfTestTick = nil
	end
end
