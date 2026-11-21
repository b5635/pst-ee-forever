-- SPDX-License-Identifier: GPL-2.0-or-later
-- Full combat simulation for stability: the Dead Nations undead (about 18 of them once one is hit)
-- against the party with party AI, round after round, every system on (round clock, held effects,
-- projectile hold, AI nudges, running, music, camera, HUD, dice, hotbar). Passes when 5 rounds go by
-- (or the fight ends) without a Lua error, engine fault or crash.
--   tools\launch.ps1 -Lua tests\ingame\tbc_sim.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'TEST PASS|TEST FAIL' -Close -Timeout 1500
local enemy = 0x320032
local frames, started, maxRound, turns, cur = 0, false, 0, 0, -1
function pstfTestTick()
	frames = frames + 1
	if frames == 8 then PSTF_MakeEnemy(enemy) end
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
