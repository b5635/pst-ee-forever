-- SPDX-License-Identifier: GPL-2.0-or-later
-- HUD look: initiative portraits with red damage fill, hover card with hit chance over the enemy,
-- the dice panel for an attack (party member 3 attacks) and a saving throw (probe roll, plays the
-- save sound). Screenshots: tools\launch.ps1 -Lua tests\ingame\tbc_hud2.lua -Env @{PSTF_TEST_TICK='1';
-- PSTF_SHOT_DIR='<dir>'; PSTF_SHOT_EVERY_MS='1000'} -Until 'hud done' -Close
local enemy = 0x320032
local P = {PSTF_PartyId(0), PSTF_PartyId(1), PSTF_PartyId(2)}
local frames, cur, turnStart = 0, -1, 0
local saw = {}
function pstfTestTick()
	frames = frames + 1
	if frames == 3 then C:Eval('ApplyDamage(Myself,20,0)', 0) end
	if frames == 8 then PSTF_MakeEnemy(enemy) end
	local id, r, party = PSTF_TbcCurrent()
	if id ~= cur then cur, turnStart = id, frames end
	local x, y = PSTF_SpritePos(enemy, 1)
	PSTF_HoverAt(x, y)  -- inside its selection circle
	if id == P[3] and frames - turnStart == 5 and not saw.attack then
		saw.attack = true
		C:Eval('Attack(NearestEnemyOf(Myself))', 2)
	end
	if id == P[1] and frames - turnStart == 5 and not saw.save then
		saw.save = true
		PSTF_RollProbe(1, 0, 4)
	end
	if party and frames - turnStart > 140 then PSTF_EndTurn() end
	if saw.attack and saw.save and frames - turnStart == 400 then PSTF_Note('hud done') pstfTestTick = nil end
end
