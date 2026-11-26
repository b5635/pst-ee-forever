-- scenario: save=000700052 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S12 Four apprentices (3012APR1..4, AR3012 actors spawned here): each a step caster with LOCALS cd_int_0
-- 1 -> 2 (Spell(Myself,2102); Wait(1); IncrementGlobal) -> 3 (Spell([EA=2],2101); Wait(1); IncrementGlobal), then melee.
-- Four counters in one fight: every apprentice must reach 3 (one step per turn), none stuck behind its Wait.
local before, extra, done = nil, {}, {}
S.steps[32] = function() before = S.ids() end
S.steps[33] = function() for i = 2, 4 do S.spawn('APPRENT' .. i, 160 + 40 * i, 30 * i) end end
S.steps[44] = function()
	local party = {}
	for i = 0, 7 do local p = PSTF_PartyId(i) if p and p ~= -1 then party[p] = true end end
	for id in pairs(S.ids()) do
		if not before[id] and not party[id] then extra[#extra + 1] = id end
	end
	for k, id in ipairs(extra) do
		PSTF_EvalAs(id, 'ChangeAIScript("3012APR' .. (k + 1) .. '",0)', 1)
		PSTF_SetHp(id, 300, 300)
	end
	S.note(#extra .. ' more apprentices')
end
S.steps[46] = function() for _, id in ipairs(extra) do PSTF_EvalAs(id, 'SetGlobal("cd_int_0","LOCALS",1)') end end
S.steps[48] = function() for _, id in ipairs(extra) do S.watch[id] = true PSTF_MakeEnemy(id) end end
S.stepCaster{name = 'APPRENT', cre = 'APPRENT', spawn = true, script = '3012APR1', last = 3, maxFrames = 10000}
local inner = S.tick
S.tick = function(frame, cur, started)
	for _, id in ipairs(extra) do
		if not done[id] and S.global('cd_int_0', 'LOCALS', 3, id) then done[id] = frame S.note(string.format('apprentice %X reached 3 at tick %d', id, frame)) end
	end
	return inner(frame, cur, started)
end
local finish = S.finish
S.finish = function(why)
	local n = 0
	for _, id in ipairs(extra) do if done[id] then n = n + 1 end end
	if n < #extra and why ~= 'timeout' and S.frame < S.maxFrames - 60 then return end  -- the others are slower: wait
	if n < #extra then S.issue(string.format('%d of %d other apprentices reached step 3', n, #extra)) end
	return finish(why)
end
