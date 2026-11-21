-- SPDX-License-Identifier: GPL-2.0-or-later
-- Turn-based HUD with MANUAL party turns: stages a fight like tbc_fight.lua, then points the hover
-- preview (PSTF_PreviewAt, instead of the mouse) at a walkable spot about 45 ft from the protagonist,
-- past the enemy, so the line shows a gold part (31 ft) and a red part. Nobody gives orders, so the
-- protagonist's turn waits; screenshots meanwhile:
--   tools\launch.ps1 -Lua tests\ingame\tbc_hud.lua -Env @{PSTF_SHOT_DIR='<dir>'; PSTF_SHOT_EVERY_MS='1500'} -Until 'shot_004' -Close
-- Log: "tbc: hover preview ...", "tbc: selection locked to ...".
local id = PSTF_MakeEnemyNearest()
if id == -1 then error("no creature to make hostile") end
local ex, ey = PSTF_SpritePos(id)
local px, py = PSTF_SpritePos(PSTF_PartyId(0))
-- Unit direction from the protagonist to the enemy, in feet (16 px across, 12 px down).
-- A reachable spot (PSTF_PathFt: same search as the hover preview) 40-60 ft of walking away, as high
-- on screen as possible (clear of the bottom panel), so the line has a gold and a red part.
local best = nil
for d = 50, 30, -5 do
	for a = 0, 330, 30 do
		local r = math.rad(a)
		local tx, ty = math.floor(px + math.cos(r) * d * 16), math.floor(py + math.sin(r) * d * 12)
		if (not best or ty < best[2]) and PSTF_WalkCost(tx, ty) < 255 then
			local ft = PSTF_PathFt(tx, ty)
			if ft >= 40 and ft <= 70 then best = {tx, ty} end
		end
	end
end
if not best then error("no reachable preview target") end
PSTF_PreviewAt(best[1], best[2])
C:Eval('MoveToPoint([' .. math.floor(ex) .. '.' .. math.floor(ey + 40) .. '])', 0)
