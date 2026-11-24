-- SPDX-License-Identifier: GPL-2.0-or-later
-- Path preview vs the engine's own walk (Hive save 000000126, manual party turns): on the protagonist's
-- turn (the first party member in the order) the preview to a spot straight behind another party member (who stands in the way) must go
-- around that member as the engine's walk then does: its length is within 15 % of the engine's path
-- (a standing creature is dear ground to the engine's search, not a wall). Also: a creature's own cell is occupied in
-- the search map (PSTF_SearchByte != 0), the preview does not count the walker itself.
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_path.lua -Env @{PSTF_TEST_TICK='1'} -Until 'tbc_path: ok|TEST FAIL' -Close -Timeout 200
local tno = -1  -- whoever has the first party turn
local frames, goal, step, prevFt = 0, nil, 0, 0
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function pts(s)
	local out = {}
	for x, y in string.gmatch(s, '(%-?%d+),(%-?%d+);') do out[#out + 1] = {tonumber(x), tonumber(y)} end
	return out
end
local function feet(x0, y0, list)
	local d = 0
	for _, p in ipairs(list) do
		local fx, fy = (p[1] - x0) / 16, (p[2] - y0) / 12
		d = d + math.sqrt(fx * fx + fy * fy)
		x0, y0 = p[1], p[2]
	end
	return d
end
function pstfTestTick()
	frames = frames + 1
	if frames == 5 and step == 0 then PSTF_MakeEnemy(0x140014) end
	local id, _, party = PSTF_TbcCurrent()
	if step == 0 and id ~= -1 and party then
		tno = id
		local tx, ty = PSTF_SpritePos(tno, 1)
		-- the party member nearest to him stands in the way: aim 3 cells straight behind it
		local best, bx, by = nil, 0, 0
		for s = 0, 5 do
			local m = PSTF_PartyId(s)
			if m ~= -1 and m ~= tno then
				local mx, my = PSTF_SpritePos(m, 1)
				local d = (mx - tx) ^ 2 + (my - ty) ^ 2
				if (not best or d < best) and d > 900 then best, bx, by = d, mx, my end
			end
		end
		if not best then return fail('no party member near the protagonist') end
		local dx, dy = bx - tx, by - ty
		local len = math.sqrt(dx * dx + dy * dy)
		goal = {math.floor(bx + dx / len * 96), math.floor(by + dy / len * 72)}
		local occ = PSTF_SearchByte(bx, by)
		PSTF_Note(string.format('tbc_path: member at %d,%d occupancy %d; goal %d,%d', bx, by, occ, goal[1], goal[2]))
		if occ <= 0 then return fail('a standing party member is not in the search map') end
		local pre = pts(PSTF_PreviewPts(goal[1], goal[2], tno))
		if #pre == 0 then return fail('no preview path') end
		prevFt = feet(tx, ty, pre)
		PSTF_Note(string.format('tbc_path: preview %d points, %.1f ft', #pre, prevFt))
		local slot = 0
		for s = 0, 5 do if PSTF_PartyId(s) == tno then slot = s end end
		C:Eval('MoveToPoint([' .. goal[1] .. '.' .. goal[2] .. '])', slot)
		step, frames = 1, 0
	elseif step == 1 and frames >= 1 then
		local tx, ty = PSTF_SpritePos(tno, 1)
		local eng = pts(PSTF_EnginePath(tno))
		if #eng == 0 then
			if frames > 30 then return fail('the engine found no path') end
			return
		end
		local engFt = feet(tx, ty, eng)
		PSTF_Note(string.format('tbc_path: engine %d points, %.1f ft (preview %.1f ft)', #eng, engFt, prevFt))
		if math.abs(engFt - prevFt) > 0.15 * engFt + 2 then
			return fail(string.format('preview %.1f ft vs engine %.1f ft', prevFt, engFt))
		end
		PSTF_Note('tbc_path: ok')
		pstfTestTick = nil
	end
	if frames > 3000 then fail('timeout') end
end
