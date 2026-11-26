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
	-- Walkthrough saves can open inside a party banter dialogue (the world waits for it): step it closed first.
	if S.frame <= 24 and S.frame % 3 == 1 and worldScreen and worldScreen.StepDialog and not S.keepDialog then
		pcall(worldScreen.StepDialog, worldScreen)
	end
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

-- Step-counter casters (section A): a creature whose script walks a counter (LOCALS Scd_int_0 1..N or a global),
-- one Spell per step. o = {name = script/shown name, cre = CRE to spawn when absent, script = BCS to give it,
-- var = 'cd_int_0', scope = 'LOCALS' | area | 'GLOBAL', start = 1, last = N (finish when reached), hp = 400,
-- setup = fn(id) extra forcing, maxFrames}. Checks: the counter climbs to `last`; at most one cast per own turn
-- (PSTF_Casts); every own turn noted with the counter value.
function S.stepCaster(o)
	o.var = o.var or 'cd_int_0'  -- BCS "LOCALScd_int_0" = scope LOCALS + name cd_int_0
	o.scope = o.scope or 'LOCALS'
	o.start = o.start or 1
	if o.near == nil then o.near = {200, 40} end
	S.maxFrames = o.maxFrames or 6000
	local id, top, turns, casts0 = -1, nil, 0, 0
	local function val()
		for v = (top or o.start), o.last + 2 do
			if S.global(o.var, o.scope, v, o.scope == 'LOCALS' and id or nil) then return v end
		end
		return top
	end
	S.steps[26] = function()
		id = S.find(o.name)
		if id == -1 and o.cre then S.spawn(o.cre, 200, 40) end
	end
	S.steps[29] = function()
		if id == -1 then id = S.find(o.name) end
		if id == -1 and o.cre then id = S.find(o.cre) end
		if id == -1 then return S.fail(o.name .. ' not found') end
		S.caster = id
		S.note(string.format('%s is %X', o.name, id))
		S.watch[id] = true
		-- one action per C:Eval (it runs only the first of several); the first clears its queue
		local acts = {}
		if o.script then acts[#acts + 1] = 'ChangeAIScript("' .. o.script .. '",0)' end
		if o.scope == 'LOCALS' then acts[#acts + 1] = 'SetGlobal("' .. o.var .. '","LOCALS",' .. o.start .. ')' end
		if o.near then
			local x, y = PSTF_SpritePos(S.tno(), 1)
			acts[#acts + 1] = 'JumpToPoint([' .. math.floor(x + o.near[1]) .. '.' .. math.floor(y + o.near[2]) .. '])'
		end
		for i, a in ipairs(acts) do PSTF_EvalAs(id, a, i == 1 and 1 or 0) end
		if o.go then  -- the party goes to it (a creature whose AI is idle off-screen does not jump)
			local x, y = S.pos(id)
			for i = 0, 7 do
				local p = PSTF_PartyId(i)
				if p and p ~= -1 then PSTF_EvalAs(p, 'JumpToPoint([' .. math.floor(x + o.go[1] + 30 * i) .. '.' .. math.floor(y + o.go[2]) .. '])', 1) end
			end
		end
		if o.scope ~= 'LOCALS' then PSTF_EvalAs(S.tno(), 'SetGlobal("' .. o.var .. '","' .. o.scope .. '",' .. o.start .. ')') end
	end
	S.steps[60] = function()
		local x, y = S.pos(id)
		local tx, ty = S.pos(S.tno())
		S.note(string.format('%X at %d,%d, protagonist at %d,%d, queue [%s]', id, x or -1, y or -1, tx, ty, PSTF_Queue(id, 1)))
		PSTF_SetHp(id, o.hp or 3000, o.hp or 3000)
		for i = 0, 7 do local p = PSTF_PartyId(i) if p and p ~= -1 then PSTF_SetHp(p, 400, 400) end end
		if not S.global(o.var, o.scope, o.start, o.scope == 'LOCALS' and id or nil) then return S.fail('could not set ' .. o.var) end
		top = o.start
		if o.setup then o.setup(id) end
		PSTF_MakeEnemy(id)
	end
	local lastCur2 = -2
	S.tick = function(frame, cur)
		if not top then return end
		local v = val()
		if v and v ~= top then
			S.note(string.format('%s = %d at tick %d (its turn %d)', o.var, v, frame, turns))
			top = v
		end
		if cur ~= lastCur2 then
			if lastCur2 == id then
				local c = PSTF_Casts(id)
				if c - casts0 > 1 then S.issue(string.format('%d casts in one turn (turn %d)', c - casts0, turns)) end
			end
			if cur == id then turns = turns + 1 casts0 = PSTF_Casts(id) end
			lastCur2 = cur
		end
		if top >= o.last then return S.finish(string.format('%s reached %d in %d of its turns', o.var, top, turns)) end
		if frame == S.maxFrames - 50 then S.issue(string.format('%s stuck at %d after %d of its turns (expected %d)', o.var, top, turns, o.last)) end
	end
end

-- Chain casters (section A: Spell; Wait; Spell; ... in one response, no counter). o = {name, cre, script, hp, near,
-- go, pre = {BCS actions on it first}, set = {{name, scope, v}, ...} (globals set through the protagonist),
-- casts = K (expected spells), turns = T (own turns to watch), done = {name, scope, v} (finish when it holds)}.
-- Checks: at most one cast per own turn; at least K casts in T own turns (or `done` holds).
function S.chainCaster(o)
	if o.near == nil and not o.go then o.near = {200, 40} end
	S.maxFrames = o.maxFrames or 9000
	local id, turns, casts0, total, started = -1, 0, 0, 0, false
	S.steps[26] = function()
		id = S.find(o.name)
		if id == -1 and o.cre then S.spawn(o.cre, o.near and o.near[1] or 200, o.near and o.near[2] or 40) end
	end
	S.steps[29] = function()
		if id == -1 then id = S.find(o.name) end
		if id == -1 and o.cre then id = S.find(o.cre) end
		if id == -1 then return S.fail(o.name .. ' not found') end
		S.caster = id
		S.note(string.format('%s is %X', o.name, id))
		S.watch[id] = true
		local acts = {}
		if o.script then acts[#acts + 1] = 'ChangeAIScript("' .. o.script .. '",0)' end
		for _, a in ipairs(o.pre or {}) do acts[#acts + 1] = a end
		if o.near and not o.cre then
			local x, y = PSTF_SpritePos(S.tno(), 1)
			acts[#acts + 1] = 'JumpToPoint([' .. math.floor(x + o.near[1]) .. '.' .. math.floor(y + o.near[2]) .. '])'
		end
		for i, a in ipairs(acts) do PSTF_EvalAs(id, a, i == 1 and 1 or 0) end
		for _, g in ipairs(o.set or {}) do PSTF_EvalAs(S.tno(), 'SetGlobal("' .. g[1] .. '","' .. g[2] .. '",' .. g[3] .. ')') end
		if o.go then
			local x, y = S.pos(id)
			for i = 0, 7 do
				local p = PSTF_PartyId(i)
				if p and p ~= -1 then PSTF_EvalAs(p, 'JumpToPoint([' .. math.floor(x + o.go[1] + 30 * i) .. '.' .. math.floor(y + o.go[2]) .. '])', 1) end
			end
		end
	end
	S.steps[60] = function()
		PSTF_SetHp(id, o.hp or 3000, o.hp or 3000)
		for i = 0, 7 do local p = PSTF_PartyId(i) if p and p ~= -1 then PSTF_SetHp(p, 400, 400) end end
		if o.setup then o.setup(id) end
		if o.hostile ~= false then PSTF_MakeEnemy(id) end
		started = true
	end
	local last = -2
	S.tick = function(frame, cur)
		if not started then return end
		if o.done and S.global(o.done[1], o.done[2], o.done[3]) then
			return S.finish(string.format('%s = %d after %d of its turns, %d casts', o.done[1], o.done[3], turns, total))
		end
		if cur ~= last then
			if last == id then
				local c = PSTF_Casts(id) - casts0
				total = total + c
				S.note(string.format('its turn %d: %d cast(s), %d so far', turns, c, total))
				if c > 1 then S.issue(string.format('%d casts in one turn (turn %d)', c, turns)) end
			end
			if cur == id then turns = turns + 1 casts0 = PSTF_Casts(id) end
			last = cur
		end
		if o.trace and cur == id and frame % 25 == 0 then PSTF_Action(id) PSTF_Queue(id) end
		if turns > (o.turns or 8) and cur ~= id then
			if total < (o.casts or 1) then S.issue(string.format('%d casts in %d of its turns (expected %d)', total, turns - 1, o.casts)) end
			return S.finish(string.format('%d casts in %d of its turns', total, turns - 1))
		end
		if frame == S.maxFrames - 50 then S.issue(string.format('only %d of its turns, %d casts', turns, total)) end
	end
end
