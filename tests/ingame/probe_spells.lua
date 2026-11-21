-- SPDX-License-Identifier: GPL-2.0-or-later
-- Out of combat: do these casts work at all? (no fight: nobody is hostile)
local P = {PSTF_PartyId(0), PSTF_PartyId(1), PSTF_PartyId(2)}
local enemy = 0x320032
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h end
local n = 0
local last = {}
function pstfTestTick()
	n = n + 1
	if n == 5 then PSTF_EvalAs(enemy, 'ReallyForceSpell(Player2,2201)', 1) end
	if n == 120 then local x, y = PSTF_SpritePos(enemy, 1) C:Eval('ReallyForceSpellPoint([' .. math.floor(x) .. '.' .. math.floor(y) .. '],2502)', 1) end
	if n == 240 then C:Eval('ReallyForceSpell(Player3,2207)', 0) end
	if n == 360 then PSTF_EvalAs(enemy, 'ReallyForceSpell(Player1,2310)', 1) end
	for _, w in ipairs({P[1], P[2], P[3], enemy}) do
		local h = hp(w)
		if last[w] and h ~= last[w] then PSTF_Note(string.format('hp %d %d -> %d at frame %d', w, last[w], h, n)) end
		last[w] = h
	end
	if n % 20 == 0 then
		for _, w in ipairs({P[1], P[2], enemy}) do
			local _, _, _, _ = PSTF_SpritePos(w, 1)
		end
		PSTF_Note('frame ' .. n .. ' enemy obj ' .. PSTF_Objects(enemy) .. ' p2 obj ' .. PSTF_Objects(P[2]) .. ' p1 obj ' .. PSTF_Objects(P[1]))
	end
	if n == 480 then PSTF_Note('probe done') pstfTestTick = nil end
end
