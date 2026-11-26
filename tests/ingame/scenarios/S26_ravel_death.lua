-- scenario: save=000700161 env=PSTF_TBC_AUTOPARTY=1 timeout=600
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S26 Ravel dies in a fight (0610RAVL, AR0610): Die() -> StartCutSceneMode + 0610cut2. The fight ends or pauses; after the cutscene nobody is held.
local dead = 0
S.plot{name = 'Ravel', hp = 60, maxFrames = 5000, check = function(id)
	local _, _, _, _, alive = PSTF_SpritePos(id, 1)
	if PSTF_TbcCurrent() == -1 and S.frame > 200 then dead = dead + 1 end
	return dead > 150, 'fight over for ' .. dead .. ' ticks'
end}
