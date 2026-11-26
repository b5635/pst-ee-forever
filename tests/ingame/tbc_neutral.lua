-- SPDX-License-Identifier: GPL-2.0-or-later
-- A neutral fighting a hostile next to the party joins the turn-based fight (Hive save 000000126, party AI on):
--   Join      a neutral Hive thug attacking the hostile one is pulled into the fight on the party's side;
--   Bystander a neutral standing by (no fight of its own) never joins;
--   Held      outside its turns the neutral does not move and the hostile takes no damage from it, so it
--             does not fight in real time against a creature that waits for its turn;
--   Turns     it gets turns of its own and attacks on them; the fight ends when the hostiles are gone.
--   Sides     if its own script turns it hostile (Hive thugs answer a call for help), it moves to the hostile side.
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_neutral.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'tbc_neutral: ok|TEST FAIL' -Close -Timeout 600
local H, N, B = 0x180018, pstfNeutral or 0x220022, 0x1C001C  -- Hive thugs: hostile, fighting neutral, bystander
local frames, joinedAt, nTurns, nAttackTicks = 0, nil, 0, 0
local lastCur, lastNx, lastNy, lastHhp = -1, nil, nil, nil
local hurtOutside, movedOutside, started = 0, 0, false
local turnedAt, switched = nil, nil
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function inFight(id)
	for i = 1, 20 do
		local f = PSTF_FightId(i)
		if f == -1 then return false end
		if f == id then return true end
	end
	return false
end
local function report(why)
	PSTF_Note(string.format('tbc_neutral: %s; neutral joined at frame %s, %d turns, %d attack ticks on them, moved %d / hurt the foe %d ticks outside them',
		why, tostring(joinedAt), nTurns, nAttackTicks, movedOutside, hurtOutside))
	if not joinedAt then return fail('the neutral fighting the hostile never joined the fight') end
	if nTurns < 1 then return fail('the neutral never got a turn') end
	if nAttackTicks < 1 then return fail('the neutral never attacked on its turns') end
	if movedOutside > 0 then return fail('the neutral moved outside its turns (' .. movedOutside .. ' ticks)') end
	if hurtOutside > 0 then return fail('the hostile took damage outside the party\'s and the neutral\'s turns') end
	PSTF_Note('tbc_neutral: ok')
	pstfTestTick = nil
end
function pstfTestTick()
	frames = frames + 1
	if frames == 2 then
		local x, y = PSTF_SpritePos(PSTF_PartyId(0))
		local at = {[H] = {260, 0}, [N] = {300, 50}, [B] = {150, -90}}
		for id, o in pairs(at) do
			PSTF_EvalAs(id, 'JumpToPoint([' .. math.floor(x + o[1]) .. '.' .. math.floor(y + o[2]) .. '])', 1)
		end
		return
	end
	if frames == 4 then
		PSTF_SetHp(H, 120, 120)  -- lives a few rounds: the neutral gets its turns, the fight still ends
		PSTF_SetHp(N, 150, 150)
		PSTF_SetHp(B, 60, 60)
		local _, _, _, ea = PSTF_Side(N)
		PSTF_Note('tbc_neutral: neutral EA ' .. ea .. ', bystander EA ' .. select(4, PSTF_Side(B)))
		if ea <= 30 or ea >= 200 then return fail('the neutral thug is not neutral (EA ' .. ea .. ')') end
		PSTF_MakeEnemy(H)
		PSTF_EvalAs(N, 'Attack([ENEMY])')  -- nearest creature of EA ENEMY: the hostile thug
		return
	end
	if frames < 4 then return end
	local cur = PSTF_TbcCurrent()
	if cur ~= -1 then started = true end
	if started and cur == -1 then return report('fight over') end
	if frames > 12000 then return report('timeout') end
	local bside, _, _, bea = PSTF_Side(B, 1)
	if bside ~= 0 and bea > 30 and bea < 200 then return fail('the neutral bystander joined the fight') end
	local side, target, action, nea = PSTF_Side(N, 1)
	if frames % 40 == 0 then
		local x1, y1 = PSTF_SpritePos(N, 1)
		local x2, y2, h2 = PSTF_SpritePos(H, 1)
		PSTF_Note(string.format('tbc_neutral: frame %d cur %d: neutral side %d target %d action %d ea %d at %d,%d; hostile at %d,%d hp %d; bystander side %d ea %d',
			frames, cur, side, target, action, nea, x1, y1, x2, y2, h2, bside, bea))
	end
	-- Hive thugs call for help: its own script may turn it hostile mid-fight; then it changes sides
	if nea >= 200 and joinedAt then
		turnedAt = turnedAt or frames
		if side == 2 and not switched then
			switched = frames
			PSTF_Note(string.format('tbc_neutral: neutral turned EA %d at frame %d, hostile side at frame %d', nea, turnedAt, frames))
		end
		if not switched and frames > turnedAt + 40 then return fail('the neutral turned hostile but stays on the party\'s side') end
	end
	if not joinedAt and inFight(N) then
		joinedAt = frames
		PSTF_Note(string.format('tbc_neutral: neutral joins, side %d, target %d, action %d', side, target, action))
		if side ~= 1 then return fail('the neutral joined on side ' .. side .. ', not the party\'s') end
	end
	local nx, ny = PSTF_SpritePos(N, 1)
	local _, _, hhp = PSTF_SpritePos(H, 1)
	if joinedAt and frames > joinedAt + 3 and cur ~= N and lastCur ~= N then
		if lastNx and (nx ~= lastNx or ny ~= lastNy) then movedOutside = movedOutside + 1 end
		-- party turns (autoparty) may hurt it; outside those and the neutral's own, nothing should
		local party = false
		for i = 0, 7 do if PSTF_PartyId(i) == cur then party = true end end
		if lastHhp and hhp < lastHhp and not party and cur ~= H then hurtOutside = hurtOutside + 1 end
	end
	if cur == N then
		if lastCur ~= N then nTurns = nTurns + 1 PSTF_Note('tbc_neutral: neutral turn ' .. nTurns) end
		if action == 3 or action == 94 or action == 98 or action == 105 or action == 134 then nAttackTicks = nAttackTicks + 1 end
	end
	lastCur, lastNx, lastNy, lastHhp = cur, nx, ny, hhp
end
