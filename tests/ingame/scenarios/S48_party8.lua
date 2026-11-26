-- scenario: save=000000200 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S48 8-member party (test save 200) against 6 spawned Starved Dog thugs: party slots 7 and 8 get turns, every turn
-- completes, the fight ends. The thugs get 60 HP so the fight lasts long enough for every slot.
local before, more, seen = nil, {}, {}
S.steps[2] = function() before = S.ids() end
S.steps[3] = function() for i = 2, 6 do S.spawn('SDTHUG' .. i, 160 + 30 * i, 20 * i) end end
S.steps[8] = function()
	local party = {}
	for i = 0, 7 do local p = PSTF_PartyId(i) if p and p ~= -1 then party[p] = true end end
	for id in pairs(S.ids()) do
		if not before[id] and not party[id] then more[#more + 1] = id PSTF_SetHp(id, 60, 60) PSTF_MakeEnemy(id) end
	end
	S.note(#more .. ' spawned thugs made hostile')
end
S.brawl{name = 'SDTHUG1', cre = 'SDTHUG1', spawn = true, provoke = false, hostile = true, hp = 60, maxFrames = 9000}
local slot7, slot8
local inner = S.tick
S.tick = function(frame, cur, started)
	local p7, p8 = PSTF_PartyId(6), PSTF_PartyId(7)
	if cur and cur ~= -1 and cur == p7 and not slot7 then slot7 = frame S.note('party slot 7 has its turn at tick ' .. frame) end
	if cur and cur ~= -1 and cur == p8 and not slot8 then slot8 = frame S.note('party slot 8 has its turn at tick ' .. frame) end
	if frame == S.maxFrames - 60 and not (slot7 and slot8) then S.issue('party slot 7 or 8 never had a turn') end
	return inner(frame, cur, started)
end
local finish = S.finish
S.finish = function(why)
	if not (slot7 and slot8) then S.issue('the fight ended before party slot 7 and 8 both had a turn') end
	return finish(why)
end
