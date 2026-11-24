-- SPDX-License-Identifier: GPL-2.0-or-later
-- Stygian Ice Storm (SPWI705) in a turn-based fight (all-spells save 000000990): its 8d8 cold damage is an op12
-- delayed 75 ticks (the ice drop's visuals at 65-70 ticks), so it must land in the caster's turn with the visuals,
-- not a round later. The Nameless One (level 20) casts it at a foe; the foe's HP must drop before the foe's own
-- next turn ends, and the damage must come in the caster's turn.
--   tools\launch.ps1 -Save 000000990 -Lua tests\ingame\tbc_ice.lua -Env @{PSTF_TEST_TICK='1'; PSTF_FEEDBACK_LOG='1'} -Until 'tbc_ice: ok|TEST FAIL' -Close -Timeout 300
local frames, tno, foe = 0, nil, 0x180018
local cast, castFrame, hp0, lastId, hitIn = false, nil, nil, nil, nil
local turnsAfter = 0
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h or -1 end
function pstfTestTick()
	frames = frames + 1
	if frames == 3 then
		tno = PSTF_PartyId(0)
		for _, st in ipairs({'LEVEL', 'LEVEL2', 'LEVEL3'}) do C:Eval('ChangeStat(Myself,' .. st .. ',20,SET)', 0) end
		PSTF_MakeEnemy(foe)
		return
	end
	if frames < 5 then return end
	for i = 0, 5 do
		local p = PSTF_PartyId(i)
		if p and p ~= -1 and hp(p) > 0 and hp(p) < 60 then PSTF_SetHp(p, 120) end
	end
	local id = PSTF_TbcCurrent()
	if cast and hp0 and not hitIn and hp(foe) < hp0 then
		hitIn = id ~= -1 and id or lastId  -- -1: the kill ended the fight in this tick
		PSTF_Note(string.format('tbc_ice: foe hp %d -> %d in the turn of %d (caster %d), %d frames after the cast',
			hp0, hp(foe), id, tno, frames - castFrame))
	end
	if id ~= lastId then
		lastId = id
		if cast then
			turnsAfter = turnsAfter + 1
			PSTF_Note(string.format('tbc_ice: turn of %d, foe hp %d, effects %s', id, hp(foe), tostring(PSTF_Effects(foe))))
		end
	end
	if hitIn then
		if hitIn ~= tno then return fail('the damage landed in another turn (' .. hitIn .. '), not the caster\'s') end
		PSTF_Note('tbc_ice: ok')
		pstfTestTick = nil
		return
	end
	if cast and turnsAfter > 8 then return fail('no damage after ' .. turnsAfter .. ' turns') end
	if id ~= tno then
		if id ~= -1 and not cast then PSTF_EndTurn() end
		if id ~= -1 and cast and id ~= foe then PSTF_EndTurn() end
		return
	end
	if not cast then
		cast, castFrame, hp0 = true, frames, hp(foe)
		local fx, fy = PSTF_SpritePos(foe, 1)
		PSTF_EvalAs(tno, string.format('JumpToPoint([%d.%d])', fx - 90, fy + 30), 1)
		PSTF_EvalAs(tno, 'SpellNoDec(NearestEnemyOf(Myself),2705)')
		PSTF_Note('tbc_ice: Stygian Ice Storm cast, foe hp ' .. hp0)
		return
	end
	if frames - castFrame == 700 then PSTF_EndTurn() end  -- a party turn waits for End
	if frames - castFrame > 1500 then return fail('no end') end
end
