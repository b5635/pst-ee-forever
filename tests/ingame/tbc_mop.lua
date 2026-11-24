-- SPDX-License-Identifier: GPL-2.0-or-later
-- Missile of Patience in a turn-based fight (all-spells save 000000990): party slot 0 (a mage), level 20, casts it
-- (SPWI113, caster level 11+: the missile hovers and waits for targets); this script clicks a target (PSTF_MopFire,
-- the caster's target field) whenever the game's cursor waits (mode 20). In a turn-based fight it fires up to 3
-- times in the turn, then the missile is gone (cursor back to normal) and a 4th click does nothing.
--   tools\launch.ps1 -Save 000000990 -Lua tests\ingame\tbc_mop.lua -Env @{PSTF_TEST_TICK='1'; PSTF_FEEDBACK_LOG='1'} -Until 'tbc_mop: ok|TEST FAIL' -Close -Timeout 300
local frames, tno, foe = 0, nil, 0x180018
local cast, castFrame, clicks, lastClick, gone = false, nil, 0, 0, nil
local hits = 0
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h or -1 end
local function shots() return hits end
function pstfTestTick()
	frames = frames + 1
	if frames == 3 then
		tno = PSTF_PartyId(0)
		for _, st in ipairs({'LEVEL', 'LEVEL2', 'LEVEL3'}) do C:Eval('ChangeStat(Myself,' .. st .. ',20,SET)', 0) end
		PSTF_MakeEnemy(foe)
		return
	end
	if frames < 5 then return end
	if hp(foe) > 0 and hp(foe) < 500 then PSTF_SetHp(foe, 1000) end
	for i = 0, 5 do
		local p = PSTF_PartyId(i)
		if p and p ~= -1 and hp(p) > 0 and hp(p) < 40 then PSTF_SetHp(p, 80) end
	end
	local id = PSTF_TbcCurrent()
	if id ~= tno then
		if id ~= -1 and cast and not gone then
			PSTF_Note(string.format('tbc_mop: his turn ended after %d clicks, %d hits', clicks, shots()))
			return fail('the turn ended with the missile still to fire (' .. shots() .. ' hits)')
		end
		if id ~= -1 and not cast then PSTF_EndTurn() end  -- the others pass
		return
	end
	if not cast then
		cast, castFrame = true, frames
		local fx, fy = PSTF_SpritePos(foe, 1)
		PSTF_EvalAs(tno, string.format('JumpToPoint([%d.%d])', fx - 60, fy + 20), 1)
		PSTF_EvalAs(tno, 'SpellNoDec(NearestEnemyOf(Myself),2113)')
		PSTF_Note('tbc_mop: Missile of Patience cast')
		return
	end
	local cur = PSTF_Cursor()
	local h = hp(foe)
	if h > 0 and h < 1000 then
		hits = hits + 1
		PSTF_Note(string.format('tbc_mop: hit %d for %d', hits, 1000 - h))
		PSTF_SetHp(foe, 1000)
	end
	if cur == 20 and frames - lastClick > 40 then
		clicks = clicks + 1
		lastClick = frames
		PSTF_MopFire(tno, foe)
		PSTF_Note(string.format('tbc_mop: click %d (cursor %d)', clicks, cur))
	end
	if cur ~= 20 and clicks > 0 and not gone then
		gone = frames
		PSTF_Note(string.format('tbc_mop: the missile is gone after %d clicks (cursor %d)', clicks, cur))
	end
	if gone and frames - gone == 150 then  -- the last missiles land
		PSTF_Note(string.format('tbc_mop: %d hits; a click now: %s', shots(), tostring(PSTF_MopFire(tno, foe))))
		if shots() ~= 3 then return fail('it fired ' .. shots() .. ' times, not 3') end
	end
	if gone and frames - gone == 260 then
		PSTF_Note(string.format('tbc_mop: %d hits after the extra click, cursor %d', shots(), PSTF_Cursor()))
		if shots() ~= 3 then return fail('a 4th shot went (' .. shots() .. ')') end
		PSTF_Note('tbc_mop: ok')
		pstfTestTick = nil
		return
	end
	if frames - castFrame > 900 then return fail(string.format('no end: %d clicks, %d hits, cursor %d', clicks, shots(), cur)) end
end
