-- SPDX-License-Identifier: GPL-2.0-or-later
-- Probe (Hive save 000000126): Dak'kon's effect list frame by frame while he casts Adder's Kiss in
-- his first turn: where does the op374 casting feature (3 s delay) live and how is it timed?
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\probe_adder.lua -Env @{PSTF_TEST_TICK='1'} -Until 'PROBE DONE' -Close
local dak = PSTF_PartyId(1)
local frames, cast, turn0 = 0, -1, nil
function pstfTestTick()
	frames = frames + 1
	if frames == 2 then PSTF_EvalAs(dak, 'AddXPObject(Myself,3000000)') end
	if frames == 20 then for _, t in ipairs({0x180018, 0x140014, 0x1C001C}) do PSTF_MakeEnemy(t) end end
	local id, r = PSTF_TbcCurrent()
	if id == dak and turn0 == nil then turn0 = frames end
	if id == dak and cast == -1 and frames - turn0 == 5 then
		cast = frames
		PSTF_EvalAs(dak, 'SpellNoDec(NearestEnemyOf(Myself),2201)')
	end
	if cast ~= -1 and (frames - cast) % 6 == 0 and frames - cast <= 160 then
		PSTF_Note('+' .. (frames - cast) .. ' world ' .. PSTF_WorldTicks())
		PSTF_Effects(dak)
		PSTF_Objects(dak)
	end
	if cast ~= -1 and frames - cast == 170 then PSTF_Note('PROBE DONE') pstfTestTick = nil end
	if id ~= -1 and id ~= dak and frames > 22 then PSTF_EndTurn() end
end
