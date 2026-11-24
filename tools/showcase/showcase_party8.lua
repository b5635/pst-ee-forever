-- SPDX-License-Identifier: GPL-2.0-or-later
-- Showcase: the 8-member party (README recording). Opens the character record and the inventory the way
-- the radial menu's buttons do (OnLeftPanelButtonClick 4 / 3) and pages through all eight members by
-- clicking their portraits. Starts after 15 s (launch overlays).
-- Run with env PSTF_DLG_TEST=1 (pstfDlgTest runs every main-loop iteration, also on the menu screens).
local t0, step, last = nil, 0, 0
local WAIT, EACH = 15000, 1700
local function panel(n) e:GetActiveEngine():OnLeftPanelButtonClick(n) end
local function toWorld() e:GetActiveEngine():SelectEngine(worldScreen) end
function pstfDlgTest()
	local t = Infinity_GetClockTicks()
	if not t0 then
		t0 = t
		PSTF_Note('showcase_party8: party ' .. PSTF_PartyNames())
		Infinity_OnPortraitLClick(0)
		return
	end
	if t - t0 < WAIT or t - last < EACH then return end
	last = t
	if step == 0 then
		PSTF_Note('showcase_party8: character sheets')
		panel(4)
	elseif step <= 7 then
		Infinity_OnPortraitLClick(step)
	elseif step == 8 then
		toWorld()
		Infinity_OnPortraitLClick(0)
		PSTF_Note('showcase_party8: inventories')
		panel(3)
	elseif step <= 15 then
		Infinity_OnPortraitLClick(step - 8)
	elseif step == 16 then
		toWorld()
	else
		PSTF_Note('showcase_party8: done')
		pstfDlgTest = nil
	end
	step = step + 1
end
