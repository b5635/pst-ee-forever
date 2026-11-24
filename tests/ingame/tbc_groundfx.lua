-- SPDX-License-Identifier: GPL-2.0-or-later
-- Maneuver ground effects (dust and smoke puffs) for a look: every effect plays at the Nameless One's feet in
-- turn, with screenshots (env PSTF_SHOT_DIR / PSTF_SHOT_EVERY_MS). No fight.
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_groundfx.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'; PSTF_SHOT_DIR='<dir>'; PSTF_SHOT_EVERY_MS='150'} -Until 'tbc_groundfx: ok|TEST FAIL' -Close
local frames, tno = 0, nil
function pstfTestTick()
	frames = frames + 1
	if frames == 3 then tno = PSTF_PartyId(0) end
	if not tno then return end
	for k = 0, 4 do
		if frames == 20 + k * 90 then
			if k == 1 or k == 3 then  -- trails: a few in a row, as while walking
				for i = 0, 3 do PSTF_GroundFx(k, tno) end
			else
				PSTF_GroundFx(k, tno)
			end
			PSTF_Note('tbc_groundfx: kind ' .. k)
		end
	end
	if frames == 20 + 5 * 90 then PSTF_Note('tbc_groundfx: ok') pstfTestTick = nil end
end
