-- SPDX-License-Identifier: GPL-2.0-or-later
-- Turn-based maneuvers (Hive save 000000126, party turns driven by this script, one hostile thug):
--   Run       31 ft -> 62 ft, the action is gone, a second Run is refused;
--   Withdraw  on a later turn: a third of the movement is left, the action is gone; walking away from the thug then
--             provokes no attack of opportunity;
--   AoO       a party member in the thug's reach walks away without Withdraw: "Attack of Opportunity!",
--             the thug swings once, the walk goes on;
--   Block     on the thug's second turn (if it is a fighter): block stacks; party swings at it try them;
--   Trip      a party member who may trip (fighter, not the Nameless One) arms it and attacks: the first
--             swing is a takedown (hit: save or prone, no damage).
-- PSTF_MoveStats() counters are the evidence; the combat log (MOVE/AOO lines) has the rolls.
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_moves.lua -Env @{PSTF_TEST_TICK='1'} -Until 'tbc_moves: ok|TEST FAIL' -Close -Timeout 400
local thug = 0x180018
local frames, cur, turnFrame = 0, -1, 0
local done = {}
local thugTurns = 0
local pending = nil  -- {kind, frame, stats before}
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function stats()
	local t = {}
	for n in string.gmatch(PSTF_MoveStats(), '(%-?%d+)') do table.insert(t, tonumber(n)) end
	return {runs = t[1], withdraws = t[2], blocks = t[3], tries = t[4], blocked = t[5], takedowns = t[6],
		tdhits = t[7], prone = t[8], aoo = t[9], aooSwung = t[10], ai = t[11], stood = t[12]}
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
local function walkAway(id)
	local ax, ay = PSTF_SpritePos(id, 1)
	local bx, by = PSTF_SpritePos(thug, 1)
	local dx, dy = ax - bx, ay - by
	local len = math.sqrt(dx * dx + dy * dy)
	if len < 1 then dx, dy, len = 1, 0, 1 end
	local x, y = math.floor(ax + dx / len * 200), math.floor(ay + dy / len * 150)
	PSTF_EvalAs(id, 'MoveToPoint([' .. x .. '.' .. y .. '])', 1)
end
local function ft() local _, _, _, _, _, f = PSTF_TbcCurrent() return f end
local function used() local _, _, _, u = PSTF_TbcCurrent() return u end

-- a party turn: do the next pending check that fits this member and where it stands
local function partyTurn(id)
	local near = cells(id, thug) <= 3.0  -- the engine reach of two personal-space-3 creatures (maneuvers.h ReachCells)
	if not done.run then
		local before = ft()
		if not PSTF_Maneuver('run') then return fail('Run refused at turn start') end
		local after = ft()
		PSTF_Note(string.format('tbc_moves: run %.1f -> %.1f ft, action used %s', before, after, tostring(used())))
		if math.abs(after - before - 31) > 0.01 or not used() then return fail('Run did not add 31 ft and take the action') end
		if PSTF_Maneuver('run') then return fail('a second Run was allowed') end
		done.run = true
		return PSTF_EndTurn()
	end
	if not done.withdraw and near then
		local before, s0 = ft(), stats()
		if not PSTF_Maneuver('withdraw') then return fail('Withdraw refused') end
		PSTF_Note(string.format('tbc_moves: withdraw %.1f -> %.1f ft', before, ft()))
		if math.abs(ft() - before / 3) > 0.01 or not used() then return fail('Withdraw did not leave a third of the movement and take the action') end
		walkAway(id)
		pending = {kind = 'withdraw', frame = frames, s0 = s0}
		done.withdraw = true
		return
	end
	if done.withdraw and not done.aoo and near then
		local s = PSTF_Stance(thug)
		if s:match('^%d+ %d+ %d+ %d+ 1') then return PSTF_EndTurn() end  -- the thug is prone: no reaction
		local x0, y0 = PSTF_SpritePos(id, 1)
		pending = {kind = 'aoo', frame = frames, s0 = stats(), mover = id, x0 = x0, y0 = y0, walkHeld = 0, moved = 0}
		walkAway(id)
		done.aoo = true
		return
	end
	if not done.trip then
		local c, sp = PSTF_Class(id)
		local fighter = (c == 2 or c == 6 or c == 7 or c == 8 or c == 9 or c == 10 or c == 12 or c == 16 or c == 17) and sp ~= 2
		if fighter then
			if not PSTF_Maneuver('trip') then return fail('Trip refused for a fighter (class ' .. c .. ')') end
			if not PSTF_Stance(id):match('^%d+ %d+ %d+ 1') then return fail('Trip not armed') end
			pending = {kind = 'trip', frame = frames, s0 = stats()}
			PSTF_EvalAs(id, 'Attack(NearestEnemyOf(Myself))', 1)
			done.trip = true
			return
		end
	end
	if done.blockSet and not done.blockHit then
		pending = {kind = 'block', frame = frames, s0 = stats()}
		PSTF_EvalAs(id, 'Attack(NearestEnemyOf(Myself))', 1)
		done.blockHit = true
		return
	end
	-- nothing to check with this one: walk next to the thug for the next checks
	PSTF_EvalAs(id, 'Attack(NearestEnemyOf(Myself))', 1)
end

local function checkPending(turnOver)
	if not pending then return end
	local s, s0 = stats(), pending.s0
	if pending.kind == 'withdraw' and turnOver then
		PSTF_Note(string.format('tbc_moves: withdraw walk: aoo %d -> %d', s0.aoo, s.aoo))
		if s.aoo ~= s0.aoo then return fail('a withdrawing walk provoked an attack of opportunity') end
		pending = nil
	elseif pending.kind == 'aoo' and (s.aoo > s0.aoo or turnOver) then
		if s.aoo == s0.aoo then
			PSTF_Note('tbc_moves: walk away provoked nothing (left reach? ' .. string.format('%.1f', cells(cur, thug)) .. ' cells)')
			done.aoo = false  -- try again on a later turn
			pending = nil
			return
		end
		local r, m = PSTF_Aoo()
		PSTF_Note(string.format('tbc_moves: attack of opportunity by %X on %X', r, m))
		if r ~= thug then return fail('the reactor is not the thug') end
		local p = pending
		pending = {kind = 'aooSwing', frame = frames, s0 = s0, mover = p.mover, x0 = p.x0, y0 = p.y0, walkHeld = 0, moved = 0}
	elseif pending.kind == 'aooSwing' then
		-- while the reactor swings the mover stands where it started the walk (turned toward its way), not walking in place
		if PSTF_Aoo() ~= -1 then
			local x, y = PSTF_SpritePos(pending.mover, 1)
			if math.abs(x - pending.x0) + math.abs(y - pending.y0) > 2 then pending.moved = pending.moved + 1 end
			local _, _, _, seq = PSTF_AnimProbe(pending.mover):match('(%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+)')
			if tonumber(seq) == 10 then pending.walkHeld = pending.walkHeld + 1 end
		end
		if s.aooSwung > s0.aooSwung or frames - pending.frame > 120 then
			local r = PSTF_Aoo()
			PSTF_Note(string.format('tbc_moves: aoo swung %d (reactor now %d); mover moved %d ticks, walk sequence %d ticks while waiting',
				s.aooSwung - s0.aooSwung, r, pending.moved, pending.walkHeld))
			if s.aooSwung == s0.aooSwung then return fail('the attack of opportunity never swung') end
			if pending.moved > 0 then return fail('the mover walked before the attack of opportunity') end
			if pending.walkHeld > 2 then return fail('the mover walked in place while waiting for the attack of opportunity') end
			pending = nil
		end
	elseif pending.kind == 'trip' and turnOver then
		PSTF_Note(string.format('tbc_moves: trip: takedowns %d, hits %d, prone %d', s.takedowns - s0.takedowns,
			s.tdhits - s0.tdhits, s.prone - s0.prone))
		if s.takedowns == s0.takedowns then
			done.trip = false  -- it never swung (out of reach / round over): again later
		end
		pending = nil
	elseif pending.kind == 'block' and turnOver then
		PSTF_Note(string.format('tbc_moves: block: tries %d, blocked %d', s.tries - s0.tries, s.blocked - s0.blocked))
		pending = nil
	end
end

function pstfTestTick()
	frames = frames + 1
	if frames == 5 then PSTF_MakeEnemy(thug) end
	local _, _, h = PSTF_SpritePos(thug, 1)
	if h and h > 0 and h < 60 then PSTF_SetHp(thug, 150) end
	for i = 0, 5 do
		local p = PSTF_PartyId(i)
		if p and p ~= -1 then
			local _, _, ph = PSTF_SpritePos(p, 1)
			if ph and ph > 0 and ph < 25 then PSTF_SetHp(p, 60) end
		end
	end
	local id = PSTF_TbcCurrent()
	if id ~= cur then
		checkPending(true)
		if pstfTestTick == nil then return end
		cur = id
		turnFrame = frames
		if cur ~= -1 and cur ~= id and stood == nil then stood = nil end
		if id == thug then
			thugTurns = thugTurns + 1
			PSTF_Note('tbc_moves: thug turn ' .. thugTurns .. ', state ' .. string.format('%X', PSTF_State(thug)) .. ', stance ' .. PSTF_Stance(thug))
			if thugTurns == 2 and not done.blockSet then
				local c, sp = PSTF_Class(thug)
				local ok = PSTF_Maneuver('block')
				PSTF_Note(string.format('tbc_moves: thug class %d specific %d, block %s, stance %s', c, sp, tostring(ok), PSTF_Stance(thug)))
				done.blockSet = true
				if not ok then done.blockHit = true end
			end
		end
	end
	-- standing up clears the prone state (helpless 0x20) a few ticks into its turn
	if id == thug and frames == turnFrame + 10 then
		local st = PSTF_State(thug)
		PSTF_Note(string.format('tbc_moves: thug state 10 ticks into its turn: %X', st))
		if st >= 0 and math.floor(st / 32) % 2 == 1 then return fail('still helpless after standing up') end
	end
	checkPending(false)
	if pstfTestTick == nil then return end
	if id ~= -1 and party(id) >= 0 and frames == turnFrame + 3 then partyTurn(id) end
	-- a party turn ends once its order is done (movement or action left over)
	if id ~= -1 and party(id) >= 0 and frames > turnFrame + 3 then
		local _, _, _, u, acting = PSTF_TbcCurrent()
		local _, _, _, _, _, path = PSTF_SpritePos(id, 1)
		if not acting and path == 0 and PSTF_Aoo() == -1 and frames - turnFrame > 30 then PSTF_EndTurn() end
	end
	if done.run and done.withdraw and done.aoo and done.trip and done.blockHit and not pending then
		local s = stats()
		PSTF_Note(string.format('tbc_moves: ok (runs %d, withdraws %d, blocks %d/%d/%d, takedowns %d/%d, prone %d, aoo %d/%d, ai %d, stood %d)',
			s.runs, s.withdraws, s.blocks, s.tries, s.blocked, s.takedowns, s.tdhits, s.prone, s.aoo, s.aooSwung, s.ai, s.stood))
		pstfTestTick = nil
		return
	end
	if frames > 5000 then
		local miss = ''
		for _, k in ipairs({'run', 'withdraw', 'aoo', 'trip', 'blockHit'}) do if not done[k] then miss = miss .. k .. ' ' end end
		fail('timeout; not done: ' .. miss)
	end
end
