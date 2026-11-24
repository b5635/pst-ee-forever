-- SPDX-License-Identifier: GPL-2.0-or-later
-- Spell effects by night and by day, lit and vanilla (all-spells save 000000990, hour 1): each entry of
-- pstfFxList (default below) is cast by its caster at a Hive thug (or at itself), once per mode of pstfFxModes,
-- night modes first, then DayNight(12) and the day modes. Shots of the effect:
-- pst_forever/logs/fx_<RES>_<night|day>_<mode>_<tick>.bmp; PSTF_LightFxCells() logs the effect cells.
-- `python -I tools/fx_sheet.py` turns the shots into one contact sheet per spell (lit vs off side by side).
--   tools\launch.ps1 -Save 000000990 -Lua tests\ingame\lighting_spellfx.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'lighting_spellfx: ok|TEST FAIL' -Close -Timeout 900
-- A prefix line (a generated copy of this file) picks spells: pstfFxList = {{'Nameless', 'SPWI803', 2803, 'foe', 150}}
local list = pstfFxList or {
	{'Grace', 'SPPR204', 1204, 'foe', 110},
	{'Nameless', 'SPWI803', 2803, 'foe', 150},
	{'Ignus', 'SPWI802', 2802, 'foe', 200},
	{'Nameless', 'SPWI905', 2905, 'foe', 200},
	{'Nameless', 'SPWI602', 2602, 'self', 80},
	{'Nameless', 'SPWI704', 2704, 'self', 80},
}
local modes = pstfFxModes or {{'night', 'lit'}, {'night', 'off'}, {'day', 'lit'}, {'day', 'off'}}
local every = pstfFxEvery or 6
local foe = 0x180018
local f, step, start, waitDay, todNight = 0, 1, 30, nil, nil
local plan = {}
for _, m in ipairs(modes) do
	for _, s in ipairs(list) do plan[#plan + 1] = {s = s, day = m[1], mode = m[2]} end
end
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function slotOf(name)
	local i = 0
	for v in PSTF_PartyNames():gmatch('[^,]+') do
		if v == name then return i end
		i = i + 1
	end
	return -1
end
local ids = {}
function pstfTestTick()
	f = f + 1
	if f == 3 then
		for _, s in ipairs(list) do
			local sl = slotOf(s[1])
			if sl < 0 then return fail('no ' .. s[1] .. ': use the all-spells save') end
			ids[s[1]] = PSTF_PartyId(sl)
			for _, st in ipairs({'LEVEL', 'LEVEL2', 'LEVEL3'}) do C:Eval('ChangeStat(Myself,' .. st .. ',20,SET)', sl) end
		end
		PSTF_MakeEnemy(foe)
		if pstfFxTrace then PSTF_LightTrace(pstfFxTrace) end  -- logs each distinct draw path/flags of those cells
		todNight = PSTF_LightGlobal()
		if plan[1] and plan[1].day == 'day' then  -- day runs only: switch to noon first
			C:Eval('DayNight(12)', 0)
			waitDay, start = f, f + 100000
		end
		return
	end
	if waitDay then
		local tod = PSTF_LightGlobal()
		if tod ~= todNight or f - waitDay > 900 then
			PSTF_Note(string.format('lighting_spellfx: day tint %06X after %d frames', tod, f - waitDay))
			waitDay, start = nil, f + 5
		end
		return
	end
	if f < start then return end
	local p = plan[step]
	local s = p.s
	local who = ids[s[1]]
	local t = f - start
	if t == 0 then
		local _, _, h = PSTF_SpritePos(foe, 1)
		if not h or h <= 0 then  -- killed by an earlier spell (Power Word, Kill): the next Hive thug
			foe = PSTF_MakeEnemyNearest()
			PSTF_Note(string.format('lighting_spellfx: new foe %X', foe))
		end
		PSTF_LightDebug(p.mode == 'off' and 4 or p.mode == 'mask' and 5 or p.mode == 'raw' and 7 or p.mode == 'untint' and 8 or 0)
		-- A/B modes: pstfFxSets = {noemis = {'emissive', 0, 1}} sets a key for that run (third value: restored after)
		for m, kv in pairs(pstfFxSets or {}) do PSTF_LightSet(kv[1], (m == p.mode or 'alt' .. m == p.mode) and kv[2] or kv[3]) end
		PSTF_SetHp(foe, 1000)
		for _, n in pairs(ids) do PSTF_SetHp(n, 200) PSTF_Unapply(n, s[2]) end
		local fx, fy = PSTF_SpritePos(foe, 1)
		PSTF_EvalAs(who, string.format('JumpToPoint([%d.%d])', fx - 180, fy + 40), 1)
	elseif t == 2 then
		PSTF_CenterOn(who)  -- shots: the caster at the centre, the foe ~90 px (half size) to the right
	elseif t == 4 then
		local tgt = s[4] == 'self' and 'Myself' or 'NearestEnemyOf(Myself)'
		PSTF_EvalAs(who, 'SpellNoDec(' .. tgt .. ',' .. s[3] .. ')', 1)
		local tod, applied = PSTF_LightGlobal()
		PSTF_Note(string.format('lighting_spellfx: %s %s %s by %s (tod %06X applied %06X)', s[2], p.day, p.mode, s[1], tod, applied))
	elseif t > 4 and t <= s[5] and t % every == 0 then
		-- 'alt' / 'altNAME' (NAME = a pstfFxSets mode applied to the lit frame): lit frame, then vanilla
		local alt = p.mode:sub(1, 3) == 'alt'
		local m = p.mode == 'alt' and 'lit' or (alt and p.mode:sub(4) or p.mode)
		PSTF_LightShot(string.format('pst_forever/logs/fx_%s_%s_%s_%03d.bmp', s[2], p.day, m, t))
		if t % (every * 3) == 0 then PSTF_LightFxCells() end
		-- 'alt': the next tick is shot with our pass off, so lit and vanilla show (almost) the same frame
		if alt then PSTF_LightDebug(4) end
	elseif p.mode:sub(1, 3) == 'alt' and t > 4 and t <= s[5] + 1 and t % every == 1 then
		PSTF_LightShot(string.format('pst_forever/logs/fx_%s_%s_off%s_%03d.bmp', s[2], p.day, p.mode:sub(4), t - 1))
		PSTF_LightDebug(0)
	elseif t == s[5] + 2 then  -- (+1 is the last 'alt' vanilla shot)
		if s[4] == 'self' then PSTF_Note('lighting_spellfx: ' .. s[2] .. ' status ' .. tostring(PSTF_Status(who))) end
	elseif t == s[5] + 120 then  -- long effects (meteors) end before the next cast
		step = step + 1
		start = f + 1
		local n = plan[step]
		if not n then
			PSTF_LightDebug(0)
			PSTF_Note('lighting_spellfx: ok')
			pstfTestTick = nil
		elseif n.day == 'day' and p.day == 'night' then
			C:Eval('DayNight(12)', 0)
			waitDay, start = f, f + 100000
		end
	end
end
