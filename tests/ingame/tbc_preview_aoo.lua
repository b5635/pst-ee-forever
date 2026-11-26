-- SPDX-License-Identifier: GPL-2.0-or-later
-- BG3 cursor hints on a party turn (Hive save 000000126, one hostile thug jumped beside the Nameless One):
--   hit chance  hovering the foe with the plain (attack) cursor shows "NN%" next to the cursor (pstfHovHitOn),
--               not on the hover card;
--   AoO         a move preview that walks out of the thug's reach says "Provokes attack of opportunity"
--               (pstfTbc.aoo); the same preview after Withdraw does not.
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_preview_aoo.lua -Env @{PSTF_TEST_TICK='1'}
--     -Until 'tbc_preview_aoo: ok|TEST FAIL' -Close -Timeout 240
--   (add PSTF_SHOT_DIR / PSTF_SHOT_EVERY_MS to look at the labels)
local thug = 0x180018
local frames, tno, step, at = 0, nil, 0, 0
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function cells(a, b)
	local ax, ay = PSTF_SpritePos(a, 1)
	local bx, by = PSTF_SpritePos(b, 1)
	if not ax or ax < 0 or not bx or bx < 0 then return 999 end
	local dx, dy = math.floor(ax / 16) - math.floor(bx / 16), math.floor(ay / 12) - math.floor(by / 12)
	return math.sqrt(dx * dx + dy * dy)
end
local function beside()
	local x, y = PSTF_SpritePos(tno, 1)
	PSTF_EvalAs(thug, string.format('JumpToPoint([%d.%d])', math.floor(x + 28), math.floor(y)), 1)
end
local function awayPoint()
	local ax, ay = PSTF_SpritePos(tno, 1)
	local bx, by = PSTF_SpritePos(thug, 1)
	local dx, dy = ax - bx, ay - by
	local len = math.sqrt(dx * dx + dy * dy)
	if len < 1 then dx, dy, len = -1, 0, 1 end
	return math.floor(ax + dx / len * 130), math.floor(ay + dy / len * 100)
end

function pstfTestTick()
	frames = frames + 1
	if frames == 2 then tno = PSTF_PartyId(0) beside() return end
	if frames == 6 then PSTF_MakeEnemy(thug) return end
	if frames < 10 then return end
	local cur = PSTF_TbcCurrent()
	if cur == -1 then
		if frames > 600 then fail('no fight started') end
		return
	end
	if cur ~= tno then
		-- not ours: end party turns at once, let the thug act
		local slot = -1
		for i = 0, 7 do if PSTF_PartyId(i) == cur then slot = i end end
		if slot >= 0 and frames % 10 == 0 then PSTF_EndTurn() end
		return
	end
	if step == 0 then
		if cells(tno, thug) > 2.5 then beside() at = frames step = -1 return end
		step, at = 1, frames
		local tx, ty = PSTF_SpritePos(thug, 1)
		PSTF_HoverAt(tx, ty - 30)
		return
	end
	if step == -1 then
		if frames - at > 5 then step = 0 end
		return
	end
	if step == 1 and frames - at == 40 then
		local on = pstfHovHitOn()
		local txt = pstfHovHit and pstfHovHit() or ''
		PSTF_Note(string.format('tbc_preview_aoo: hover thug -> at cursor %s "%s" (cursor ui %d,%d)', tostring(on), txt,
			pstfHov.cx or -1, pstfHov.cy or -1))
		if not on then return fail('no hit chance next to the cursor over the foe') end
		if not string.match(txt, '^%d+%%$') then return fail('hit chance text is "' .. txt .. '", wanted NN%') end
		PSTF_HoverAt(-1, -1)
		local x, y = awayPoint()
		PSTF_PreviewAt(x, y)
		step, at = 2, frames
		return
	end
	if step == 2 and frames - at == 40 then
		PSTF_Note(string.format('tbc_preview_aoo: walk-away preview -> %d ft, aoo %s', pstfTbc.pathFt or 0, tostring(pstfTbc.aoo)))
		if (pstfTbc.pathFt or 0) <= 0 then return fail('no move preview') end
		if not pstfTbc.aoo then return fail('walking out of the thug\'s reach does not say it provokes') end
		if not pstfPathAooOn() then return fail('pstfPathAooOn() false while aoo is set') end
		if not PSTF_Maneuver('withdraw') then return fail('Withdraw refused') end
		step, at = 3, frames
		return
	end
	if step == 3 and frames - at == 40 then
		PSTF_Note(string.format('tbc_preview_aoo: after Withdraw -> %d ft, aoo %s', pstfTbc.pathFt or 0, tostring(pstfTbc.aoo)))
		if pstfTbc.aoo then return fail('a withdrawn walk still says it provokes') end
		PSTF_PreviewAt(-1, -1)
		PSTF_Note('tbc_preview_aoo: ok')
		pstfTestTick = nil
	end
end
