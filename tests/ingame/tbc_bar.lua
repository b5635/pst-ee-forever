-- SPDX-License-Identifier: GPL-2.0-or-later
-- BG3 hotbar on a manual party turn: all weapons, spells, quick items and abilities of the active party
-- member at once (screenshot), then a click on the first usable spell button through the HUD's own
-- handler (pstfBarClick) must start a cast or a target cursor.
--   tools\launch.ps1 -Lua tests\ingame\tbc_bar.lua -Env @{PSTF_TEST_TICK='1'; PSTF_SHOT_DIR='<dir>';
--   PSTF_SHOT_EVERY_MS='800'} -Until 'TEST PASS|TEST FAIL' -Close
local enemy = 0x320032
local frames, cur, turnStart = 0, -1, 0
local done = false
local probed = false
local clicked = false
local function fail(m) PSTF_Note('TEST FAIL: ' .. m) pstfTestTick = nil end
function pstfTestTick()
	frames = frames + 1
	if frames == 8 then PSTF_MakeEnemy(enemy) end
	local id, r, party = PSTF_TbcCurrent()
	if id ~= cur then cur, turnStart = id, frames end
	if party and frames - turnStart == 30 and not probed then
		probed = true
		PSTF_Note('const: ' .. tostring(const) .. ' spells ' .. tostring(const and const.ACTIONBAR_STATE_SPELLS) .. ' weapons ' .. tostring(const and const.ACTIONBAR_STATE_WEAPONS))
		local ok, err = pcall(function()
			local ba = game:GetButtonArray()
			for _, st in ipairs({const.ACTIONBAR_STATE_WEAPONS, const.ACTIONBAR_STATE_SPELLS, const.ACTIONBAR_STATE_ITEMS}) do
				ba:SetState(st)
				PSTF_Note('state ' .. tostring(st) .. ' occupied ' .. tostring(ba:GetNumberOccupiedSlots()) .. ' slots ' .. PSTF_BarSlots())
				PSTF_Bar()
			end
		end)
		if not ok then PSTF_Note('probe error ' .. tostring(err)) end
	end
	if party and frames - turnStart == 40 then
		local rows = {}
		for _, e in ipairs(pstfBar or {}) do rows[e.row] = (rows[e.row] or 0) + 1 end
		PSTF_Note(string.format('hotbar for %d: %d buttons (weapons %d, spells %d, items %d, abilities %d)', id, #(pstfBar or {}),
			rows[1] or 0, rows[2] or 0, rows[3] or 0, rows[4] or 0))
		for k, e in ipairs(pstfBar or {}) do PSTF_Note('  ' .. k .. ' row ' .. e.row .. ' slot ' .. e.slot .. ' ' .. e.icon .. (e.on and '' or ' (disabled)')) end
		if #(pstfBar or {}) == 0 then return fail('no hotbar buttons on a party turn') end
	end
	if party and frames - turnStart == 45 and not clicked then
		-- click the first spell through the HUD's own handler: the engine must enter its spell mode
		for k, e in ipairs(pstfBar or {}) do
			if e.row == 2 and e.on then
				local before = game:GetState()
				pstfBarClick(k)
				PSTF_Note('clicked hotbar ' .. k .. ' (' .. e.icon .. '): game state ' .. tostring(before) .. ' -> ' .. tostring(game:GetState()) .. ', icon ' .. tostring(game:GetIconIndex()))
				clicked = (game:GetState() ~= before) or (game:GetIconIndex() ~= 0)
				if not clicked then return fail('hotbar click did not start a cast') end
				game:SetIconIndex(0) game:SetState(0)
				break
			end
		end
	end
	if party and frames - turnStart == 60 then PSTF_EndTurn() end
	if clicked and frames - turnStart == 61 then PSTF_Note('TEST PASS: hotbar shown and a click starts the engine cast') pstfTestTick = nil end
	if frames > 60 * 30 then PSTF_Note('TEST PASS: hotbar shown on party turns') pstfTestTick = nil end
end
