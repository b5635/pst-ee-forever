-- SPDX-License-Identifier: GPL-2.0-or-later
-- Builds the all-spells test save from the Hive save (000000126): Fall-from-Grace joins (the party's priest),
-- every party member gets 3,000,000 XP (real level-ups), the Nameless One, Ignus and Dak'kon learn every mage
-- spell and Grace every priest spell (CLUAConsole::AddSpell = opcode 147 Learn Spell, the console's own path), the
-- party rests (spells memorised), then the game quick-saves (000000001-Quick-Save).
-- pstfSpellSaveKeep (tools\make_spell_save.ps1 -Keep, from the all-spells save itself): no join, no XP, and the
-- time of day the save had comes back after the rest (DayNight).
-- tools\make_spell_save.ps1 runs this and stores the save as tests\saves\000000990-PSTF-AllSpells.
-- pstfSpellList comes from tests/ingame/spell_list.lua, put in front of this file by tools/make_spell_save.ps1.
if not pstfSpellList then error('pstfSpellList missing: run through tools/make_spell_save.ps1') end
local f, saveFrom, saved, dialogs = 0, nil, nil, 0
local x, y
local hour0 = 0
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function slotOf(name)
	local i = 0
	for v in PSTF_PartyNames():gmatch('[^,]+') do
		if v == name then return i end
		i = i + 1
	end
	return -1
end
function pstfTestTick()
	f = f + 1
	if f == 2 then
		local t = PSTF_WorldTime()
		hour0 = math.floor((t / 15) % 7200 / 300)  -- TIME.IDS hours: a day is 7200 game seconds (GTIMES), 15 ticks each
		PSTF_Note('make_spell_save: world time ' .. t .. ', hour ' .. hour0)
		if pstfSpellSaveKeep then f = 79 return end
		x, y = PSTF_SpritePos(PSTF_PartyId(0))
		if slotOf('Grace') < 0 then C:Eval('CreateCreature("GRACE",[' .. (x + 40) .. '.' .. (y + 20) .. '],0)', 0) end
	elseif f == 6 then
		if slotOf('Grace') < 0 then C:Eval('ActionOverride("Grace",JoinParty())', 0) end
	elseif f == 40 then
		PSTF_Note('make_spell_save: party ' .. PSTF_PartyNames())
		if slotOf('Grace') < 0 then return fail('Grace did not join') end
		for i = 0, 5 do
			if PSTF_PartyId(i) ~= -1 then C:Eval('AddXPObject(Myself,3000000)', i) end
		end
	elseif f == 80 then
		local grace, mage, priest = slotOf('Grace'), 0, 0
		local mages = {0}
		for _, n in ipairs({'Ignus', 'Dakkon'}) do
			local sl = slotOf(n)
			if sl >= 0 then table.insert(mages, sl) else PSTF_Note('make_spell_save: ' .. n .. ' is not in the party') end
		end
		for _, s in ipairs(pstfSpellList) do
			if s[1]:sub(1, 4) == 'SPWI' then
				for _, sl in ipairs(mages) do C:AddSpell(s[1], sl) end
				mage = mage + 1
			elseif grace >= 0 then
				C:AddSpell(s[1], grace)
				priest = priest + 1
			end
		end
		PSTF_Note(string.format('make_spell_save: learned %d mage spells (slots %s), %d priest spells (Grace, slot %d); party %s', mage,
			table.concat(mages, ','), priest, grace, PSTF_PartyNames()))
	elseif f == 120 then
		C:Eval('RestParty()', 0)
	elseif f == 300 and pstfSpellSaveKeep then
		C:Eval('DayNight(' .. hour0 .. ')', 0)  -- back to the hour it had (the rest moved it on)
	elseif f == 360 then
		PSTF_Note('make_spell_save: hour now ' .. math.floor((PSTF_WorldTime() / 15) % 7200 / 300))
	elseif f == 400 then
		saveFrom = f
	elseif saveFrom and not saved then
		if PSTF_CanSave() == 1 then
			worldScreen:OnQuickSaveButtonClick(false)
			saved = f
			PSTF_Note('make_spell_save: quick save requested')
		else
			dialogs = dialogs + 1
			pcall(function()
				if dialogResponses and dialogResponses[1] then worldScreen:OnDialogReplyClick(dialogResponses[1].marker)
				else worldScreen:StepDialog() end
			end)
			if f - saveFrom > 900 then return fail('could not save within 900 ticks') end
		end
	elseif saved and f == saved + 90 then
		PSTF_Note('make_spell_save: ok (' .. PSTF_PartyNames() .. ', ' .. dialogs .. ' dialogue steps)')
		pstfTestTick = nil
	end
end
