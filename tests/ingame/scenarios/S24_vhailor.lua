-- scenario: save=000700178 env=PSTF_TBC_AUTOPARTY=1 timeout=500
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S24 Vhailor (0702VHAI, AR0702): attacked -> Polymorph(61511), Enemy, Attack(LastAttackerOf). The card and name follow the polymorph.
local anim0
S.plot{name = 'Vhailor', maxFrames = 4000, check = function(id)
	local a = PSTF_AnimId(id, 1)
	anim0 = anim0 or a
	return a ~= anim0, 'animation ' .. tostring(anim0) .. ' -> ' .. tostring(a)
end}
