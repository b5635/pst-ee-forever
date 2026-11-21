-- SPDX-License-Identifier: GPL-2.0-or-later
-- Which ForceSpell forms work out of combat? (party member 2 = slot 1 cast Blur on itself before)
local P = {PSTF_PartyId(0), PSTF_PartyId(1), PSTF_PartyId(2)}
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h end
local n = 0
local last = {}
local steps = {
	{10, function() C:Eval('ForceSpell(Player1,2216)', 1) end, 'blur on Player1 by slot 1'},
	{100, function() PSTF_Note('tno blur ' .. PSTF_HasEffect(P[1], 65, 1)) end, ''},
	{110, function() C:Eval('ForceSpell(Myself,2201)', 1) end, 'adder on self by slot 1'},
	{230, function() C:Eval('ForceSpell(Player1,2201)', 1) end, 'adder on Player1 by slot 1'},
	{350, function() C:Eval('ForceSpellRES("SPWI201",Player1)', 1) end, 'adder RES on Player1 by slot 1'},
	{470, function() C:Eval('Spell(Player1,2201)', 1) end, 'Spell adder Player1'},
}
function pstfTestTick()
	n = n + 1
	for _, s in ipairs(steps) do if s[1] == n then s[2]() if s[3] ~= '' then PSTF_Note('step: ' .. s[3]) end end end
	for _, w in ipairs(P) do
		local h = hp(w)
		if last[w] and h ~= last[w] then PSTF_Note(string.format('hp %d %d -> %d at frame %d', w, last[w], h, n)) end
		last[w] = h
	end
	if n == 600 then PSTF_Note('probe done') pstfTestTick = nil end
end
