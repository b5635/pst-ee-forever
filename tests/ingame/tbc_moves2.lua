-- SPDX-License-Identifier: GPL-2.0-or-later
-- Turn-based maneuvers, second suite (Hive save 000000126, party turns driven by this script, one hostile thug):
--   Withdraw anim  a PST character that withdraws walks with the WALK cycle (run flag 0, cycle 5), not the run frames;
--   AoO            a party member in the thug's reach walks away (running): the attack of opportunity comes the first
--                  time (no retry: a running step may jump past the reach edge, the lookahead must catch it);
--   Trip           an armed fighter's swing: on a hit that knocks down, the thug falls (pose 1 -> 2), its AC is 4 worse
--                  (+4 to hit it), and at its next turn it gets up (pose 3) with its AC back;
--   Buff           Submerge the Will on the Nameless One: its status icon shows, its attached shield's end does not
--                  move while others act and comes one round (75 ticks) closer at his turn start;
--   No push        no held combatant plays its walk sequence without a path for more than 2 ticks.
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_moves2.lua -Env @{PSTF_TEST_TICK='1'} -Until 'tbc_moves2: ok|TEST FAIL' -Close -Timeout 500
local thug = 0x180018
local frames, cur, turnFrame = 0, -1, 0
local done = {}
local pending = nil
local tno = nil
local walkInPlace = {}
local buff = {cast = false, ends = nil, tnoTurns = 0}
local tripTries = 0
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function stats()
	local t = {}
	for n in string.gmatch(PSTF_MoveStats(), '(%-?%d+)') do table.insert(t, tonumber(n)) end
	return {takedowns = t[6], tdhits = t[7], prone = t[8], aoo = t[9], aooSwung = t[10], shortRuns = t[13]}
end
local function party(id)
	for i = 0, 7 do if PSTF_PartyId(i) == id then return i end end
	return -1
end
local function cells(a, b)
	local ax, ay = PSTF_SpritePos(a, 1)
	local bx, by = PSTF_SpritePos(b, 1)
	if not ax or ax < 0 or not bx or bx < 0 then return 999 end
	local dx, dy = math.floor(ax / 16) - math.floor(bx / 16), math.floor(ay / 12) - math.floor(by / 12)
	return math.sqrt(dx * dx + dy * dy)
end
local function walkAway(id, px)
	local ax, ay = PSTF_SpritePos(id, 1)
	local bx, by = PSTF_SpritePos(thug, 1)
	local dx, dy = ax - bx, ay - by
	local len = math.sqrt(dx * dx + dy * dy)
	if len < 1 then dx, dy, len = 1, 0, 1 end
	PSTF_EvalAs(id, 'MoveToPoint([' .. math.floor(ax + dx / len * px) .. '.' .. math.floor(ay + dy / len * px * 0.75) .. '])', 1)
end
local function anim(id)
	local t = {}
	for n in string.gmatch(PSTF_AnimProbe(id), '(%-?%d+)') do table.insert(t, tonumber(n)) end
	return t[1], t[2], t[3], t[4]  -- run flag, cycle, frame, sprite sequence
end
local function stance(id)
	local t = {}
	for n in string.gmatch(PSTF_Stance(id), '(%-?%d+)') do table.insert(t, tonumber(n)) end
	return t  -- ran withdrawn stacks takedown prone reaction_used
end
local function ac(id) local _, a = PSTF_Stats(id) return a end
local function isFighter(id)
	local c, sp = PSTF_Class(id)
	return (c == 2 or c == 6 or c == 7 or c == 8 or c == 9 or c == 10 or c == 12 or c == 16 or c == 17) and sp ~= 2
end

local function partyTurn(id)
	if id == tno and not buff.cast then
		buff.cast = true
		PSTF_EvalAs(id, 'SpellNoDec(Myself,2119)', 1)
		PSTF_Note('tbc_moves2: Submerge the Will cast')
		return
	end
	local flag = anim(id)
	if not done.withdrawAnim and flag ~= -1 then
		if not PSTF_Maneuver('withdraw') then return fail('Withdraw refused') end
		walkAway(id, 120)
		pending = {kind = 'withdrawAnim', id = id, walkFrames = 0, runFrames = 0}
		done.withdrawAnim = true
		return
	end
	local near = cells(id, thug) <= 3.0
	local ts = stance(thug)
	if done.withdrawAnim and not done.aoo and near and ts[5] == 0 and ts[6] == 0 and PSTF_State(thug) % 64 < 32 then
		pending = {kind = 'aoo', s0 = stats()}
		walkAway(id, 220)
		done.aoo = true
		return
	end
	if not done.trip and isFighter(id) and tripTries < 24 and ts[5] == 0 then
		if not PSTF_Maneuver('trip') then return fail('Trip refused for a fighter') end
		tripTries = tripTries + 1
		pending = {kind = 'trip', s0 = stats(), ac0 = ac(thug), sawFall = false, sawDown = false}
		PSTF_EvalAs(id, 'Attack(NearestEnemyOf(Myself))', 1)
		return
	end
	PSTF_EvalAs(id, 'Attack(NearestEnemyOf(Myself))', 1)
end

local function checkPending(turnOver)
	if not pending then return end
	local s = stats()
	if pending.kind == 'withdrawAnim' then
		local _, _, _, _, _, path = PSTF_SpritePos(pending.id, 1)
		if path and path > 0 then
			local flag, cyc = anim(pending.id)
			if cyc == 4 then pending.runFrames = pending.runFrames + 1 end
			if cyc == 5 and flag == 0 then pending.walkFrames = pending.walkFrames + 1 end
		end
		if turnOver then
			PSTF_Note(string.format('tbc_moves2: withdraw walk: %d walk-cycle frames, %d run-cycle frames', pending.walkFrames, pending.runFrames))
			if pending.walkFrames < 3 or pending.runFrames > 1 then return fail('Withdraw did not walk with the walk cycle') end
			pending = nil
		end
	elseif pending.kind == 'aoo' and (s.aoo > pending.s0.aoo or turnOver) then
		local r, m = PSTF_Aoo()
		PSTF_Note(string.format('tbc_moves2: walk away from reach: aoo %d -> %d (reactor %d)', pending.s0.aoo, s.aoo, r))
		if s.aoo == pending.s0.aoo then return fail('walking out of reach provoked no attack of opportunity') end
		pending = {kind = 'aooSwing', s0 = pending.s0, frame = frames}
	elseif pending.kind == 'aooSwing' and (s.aooSwung > pending.s0.aooSwung or PSTF_Aoo() == -1 or frames - pending.frame > 300) then
		PSTF_Note(string.format('tbc_moves2: attack of opportunity swung %d', s.aooSwung - pending.s0.aooSwung))
		if s.aooSwung == pending.s0.aooSwung then return fail('the attack of opportunity never swung') end
		pending = nil
	elseif pending.kind == 'trip' then
		local ph = PSTF_Pose(thug)
		if (ph == 1 or ph == 2) and not pending.sawDown then
			pending.sawFall = true
			pending.sawDown = true
			pending.acDown = ac(thug)
			PSTF_Note(string.format('tbc_moves2: thug down: AC %d -> %d', pending.ac0, pending.acDown))
			if pending.acDown ~= pending.ac0 + 4 then return fail('knocked down without AC +4 (attackers +4 to hit)') end
		end
		if turnOver then
			PSTF_Note(string.format('tbc_moves2: trip: takedowns %d, hits %d, knocked down %d, fall %s, down %s', s.takedowns - pending.s0.takedowns,
				s.tdhits - pending.s0.tdhits, s.prone - pending.s0.prone, tostring(pending.sawFall), tostring(pending.sawDown)))
			if s.prone > pending.s0.prone and not pending.sawDown and PSTF_Pose(thug) > 0 then pending.sawDown = true end  -- fell on the last tick
			if s.prone > pending.s0.prone then
				if not pending.sawDown then return fail('knocked down but never fell (pose)') end
				pending = {kind = 'rise', ac0 = pending.ac0, sawRise = false, frame = frames}
				done.trip = true
			else
				pending = nil
			end
		end
	elseif pending.kind == 'rise' then
		-- it gets up at its own turn start (that turn's action goes; an AI with nothing left ends it at once,
		-- and the get-up plays on while the next creature acts)
		local ph = PSTF_Pose(thug)
		if ph == 3 then pending.sawRise = true end
		if pending.sawRise and ph == 0 then
			local a = ac(thug)
			PSTF_Note(string.format('tbc_moves2: thug got up, AC %d (before the trip %d)', a, pending.ac0))
			if a ~= pending.ac0 then return fail('AC penalty still there after getting up') end
			if PSTF_State(thug) % 64 >= 32 then return fail('helpless after getting up') end
			done.rise = true
			pending = nil
		elseif frames - pending.frame > 2500 then
			return fail('the thug never got up')
		end
	end
end

local function watchBuff(newTurn)
	if not buff.cast or not tno or (buff.ends and not newTurn) then return end
	local n, left = PSTF_Attached(tno)
	if n == 0 or left == 0 then return end
	if not buff.ends then
		buff.ends = left
		if PSTF_Status(tno) < 1 then return fail('Submerge the Will shows no status icon') end
		PSTF_Note('tbc_moves2: Submerge the Will on, shield ends in ' .. left .. ' ticks, status icon shown')
		return
	end
	if newTurn then
		local d = buff.ends - left
		if cur == tno then
			buff.tnoTurns = buff.tnoTurns + 1
			PSTF_Note(string.format('tbc_moves2: his turn start: shield end %d -> %d', buff.ends, left))
			if math.abs(d - 75) > 2 then return fail('the shield did not step one round at his turn') end
			if buff.tnoTurns >= 2 then done.buff = true end
		elseif math.abs(d) > 2 then
			return fail(string.format('the shield ran out in world time during another turn (%d -> %d)', buff.ends, left))
		end
		buff.ends = left
	end
end

function pstfTestTick()
	frames = frames + 1
	if frames == 2 then tno = PSTF_PartyId(0) C:Eval('AddXPObject(Myself,3000000)', 0) end
	if frames == 5 then PSTF_MakeEnemy(thug) end
	local _, _, h = PSTF_SpritePos(thug, 1)
	if h and h > 0 and h < 60 then PSTF_SetHp(thug, 150) end
	for i = 0, 5 do
		local p = PSTF_PartyId(i)
		if p and p ~= -1 then
			local _, _, ph = PSTF_SpritePos(p, 1)
			if ph and ph > 0 and ph < 25 then PSTF_SetHp(p, 60) end
			-- no walking in place while waiting
			local _, _, _, _, _, path = PSTF_SpritePos(p, 1)
			local _, _, _, seq = anim(p)
			if p ~= cur and PSTF_TbcCurrent() ~= -1 and seq == 10 and path == 0 then
				walkInPlace[p] = (walkInPlace[p] or 0) + 1
				if walkInPlace[p] > 2 then return fail(string.format('%X walks in place while waiting', p)) end
			else
				walkInPlace[p] = 0
			end
		end
	end
	local id = PSTF_TbcCurrent()
	local newTurn = id ~= cur
	if newTurn then
		checkPending(true)
		if pstfTestTick == nil then return end
		cur = id
		turnFrame = frames
	end
	watchBuff(newTurn)
	if pstfTestTick == nil then return end
	checkPending(false)
	if pstfTestTick == nil then return end
	if id ~= -1 and party(id) >= 0 and frames == turnFrame + 3 then partyTurn(id) end
	if id ~= -1 and party(id) >= 0 and frames > turnFrame + 3 then
		local _, _, _, u, acting = PSTF_TbcCurrent()
		local _, _, _, _, _, path = PSTF_SpritePos(id, 1)
		if not acting and path == 0 and PSTF_Aoo() == -1 and frames - turnFrame > 30 then PSTF_EndTurn() end
	end
	if done.withdrawAnim and done.aoo and done.trip and done.rise and done.buff and not pending then
		local s = stats()
		PSTF_Note(string.format('tbc_moves2: ok (aoo %d/%d, takedowns %d/%d, knocked down %d, AI short runs %d)', s.aoo, s.aooSwung,
			s.takedowns, s.tdhits, s.prone, s.shortRuns))
		pstfTestTick = nil
		return
	end
	if frames > 14000 then
		local miss = ''
		for _, k in ipairs({'withdrawAnim', 'aoo', 'trip', 'rise', 'buff'}) do if not done[k] then miss = miss .. k .. ' ' end end
		fail('timeout; not done: ' .. miss)
	end
end
