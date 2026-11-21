-- SPDX-License-Identifier: GPL-2.0-or-later
-- What objects exist while a spell flies? Party member 2 casts Adder's Kiss and Ice Knife on Player1.
local n = 0
function pstfTestTick()
	n = n + 1
	if n == 5 then C:Eval('ForceSpell(Player1,2201)', 1) end
	if n == 150 then C:Eval('ForceSpell(Player1,2207)', 1) end
	if n % 4 == 0 and n < 300 then
		local k = PSTF_Objects(-3)
		if k > 0 then PSTF_Note('frame ' .. n .. ': ' .. k .. ' new objects') end
	end
	if n == 300 then PSTF_Note('probe done') pstfTestTick = nil end
end
