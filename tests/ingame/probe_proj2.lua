-- SPDX-License-Identifier: GPL-2.0-or-later
-- Real time (PSTF_TBC_NOFIGHT=1): the fields of Ice Knife and Cone of Cold projectiles.
local n = 0
function pstfTestTick()
	n = n + 1
	if n == 3 then PSTF_MakeEnemy(0x320032) end
	if n == 10 then C:Eval('ReallyForceSpellLevel(NearestEnemyOf(Myself),2207,12)', 1) end
	if n == 150 then C:Eval('ReallyForceSpellLevel(NearestEnemyOf(Myself),2502,12)', 1) end
	if (n == 14 or n == 30 or n == 154 or n == 170 or n == 220) then PSTF_Objects(-3) end
	if n == 300 then PSTF_Note('probe done') pstfTestTick = nil end
end
