-- SPDX-License-Identifier: GPL-2.0-or-later
-- 8-member party in a turn-based fight (Hive, the 8-member quick save party8.lua writes): the nearest
-- Hive creature turns hostile (its HP refilled every tick so it lasts until all 8 had a turn); every one
-- of the 8 party members must be in the fight's initiative order and get a turn of their own (party AI plays them). Screenshots (PSTF_SHOT_DIR) show the 8-portrait HUD.
-- Run: tools\launch.ps1 -Save 000000001 -Lua tests\ingame\party8_fight.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'party8_fight: ok|TEST FAIL' -Close -Timeout 300
local f = 0
local party, seen, nseen = {}, {}, 0
local inFight = false
local enemy = -1
local function fail(msg) PSTF_Note('TEST FAIL: party8_fight: ' .. msg) pstfTestTick = nil end
function pstfTestTick()
	f = f + 1
	if f == 2 then
		for i = 0, 7 do
			local id = PSTF_PartyId(i)
			if id == -1 then return fail('party slot ' .. i .. ' empty: ' .. PSTF_PartyNames()) end
			party[id] = i
		end
		enemy = PSTF_MakeEnemyNearest()
		if enemy == -1 then return fail('no creature to make hostile') end
		return
	end
	if f < 3 then return end
	if f % 20 == 3 then PSTF_SetHp(enemy, 400) end
	local cur = PSTF_TbcCurrent()
	if cur ~= -1 and not inFight then
		inFight = true
		local n = 0
		for i = 1, 40 do
			local id = PSTF_FightId(i)
			if id == -1 then break end
			if party[id] then n = n + 1 end
		end
		PSTF_Note('party8_fight: fight on, ' .. n .. ' party members in the initiative order')
		if n ~= 8 then return fail(n .. ' of 8 party members in the fight') end
	end
	if cur ~= -1 and party[cur] and not seen[cur] then
		seen[cur] = true
		nseen = nseen + 1
		PSTF_Note('party8_fight: turn of party slot ' .. party[cur] .. ' (' .. nseen .. '/8)')
		if nseen == 8 then
			PSTF_Note('party8_fight: ok (all 8 took a turn)')
			pstfTestTick = nil
		end
	end
	if f > 6000 then return fail('only ' .. nseen .. ' of 8 took a turn in 6000 ticks') end
end
