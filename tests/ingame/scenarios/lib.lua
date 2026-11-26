-- SPDX-License-Identifier: GPL-2.0-or-later
-- Shared driver for the scenario tests (docs/SCENARIO_TEST_PLAN.md). tools\scenario.ps1 puts this file in front of
-- a scenario file; the scenario fills `S.steps` (frame -> function) and optional `S.tick(frame)` / `S.done()`.
-- Needs env PSTF_TEST_TICK=1 (pstfTestTick runs on world ticks). Every scenario ends with one log line:
--   scenario <ID>: ok | scenario <ID>: ISSUE <text> | TEST FAIL <text>
-- Invariants checked on every tick while a fight runs (each is an ISSUE, not a crash):
--   held      a combatant whose turn it is not moves more than 6 px (it should stand still)
--   stuck     one turn lasts more than S.turnCap ticks (default 900 = 60 s)
--   endless   the fight passes S.roundCap rounds (default 40)
S = S or {}
S.id = S.id or 'S??'
S.steps = S.steps or {}
S.turnCap = S.turnCap or 900
S.roundCap = S.roundCap or 40
S.maxFrames = S.maxFrames or 15000
S.issues = {}
S.frame = 0
S.watch = {}      -- ids whose queue/timers are logged at each turn change
S.lastPos = {}
local finished = false

function S.hx(v) return (v and v >= 0) and string.format('%X', v) or tostring(v) end
function S.note(t) PSTF_Note('scenario ' .. S.id .. ': ' .. t) end
function S.issue(t)
	if S.issues[t] then return end
	S.issues[t] = true
	S.issues[#S.issues + 1] = t
	S.note('ISSUE ' .. t)
end
function S.finish(why)
	if finished then return end
	finished = true
	pstfTestTick = nil
	S.note('finished (' .. why .. ') after ' .. S.frame .. ' ticks, ' .. #S.issues .. ' issue(s)')
	if #S.issues > 0 then S.note('ISSUE summary: ' .. table.concat(S.issues, ' | ')) else S.note('ok') end
end
function S.fail(t)
	if finished then return end
	finished = true
	pstfTestTick = nil
	PSTF_Note('TEST FAIL: scenario ' .. S.id .. ': ' .. t)
end
function S.tno() return PSTF_PartyId(0) end
function S.pos(id) local x, y = PSTF_SpritePos(id, 1) return x, y end
-- Moves `id` next to the protagonist (dx, dy px from it).
function S.near(id, dx, dy)
	local x, y = PSTF_SpritePos(S.tno(), 1)
	PSTF_EvalAs(id, 'JumpToPoint([' .. math.floor(x + dx) .. '.' .. math.floor(y + dy) .. '])', 1)
end
-- Spawns CRE `res` dx, dy px from the protagonist (CreateCreature through the protagonist's queue: it exists a tick
-- later; find it with S.find(its script name) in a later step).
function S.spawn(res, dx, dy)
	local x, y = PSTF_SpritePos(S.tno(), 1)
	PSTF_EvalAs(S.tno(), 'CreateCreature("' .. res .. '",[' .. math.floor(x + dx) .. '.' .. math.floor(y + dy) .. '],0)')
end
-- A creature in the party's area by script name, else by its shown (TLK) name; nth (1-based) among equals.
function S.find(name, nth)
	nth = nth or 1
	local id = PSTF_FindActor(name, nth)
	if id ~= -1 then return id end
	local k = 0
	for sid, ref in string.gmatch(PSTF_ActorRefs(), '(%-?%d+),(%-?%d+);') do
		local r = tonumber(ref)
		if r and r >= 0 and Infinity_FetchString(r) == name then
			k = k + 1
			if k == nth then return tonumber(sid) end
		end
	end
	return -1
end
-- Global("NAME","GLOBAL"|area, v) as the engine reads it (scope: 'GLOBAL', 'LOCALS' (of `who`) or an area resref).
function S.global(name, scope, v, who)
	return PSTF_Trigger(who or S.tno(), 0x400F, v, '', (scope or 'GLOBAL') .. name, 1) == 1
end
function S.inFight(id)
	for i = 1, 30 do
		local f = PSTF_FightId(i)
		if f == -1 then return false end
		if f == id then return true end
	end
	return false
end
function S.fighters()
	local n = 0
	for i = 1, 30 do if PSTF_FightId(i) == -1 then break end n = n + 1 end
	return n
end
function S.hostile(id) PSTF_MakeEnemy(id) end

local lastCur, turnStart, started, lastRound = -2, 0, false, 0
function pstfTestTick()
	if finished then return end
	S.frame = S.frame + 1
	local f = S.steps[S.frame]
	if f then
		local ok, err = pcall(f)
		if not ok then return S.fail('step ' .. S.frame .. ': ' .. tostring(err)) end
		if finished then return end
	end
	local cur, round = PSTF_TbcCurrent()
	if cur ~= -1 then started = true end
	if cur ~= lastCur then
		if lastCur ~= -2 then
			for id in pairs(S.watch) do
				local q = PSTF_Queue(id, 1)
				local t = PSTF_Timers(id, 1)
				S.note(string.format('turn %s -> %s (round %d): watched %s queue [%s] timers [%s]', S.hx(lastCur), S.hx(cur), round or 0, S.hx(id), q, t))
			end
		end
		lastCur, turnStart = cur, S.frame
		S.lastPos = {}
	end
	if cur ~= -1 then
		if S.frame - turnStart > S.turnCap then S.issue(string.format('stuck: turn of %s runs > %d ticks', S.hx(cur), S.turnCap)) end
		if round and round > S.roundCap then S.issue('endless: fight passed round ' .. S.roundCap) end
		for i = 1, 30 do
			local id = PSTF_FightId(i)
			if id == -1 then break end
			if id ~= cur then
				local x, y = PSTF_SpritePos(id, 1)
				local p = S.lastPos[id]
				if p and x and math.abs(x - p[1]) + math.abs(y - p[2]) > 6 then
					S.issue(string.format('held: %s moved outside its turn (%d,%d -> %d,%d)', S.hx(id), p[1], p[2], x, y))
				end
				if x and not p then S.lastPos[id] = {x, y} end
			end
		end
	end
	if S.tick then
		local ok, err = pcall(S.tick, S.frame, cur, started)
		if not ok then return S.fail('tick: ' .. tostring(err)) end
	end
	if finished then return end
	if S.frame >= S.maxFrames then
		if S.done then pcall(S.done, 'timeout') end
		return S.finish('timeout')
	end
end
