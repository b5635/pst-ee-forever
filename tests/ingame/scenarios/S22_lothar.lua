-- scenario: save=000700091 env=PSTF_TBC_AUTOPARTY=1 timeout=500
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S22 Lothar (0508LOTH, AR0508): attacked -> PlaySequence(3), Kill(Player1): the Nameless One dies and wakes in the Mortuary; the fight ends cleanly (no fault, nobody held).
local ran, after = false, 0
S.plot{name = 'Lothar', hp = 30000, maxFrames = 4000, check = function(id)
	local cur = PSTF_TbcCurrent()
	if cur ~= -1 then ran = true end
	if ran and cur == -1 then after = after + 1 end
	return after > 90, 'fight over ' .. after .. ' ticks (ran ' .. tostring(ran) .. ')'
end}
