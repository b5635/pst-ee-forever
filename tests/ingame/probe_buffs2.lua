-- SPDX-License-Identifier: GPL-2.0-or-later
-- Probe (turn-based fight, Hive save): Submerge the Will on the Nameless One in his turn, effects per turn start.
local thug = 0x180018
local tno, cur, n, cast, turns = nil, -1, 0, false, 0
function pstfTestTick()
	n = n + 1
	if n == 2 then tno = PSTF_PartyId(0) C:Eval('AddXPObject(Myself,3000000)', 0) end
	if n == 5 then PSTF_MakeEnemy(thug) end
	local _, _, h = PSTF_SpritePos(thug, 1)
	if h and h > 0 and h < 60 then PSTF_SetHp(thug, 150) end
	local id = PSTF_TbcCurrent()
	if id ~= cur then
		cur = id
		turns = turns + 1
		if tno and id ~= -1 then PSTF_Note('turn ' .. turns .. ' of ' .. string.format('%X', id)) PSTF_Effects(tno) PSTF_Status(tno) end
		if id == tno and not cast then cast = true PSTF_EvalAs(tno, 'SpellNoDec(Myself,2119)', 1) PSTF_Note('cast 2119') end
		if id ~= tno and id ~= -1 and PSTF_PartyId(0) ~= id then
			for i = 1, 5 do if PSTF_PartyId(i) == id then PSTF_EndTurn() end end
		end
	end
	if cast and id == tno and n % 20 == 0 then PSTF_Effects(tno) end
	if id == tno and cast and n % 40 == 39 then PSTF_EndTurn() end
	if turns > 30 or n > 4000 then PSTF_Note('probe_buffs2 done') pstfTestTick = nil end
end
