-- SPDX-License-Identifier: GPL-2.0-or-later
-- Turn-based attacks per round and menu screens (Hive save 000000126, party AI on):
--   - the protagonist (1 1/2 attacks per round in this save) swings 2, 1, 2, 1... times in his own
--     rounds: PSTF_TbcAttacks() at the end of each of his turns = (wanted, swung, APR x2);
--   - opening the inventory mid-fight and coming back keeps the fight: same fight number, same order.
-- Thugs next to the party turn hostile; their HP is topped up so the fight lasts.
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_apr.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'tbc_apr: ok|TEST FAIL' -Close -Timeout 400
local thugs = {0x180018, 0x140014, 0x1C001C}
local tno = PSTF_PartyId(0)
local frames, cur, tnoTurns, want, swung, halves = 0, -1, 0, 0, 0, 0
local seen = {}
local invAt, invFight, invOrder, invBack = -1, nil, nil, -1
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function order()
	local s = ''
	for i = 1, 12 do
		local id = PSTF_FightId(i)
		if id == -1 then break end
		s = s .. id .. ' '
	end
	return s
end
local function fightNo() return (PSTF_TbcState():match('fight=(%d+)')) end
function pstfTestTick()
	frames = frames + 1
	if frames == 5 then for _, t in ipairs(thugs) do PSTF_MakeEnemy(t) end end
	for _, t in ipairs(thugs) do
		local _, _, h = PSTF_SpritePos(t, 1)
		if h and h > 0 and h < 40 then PSTF_SetHp(t, 80) end
	end
	-- the inventory, mid-fight, for 40 frames
	if invAt > 0 and frames == invAt then
		invFight, invOrder = fightNo(), order()
		PSTF_Note('tbc_apr: inventory open (fight ' .. tostring(invFight) .. ', order ' .. invOrder .. ')')
		e:GetActiveEngine():SelectEngine(e:GetEngineInventory())
	end
	if invAt > 0 and frames == invAt + 40 then
		PSTF_Note('tbc_apr: screen inventory: ' .. tostring(e:GetActiveEngine() == e:GetEngineInventory()))
		e:GetActiveEngine():SelectEngine(worldScreen)
		invBack = frames + 10
	end
	if frames == invBack then
		local f, o = fightNo(), order()
		PSTF_Note('tbc_apr: back (fight ' .. tostring(f) .. ', order ' .. o .. ')')
		if f ~= invFight or o ~= invOrder then return fail('the inventory restarted the fight') end
	end
	local id = PSTF_TbcCurrent()
	if id == tno then
		want, swung, halves = PSTF_TbcAttacks()
	end
	if id ~= cur then
		if cur == tno then
			tnoTurns = tnoTurns + 1
			PSTF_Note(string.format('tbc_apr: TNO turn %d: wanted %d, swung %d, APR x2 %d', tnoTurns, want, swung, halves))
			seen[tnoTurns] = {want, swung}
			if halves ~= 3 then return fail('TNO APR x2 is ' .. halves .. ', expected 3 (1 1/2)') end
			if swung ~= want then return fail(string.format('TNO swung %d times, wanted %d', swung, want)) end
			local expect = (tnoTurns % 2 == 1) and 2 or 1
			if want ~= expect then return fail(string.format('turn %d wanted %d attacks, expected %d', tnoTurns, want, expect)) end
			if tnoTurns == 2 then invAt = frames + 5 end
			if tnoTurns == 4 then
				if invBack < 0 or frames < invBack then return fail('inventory step did not run') end
				PSTF_Note('tbc_apr: ok')
				pstfTestTick = nil
				return
			end
		end
		cur = id
	end
	if frames > 6000 then fail('timeout after ' .. tnoTurns .. ' TNO turns') end
end
