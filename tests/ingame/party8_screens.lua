-- SPDX-License-Identifier: GPL-2.0-or-later
-- 8-member party on the menu screens: every member's character sheet, inventory, mage and priest spell
-- screens and the area map, opened as the radial menu's buttons open them
-- (CBaldurEngine::OnLeftPanelButtonClick: 1 map, 3 inventory, 4 record, 5 mage, 6 priest; the target screen's
-- OnPortraitLClick gets the world's selected portrait).
--   - record: opened with member k selected shows member k (currentID = order[k]); a portrait click on the
--     sheet switches to that member.
--   - inventory: a portrait click selects member k (Lua `id`), its ground-pile cache slot k is filled
--     (CScreenInventory pile/flag i32[8]; slots 6/7 used to alias the flags: crash in GetUseButtonMode).
--   - map: one marker per member in the area (CScreenMap 8 entries), ids = the party.
-- Needs the 8-member quick save from party8.lua:
-- tools\launch.ps1 -Save 000000001 -Lua tests\ingame\party8_screens.lua -Env @{PSTF_DLG_TEST='1'; PSTF_TBC_NOFIGHT='1'} -Until 'party8_screens: ok|TEST FAIL|faulted' -Close -Timeout 200
local steps, at, frame = {}, 1, 0
local order, wait = {}, 0
local function note(s) PSTF_Note('party8_screens: ' .. s) end
local function fail(s) PSTF_Note('TEST FAIL: party8_screens: ' .. s) pstfDlgTest = nil end
local function add(f, frames) steps[#steps + 1] = {f = f, frames = frames or 12} end
local function engine() return e:GetActiveEngine() end
local function panel(n) engine():OnLeftPanelButtonClick(n) end
local function toWorld() if engine() ~= worldScreen then engine():SelectEngine(worldScreen) end end
local function caches()
	local s = PSTF_ScreenCaches()
	local c = {raw = s, count = tonumber(s:match('map count=(%d+)')), sel = tonumber(s:match('inv sel=(%-?%d+)'))}
	c.ids, c.piles, c.flags = {}, {}, {}
	for v in s:match('ids=([^ ]+)'):gmatch('[^,]+') do c.ids[#c.ids + 1] = tonumber(v) end
	for v in s:match('piles=([^ ]+)'):gmatch('[^,]+') do c.piles[#c.piles + 1] = tonumber(v) end
	for v in s:match('flags=([^ ]+)'):gmatch('[^,]+') do c.flags[#c.flags + 1] = tonumber(v) end
	return c
end

add(function()
	local s = PSTF_Party()
	for v in s:match('order=([^ ]+)'):gmatch('[^,]+') do order[#order + 1] = tonumber(v) end
	if #order ~= 8 or tonumber(s:match('count=(%d+)')) ~= 8 then return fail('party is not 8: ' .. s) end
	note('party ' .. PSTF_PartyNames())
end, 3)

-- Character record, opened from the world with member k selected.
for k = 0, 7 do
	add(function() toWorld() Infinity_OnPortraitLClick(k) end, 6)
	add(function() panel(4) end, 10)
	add(function()
		if engine() ~= e:GetEngineCharacter() then return fail('record ' .. k .. ': not on the character screen') end
		if currentID ~= order[k + 1] then return fail('record ' .. k .. ': currentID ' .. tostring(currentID) .. ', want ' .. order[k + 1]) end
		local c = characters[currentID]
		if not c or not c.name or c.name == '' then return fail('record ' .. k .. ': no character data') end
		note('record ' .. k .. ' ' .. c.name)
	end, 2)
end
-- Portrait clicks on the open sheet.
for k = 7, 0, -1 do
	add(function() Infinity_OnPortraitLClick(k) end, 6)
	add(function()
		if currentID ~= order[k + 1] then return fail('record click ' .. k .. ': currentID ' .. tostring(currentID)) end
	end, 2)
end
-- Inventory: every member, then back to the world (CancelEngine flushes the 8 pile slots).
add(function() toWorld() Infinity_OnPortraitLClick(0) end, 6)
add(function() panel(3) end, 10)
for k = 0, 7 do
	add(function() Infinity_OnPortraitLClick(k) end, 10)
	add(function()
		if engine() ~= e:GetEngineInventory() then return fail('inventory ' .. k .. ': screen left') end
		if id ~= order[k + 1] then return fail('inventory ' .. k .. ': id ' .. tostring(id) .. ', want ' .. order[k + 1]) end
		local c = caches()
		if c.sel ~= k then return fail('inventory ' .. k .. ': screen selection ' .. tostring(c.sel)) end
		if #c.flags ~= 8 or c.flags[k + 1] ~= 1 then return fail('inventory ' .. k .. ': ground cache not filled: ' .. c.raw) end
		if not characters[id] or not characters[id].equipment then return fail('inventory ' .. k .. ': no equipment table') end
		note('inventory ' .. k .. ' ' .. tostring(characters[id].name) .. ' pile ' .. c.piles[k + 1])
	end, 2)
end
add(function() toWorld() end, 10)
add(function()
	local c = caches()
	for i = 1, 8 do if c.flags[i] ~= 0 then return fail('ground cache not flushed on close: ' .. c.raw) end end
end, 2)
-- Inventory opened from the world with member 8 selected (the crash path: UpdateLuaEquipment on open).
add(function() Infinity_OnPortraitLClick(7) end, 6)
add(function() panel(3) end, 12)
add(function()
	if engine() ~= e:GetEngineInventory() or id ~= order[8] then return fail('inventory from member 8: id ' .. tostring(id)) end
	toWorld()
end, 8)
-- Mage and priest spell screens: every member.
for _, p in ipairs({5, 6}) do
	add(function() toWorld() Infinity_OnPortraitLClick(7) end, 6)
	add(function() panel(p) end, 10)
	for k = 0, 7 do add(function() Infinity_OnPortraitLClick(k) end, 6) end
	add(function() note('spell screen ' .. p .. ' ok') toWorld() end, 8)
end
-- Area map: a marker per member.
add(function() toWorld() panel(1) end, 40)
add(function()
	if engine() ~= e:GetEngineMap() then return fail('not on the map screen') end
	local c = caches()
	if c.count ~= 8 then return fail('map markers ' .. tostring(c.count) .. ', want 8: ' .. c.raw) end
	local want = {}
	for _, v in ipairs(order) do want[v] = true end
	for i = 1, 8 do
		if not want[c.ids[i]] then return fail('map marker ' .. i .. ' id ' .. tostring(c.ids[i]) .. ' not in the party: ' .. c.raw) end
		want[c.ids[i]] = nil
	end
	note('map ' .. c.raw)
	toWorld()
end, 10)
add(function() note('ok') pstfDlgTest = nil end, 1)

function pstfDlgTest()
	frame = frame + 1
	if frame < 30 then return end
	if wait > 0 then wait = wait - 1 return end
	local s = steps[at]
	if not s then return end
	at = at + 1
	s.f()
	wait = s.frames
end
