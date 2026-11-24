-- SPDX-License-Identifier: GPL-2.0-or-later
-- README recording: tests/ingame/lighting_hammer.lua with night off/lit runs of 200 frames, starting after 15 s (launch overlays).
pstfHammerRuns = {{'night','off'},{'night','lit'},{'night','off'},{'night','lit'}}
-- Spell glow by night and by day (all-spells save 000000990, hour 1): Grace casts Spiritual Hammer (SPPR204) at a
-- Hive thug four times: at night with our lighting, at night with it off (the engine's own look), then by day
-- (DayNight(12)) lit and off. Shots of the flight: pst_forever/logs/hammer_<night|day>_<lit|off>_<n>.bmp.
-- PSTF_LightFxCells() logs where the effect's cells are (window px rects) for the comparison.
--   tools\launch.ps1 -Save 000000990 -Lua tests\ingame\lighting_hammer.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'lighting_hammer: ok|TEST FAIL' -Close -Timeout 300
local f, grace, foe = 0, nil, 0x180018
local runs = {{'night', 'lit'}, {'night', 'off'}, {'day', 'lit'}, {'day', 'off'}}
if pstfHammerRuns then runs = pstfHammerRuns
elseif pstfHammerAB then  -- A/B of the glow stages at night (set by a prefix line)
	runs = {{'night', 'lit'}, {'night', 'nofxlight', 'fx_lights', 0}, {'night', 'nobloom', 'bloom', 0}, {'night', 'gain100', 'night_light_gain', 100},
		{'night', 'noemis', 'emissive', 0}, {'night', 'off'}, {'night', 'mask'}, {'day', 'lit'}, {'day', 'off'}, {'day', 'mask'}}
end
local run, start = 1, 450
local waitDay, todNight = nil, nil
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function slotOf(name)
	local i = 0
	for v in PSTF_PartyNames():gmatch('[^,]+') do
		if v == name then return i end
		i = i + 1
	end
	return -1
end
function pstfTestTick()
	f = f + 1
	if f == 3 then
		local s = slotOf('Grace')
		if s < 0 then return fail('no Grace: use the all-spells save') end
		grace = PSTF_PartyId(s)
		PSTF_MakeEnemy(foe)
		todNight = PSTF_LightGlobal()
		local fx, fy = PSTF_SpritePos(foe, 1)
		PSTF_EvalAs(grace, string.format('JumpToPoint([%d.%d])', fx - 180, fy + 40), 1)
		return
	end
	if waitDay then
		local tod = PSTF_LightGlobal()
		if tod ~= todNight or f - waitDay > 900 then
			PSTF_Note(string.format('lighting_hammer: day tint %06X after %d frames', tod, f - waitDay))
			waitDay, start = nil, f + 5
		end
		return
	end
	if f < start then return end
	local r = runs[run]
	local t = f - start
	if t == 0 then
		PSTF_LightDebug(r[2] == 'off' and 4 or r[2] == 'mask' and 5 or 0)
		for _, k in ipairs({{'fx_lights', 1}, {'bloom', 1}, {'night_light_gain', 150}, {'emissive', 1}, {'fx_soften', 1}}) do PSTF_LightSet(k[1], k[2]) end
		if r[3] then PSTF_LightSet(r[3], r[4]) end
		local x = pstfHammerExtra and pstfHammerExtra[r[2]]
		if x then PSTF_LightSet(x[1], x[2]) end
		PSTF_SetHp(foe, 1000)
		PSTF_EvalAs(grace, 'SpellNoDec(NearestEnemyOf(Myself),1204)', 1)
		local tod, applied = PSTF_LightGlobal()
		PSTF_Note(string.format('lighting_hammer: %s %s cast (tod %06X applied %06X)', r[1], r[2], tod, applied))
	elseif t >= 20 and t <= 80 and t % 6 == 2 then
		PSTF_LightShot(string.format('pst_forever/logs/hammer_%s_%s_%02d.bmp', r[1], r[2], t))
		if t % 12 == 2 then PSTF_LightFxCells() end
	elseif t == 200 then
		run = run + 1
		start = f + 1
		if runs[run] and runs[run][1] == 'day' and runs[run - 1][1] == 'night' then
			C:Eval('DayNight(12)', 0)
			waitDay = f
			start = f + 100000
		end
		if run > #runs then
			PSTF_LightDebug(0)
			PSTF_Note('lighting_hammer: ok')
			pstfTestTick = nil
		end
	end
end
