-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for the real-time d20 of the roll panel (src/modules/rolls/die3d.h, src/engine/die_draw.cpp).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\die3d.lua
--      -Env @{PSTF_DLG_TEST='1'; PSTF_TBC_NOFIGHT='1'} -Until 'die3d: ok|TEST FAIL' -Close -Timeout 200
-- Opens the panel with a fixed roll (natural 12, +5, DC 14), then:
-- 1. before the click the GL die draws (draw count grows, no failure) and its centre pixel is the die's
--    green, not the panel behind it;
-- 2. through the roll it keeps drawing every frame up to Continue;
-- 3. with PSTF_DIE3D = false the BAM die is back (no GL draws) - the toggle check.
-- Shots: pst_forever/logs/die3d_{idle,0300,0700,1100,1500,2100,4250,4600,6400,bam}.bmp
local function note(s) PSTF_Note(s) end
local function fail(msg) note('TEST FAIL: ' .. msg) pstfDlgTest = nil end
local function shot(name) PSTF_LightShot('pst_forever/logs/die3d_' .. name .. '.bmp') end
local function now() return Infinity_GetClockTicks() end
local function state(arm)
	local on, drawn, failed, r, g, b, why = PSTF_DieState(arm and 1 or 0):match('(%a+) (%d+) (%d+) (%-?%d+),(%-?%d+),(%-?%d+) ?(.*)')
	return { on = on, drawn = tonumber(drawn), failed = tonumber(failed), r = tonumber(r), g = tonumber(g), b = tonumber(b), why = why }
end

local phase, t = 'start', now()
local marks = { 300, 700, 1100, 1500, 2100, 4250, 4600, 6400 }
local shotAt, d0 = {}, 0
local function go(p) phase, t = p, now() end

function pstfDlgTest()
	local ms = now() - t
	if phase == 'start' then
		if ms < 1500 then return end
		if not PSTF_DIE3D then return fail('[rolls] die_3d is off in the ini') end
		pstfDrOpen{ title = 'Die Test', ab = 'Strength', card = 0, mod = 5, dc = 14,
			roll = function() return 12, 17, 1 end, done = function() end }
		if not pstfDr.open then return fail('the panel did not open') end
		go('idle')
	elseif phase == 'idle' then
		if ms == 0 then return end
		if ms > 300 and not shotAt.arm then
			shotAt.arm = true
			d0 = state(true).drawn
			return
		end
		if ms < 800 then return end
		local s = state()
		note(string.format('die3d: idle: %s, %d draws, %d failed, centre %d,%d,%d %s', s.on, s.drawn, s.failed, s.r, s.g, s.b, s.why))
		if s.failed > 0 or s.drawn <= d0 then return fail('the GL die did not draw: ' .. s.why) end
		-- the die's green (lit faces 22,92,34 .. 150,255,150 with a light number on top), not the panel's dark fill
		if s.r < 0 or not (s.g > s.r + 25 and s.g > s.b + 20) then
			return fail(string.format('centre pixel %d,%d,%d is not the die', s.r, s.g, s.b))
		end
		shot('idle')
		d0 = s.drawn
		pstfDrRollClick()
		if not pstfDr.t0 then return fail('roll did not start') end
		go('roll')
	elseif phase == 'roll' then
		local rm = pstfDrMs()
		for _, m in ipairs(marks) do
			if rm >= m and not shotAt[m] then
				shotAt[m] = true
				shot(string.format('%04d', m))
			end
		end
		if pstfDrContinueOn() then
			local s = state()
			local frames = s.drawn - d0
			note(string.format('die3d: roll: %d GL draws in %d ms, %d failed', frames, rm, s.failed))
			if s.failed > 0 then return fail('a draw failed: ' .. s.why) end
			if frames < rm / 1000 * 15 then return fail('the die drew in only ' .. frames .. ' frames') end
			pstfDrContinue()
			go('bam')
		elseif rm > 12000 then
			return fail('no Continue after 12 s')
		end
	elseif phase == 'bam' then
		if ms < 300 then return end
		if not shotAt.bam then
			shotAt.bam = true
			PSTF_DIE3D = false
			pstfDrOpen{ title = 'BAM Test', ab = 'Strength', card = 0, mod = 5, dc = 14,
				roll = function() return 12, 17, 1 end, done = function() end }
			d0 = state().drawn
			t = now()
			return
		end
		if ms < 800 then return end
		local s = state()
		shot('bam')
		pstfDrClose()
		PSTF_DIE3D = true
		if s.drawn ~= d0 then return fail('GL draws with PSTF_DIE3D off') end
		note('die3d: ok')
		pstfDlgTest = nil
	end
end
