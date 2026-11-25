-- SPDX-License-Identifier: GPL-2.0-or-later
-- Short clip "moves": turn-based maneuvers in the Hive (save 000000126, two hostile thugs, party turns driven here):
-- the Nameless One Runs in (dust trail), fighters Trip until one knocks a thug down (it falls, gets up on its turn),
-- then a party member in a thug's reach walks off and draws an attack of opportunity. The thugs' death save and AC
-- are made poor so the trip lands soon. Env PSTF_TEST_TICK=1. Starts after ~15 s. Ends with 'short_moves: done'.
local START = 520
local thug = 0x180018
local frames, cur, turnFrame = 0, -1, 0
local tno = nil
local ran, downed, aooAt, endAt = false, false, nil, nil
local tries = 0
local foes = {}
local function note(s) PSTF_Note('short_moves: ' .. s) end
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function stats()
	local t = {}
	for n in string.gmatch(PSTF_MoveStats(), '(%-?%d+)') do table.insert(t, tonumber(n)) end
	return {prone = t[8], aoo = t[9], aooSwung = t[10]}
end
local function party(id)
	for i = 0, 7 do if PSTF_PartyId(i) == id then return i end end
	return -1
end
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h or -1 end
local function isFighter(id)
	local c = PSTF_Class(id)
	return (c == 2 or c == 6 or c == 7 or c == 8 or c == 9 or c == 10 or c == 12 or c == 16 or c == 17)
end
local function cells(a, b)
	local ax, ay = PSTF_SpritePos(a, 1)
	local bx, by = PSTF_SpritePos(b, 1)
	if not ax or ax < 0 or not bx or bx < 0 then return 999 end
	local dx, dy = math.floor(ax / 16) - math.floor(bx / 16), math.floor(ay / 12) - math.floor(by / 12)
	return math.sqrt(dx * dx + dy * dy)
end
local function walkAway(id, from, px)
	local ax, ay = PSTF_SpritePos(id, 1)
	local bx, by = PSTF_SpritePos(from, 1)
	local dx, dy = ax - bx, ay - by
	local len = math.sqrt(dx * dx + dy * dy)
	if len < 1 then dx, dy, len = 1, 0, 1 end
	PSTF_EvalAs(id, 'MoveToPoint([' .. math.floor(ax + dx / len * px) .. '.' .. math.floor(ay + dy / len * px * 0.75) .. '])', 1)
end
local s0 = nil

local function partyTurn(id)
	if id == tno and not ran then
		ran = true
		PSTF_Maneuver('run')
		PSTF_EvalAs(id, 'Attack(NearestEnemyOf(Myself))', 1)
		note('Nameless runs in')
		return
	end
	if downed and not aooAt then
		for _, f in ipairs(foes) do
			if hp(f) > 0 and cells(id, f) <= 3.0 then
				aooAt = frames
				walkAway(id, f, 220)
				note(string.format('%X walks out of %X\'s reach', id, f))
				return
			end
		end
	end
	if not downed and isFighter(id) and tries < 30 then
		tries = tries + 1
		PSTF_Maneuver('trip')
		note(string.format('%X trips (try %d)', id, tries))
	end
	PSTF_EvalAs(id, 'Attack(NearestEnemyOf(Myself))', 1)
end

function pstfTestTick()
	frames = frames + 1
	if frames == 3 then tno = PSTF_PartyId(0) return end
	if frames < START then return end
	if frames == START then
		local first = PSTF_MakeEnemyNearest()
		foes = {first}
		if thug ~= first then PSTF_MakeEnemy(thug) foes[2] = thug end
		for _, k in ipairs(foes) do
			PSTF_SetHp(k, 120)
			PSTF_EvalAs(k, 'ChangeStat(Myself,SAVEVSDEATH,20,SET)', 0)
			PSTF_EvalAs(k, 'ChangeStat(Myself,ARMORCLASS,10,SET)', 0)
		end
		s0 = stats()
		note(#foes .. ' foes')
		return
	end
	for _, k in ipairs(foes) do if hp(k) > 0 and hp(k) < 30 then PSTF_SetHp(k, 80) end end
	for i = 0, 7 do
		local p = PSTF_PartyId(i)
		if p and p ~= -1 and hp(p) > 0 and hp(p) < 30 then PSTF_SetHp(p, 70) end
	end
	local s = stats()
	if not downed and s.prone > s0.prone then downed = true note('a thug is knocked down') end
	if aooAt and not endAt and (s.aooSwung > s0.aooSwung or frames - aooAt > 400) then
		note(s.aooSwung > s0.aooSwung and 'attack of opportunity swung' or 'no attack of opportunity')
		endAt = frames + 300
	end
	local id, _, _, used, acting = PSTF_TbcCurrent()
	if id ~= cur then cur, turnFrame = id, frames end
	if id ~= -1 and party(id) >= 0 then
		local t = frames - turnFrame
		if t == 3 then partyTurn(id)
		elseif t > 3 then
			local _, busy, path = PSTF_Action(id, 1)
			if (used and not acting and busy ~= 1 and path ~= 1 and t > 60) or t > 360 then PSTF_EndTurn() end
		end
	end
	if endAt and frames >= endAt then note('done') pstfTestTick = nil end
	if frames > START + 12000 then fail('no knock-down / attack of opportunity in time') end
end
