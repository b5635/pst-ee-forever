-- SPDX-License-Identifier: GPL-2.0-or-later
-- Full combat simulation for stability (Hive save 000000126): six Hive thugs turned hostile
-- against the party with party AI, round after round, every system on (round clock, held effects,
-- projectile hold, AI nudges, running, music, camera, HUD, dice, hotbar). Passes when 5 rounds go by
-- (or the fight ends) without a Lua error, engine fault or crash.
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_sim.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'TEST PASS|TEST FAIL' -Close -Timeout 1500
local enemies = {0x180018, 0x140014, 0x1C001C, 0x1E001E, 0x210021, 0x220022}
local frames, started, maxRound, turns, cur = 0, false, 0, 0, -1
function pstfTestTick()
	frames = frames + 1
	if frames == 8 then for _, e in ipairs(enemies) do PSTF_MakeEnemy(e) end end
	local id, r = PSTF_TbcCurrent()
	if id ~= -1 then started = true end
	if id ~= cur then cur = id turns = turns + 1 end
	if r > maxRound then maxRound = r PSTF_Note('round ' .. r .. ' after ' .. turns .. ' turns') end
	if started and id == -1 then
		PSTF_Note('TEST PASS: fight over after ' .. maxRound .. ' rounds, ' .. turns .. ' turns')
		pstfTestTick = nil
	elseif maxRound >= 6 then
		PSTF_Note('TEST PASS: 5 full rounds, ' .. turns .. ' turns')
		pstfTestTick = nil
	elseif frames > 40 * 60 * 20 then
		PSTF_Note('TEST FAIL: no progress') pstfTestTick = nil
	end
end
