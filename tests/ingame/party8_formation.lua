-- SPDX-License-Identifier: GPL-2.0-or-later
-- 8-member party: formation points for area moves (CAIGroup::GetOffsets(.., party=1), hooked). Area moves
-- (LeaveAreaName, world map EnterArea, CompressTime, the Lua leave-area entries) read point[slot] for every
-- party slot; the engine makes max(selected, 7) points. With fewer than 8 selected, point 7 must be ours:
-- point 6 + (point 6 - point 4). Points 0..6 stay the engine's (same as asking with 8 selected would
-- give for the table part). Needs the 8-member quick save party8.lua writes (000000001).
-- Run: tools\launch.ps1 -Save 000000001 -Lua tests\ingame\party8_formation.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'party8_formation: ok|TEST FAIL' -Close
local f = 0
local function fail(msg) PSTF_Note('TEST FAIL: party8_formation: ' .. msg) pstfTestTick = nil end
local function parse(s)
	local sel = tonumber(s:match('selected=(%-?%d+)'))
	local p = {}
	for x, y in s:gmatch('(%-?%d+),(%-?%d+)') do p[#p + 1] = { tonumber(x), tonumber(y) } end
	return sel, p
end
function pstfTestTick()
	f = f + 1
	if f ~= 3 then return end
	-- all 8 selected: the engine's own rule for groups past 7 (point 7 = member 8's place relative to
	-- the leader); then a selection smaller than the party: point 7 is ours.
	local sel8, p8 = parse(PSTF_Formation(4, 0))
	if sel8 ~= 8 or #p8 ~= 8 then return fail('want 8 selected at load, got ' .. tostring(sel8)) end
	PSTF_UnselectAll()
	local checked = 0
	for _, form in ipairs({ 1, 3, 4, 6, 11 }) do
		for _, angle in ipairs({ 0, 90 }) do
			local sel, p = parse(PSTF_Formation(form, angle))
			if not sel or #p ~= 8 then return fail('formation ' .. form .. ': no 8 points') end
			if sel < 8 then
				for c = 1, 2 do
					local want = 2 * p[7][c] - p[5][c]
					if p[8][c] ~= want then
						return fail(('formation %d angle %d: point 7 %d,%d, want %d (6 + (6 - 4))'):format(form, angle,
							p[8][1], p[8][2], want))
					end
				end
				local distinct = {}
				for i = 1, 8 do distinct[p[i][1] .. ',' .. p[i][2]] = true end
				local n = 0
				for _ in pairs(distinct) do n = n + 1 end
				if n ~= 8 then return fail('formation ' .. form .. ': ' .. n .. ' distinct points of 8') end
				checked = checked + 1
			end
		end
	end
	if checked == 0 then return fail('all 8 selected: nothing checked') end
	PSTF_Note('party8_formation: ok (' .. checked .. ' formations with point 7 filled)')
	pstfTestTick = nil
end
