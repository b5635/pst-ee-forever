-- SPDX-License-Identifier: GPL-2.0-or-later
-- BG3 HUD across screens (Hive save 000000126, manual party turns):
--   - item icons keep their aspect: PSTF_IconSize('II020') (Bandages, cycle 0 frame 0) = 61 x 47;
--   - status icons: Blessing (SPPR101) applied to the protagonist shows as an icon (PSTF_Status);
--   - the hover card shows on the world screen out of a fight (PSTF_HoverAt on a party member);
--   - in a fight, on a party turn: the hotbar has entries and pages (pstfBarPages, pstfBarNext);
--   - the inventory mid-fight and back: our bar is on the stack again and raised over the engine bar
--     (pstfHudRaises grows), the hotbar still has its entries;
--   - the options screen: none of our menus on the stack; back on the world screen they return.
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_screens.lua -Env @{PSTF_TEST_TICK='1'} -Until 'tbc_screens: ok|TEST FAIL' -Close -Timeout 300
local thugs = {0x180018, 0x140014, 0x1C001C}
local tno = PSTF_PartyId(0)
local frames, step, at = 0, 'start', 0
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function on(m) return Infinity_IsMenuOnStack(m) and true or false end
local function stack() return string.format('bar %s tbc %s status %s portraits %s engine bar %s', tostring(on('PSTF_BAR')),
	tostring(on('PSTF_TBC')), tostring(on('PSTF_STATUS')), tostring(on('PSTF_PORTRAITS')), tostring(on('WORLD_ACTIONBAR_LEFT'))) end
-- count how often our menus were raised over a re-pushed engine bar
local raises = 0
if pstfHudAfterOpen and not pstfTestWrapped then
	pstfTestWrapped = true
	local orig = pstfHudAfterOpen
	pstfHudAfterOpen = function() if pstfHudRaising then raises = raises + 1 end return orig() end
end
local function go(s) step, at = s, frames PSTF_Note('tbc_screens: ' .. s .. ' (' .. stack() .. ')') end
function pstfTestTick()
	frames = frames + 1
	local since = frames - at
	if step == 'start' and frames == 5 then
		local w, h = PSTF_IconSize('II020')
		PSTF_Note('tbc_screens: II020 ' .. tostring(w) .. 'x' .. tostring(h))
		if w ~= 61 or h ~= 47 then return fail('II020 is ' .. tostring(w) .. 'x' .. tostring(h) .. ', expected 61x47') end
		PSTF_EvalAs(tno, 'ApplySpellRES("SPPR101",Myself)')
		go('blessed')
	elseif step == 'blessed' and since == 30 then
		PSTF_Effects(tno)
		local n = PSTF_Status(tno)
		if n < 1 then return fail('no status icon after Blessing') end
		local s = pstfStat[0]
		if not s or not s[1] then return fail('pstfStat[0] has no icon') end
		PSTF_Note('tbc_screens: slot 0 icon ' .. tostring(s[1].b) .. ' rounds ' .. tostring(s[1].r))
		local x, y = PSTF_SpritePos(PSTF_PartyId(1))
		PSTF_HoverAt(x, y - 30)
		go('hover')
	elseif step == 'hover' and since == 10 then
		if not pstfHov.on then return fail('no hover card out of a fight') end
		PSTF_Note('tbc_screens: hover on ' .. pstfHovName() .. ' hp ' .. tostring(pstfHov.hp))
		PSTF_HoverAt(-1, -1)
		for _, t in ipairs(thugs) do PSTF_MakeEnemy(t) end
		go('fight')
	elseif step == 'fight' then
		local id, r, party = PSTF_TbcCurrent()
		if party and #pstfBar > 0 then
			if not on('PSTF_BAR') or not on('PSTF_TBC') then return fail('fight menus not on the stack: ' .. stack()) end
			PSTF_Note(string.format('tbc_screens: party turn of %X, hotbar %d entries, %d pages', id, #pstfBar, pstfBarPages()))
			-- pagination: pad the bar to 25 entries (2 pages of 2 x 10), page 2 starts at entry 21
			local n = #pstfBar
			if n > 40 then return fail('more than 2 pages: ' .. n) end
			for k = n + 1, 25 do pstfBar[k] = {row = 9, icon = 'II020', on = true, w = 61, h = 47, tip = 'pad ' .. k} end
			if pstfBarPages() ~= 2 or not pstfBarPagesOn() or pstfBarCanPrev() then return fail('2 pages expected, got ' .. pstfBarPages()) end
			pstfBarNext()
			if pstfBarPage ~= 2 or pstfBarEntry(1) ~= pstfBar[21] or pstfBarOn(#pstfBar - 19) or not pstfBarCanPrev() or pstfBarCanNext() then
				return fail('page 2 does not show entries 21..25')
			end
			pstfBarNext()
			if pstfBarPage ~= 2 then return fail('paged past the last page') end
			pstfBarPrev()
			if pstfBarPage ~= 1 then return fail('previous page') end
			pstfBarCollect()
			if #pstfBar ~= n then return fail('collect did not restore the bar') end
			go('inventory')
			e:GetActiveEngine():SelectEngine(e:GetEngineInventory())
		elseif since > 2000 then return fail('no party turn') end
	elseif step == 'inventory' and since == 40 then
		if not on('PSTF_BAR') then return fail('hotbar not shown on the inventory: ' .. stack()) end
		go('back')
		e:GetActiveEngine():SelectEngine(worldScreen)
	elseif step == 'back' and since == 40 then
		local _, _, party = PSTF_TbcCurrent()
		if not on('PSTF_BAR') then return fail('hotbar gone after the inventory: ' .. stack()) end
		if raises < 1 then return fail('our menus were not raised over the engine bar again') end
		if party and #pstfBar == 0 then return fail('hotbar empty after the inventory') end
		PSTF_Note(string.format('tbc_screens: back, %d raises, hotbar %d entries', raises, #pstfBar))
		go('options')
		e:GetActiveEngine():SelectEngine(optionsScreen)
	elseif step == 'options' and since == 40 then
		if on('PSTF_BAR') or on('PSTF_TBC') or on('PSTF_STATUS') or on('PSTF_PORTRAITS') then
			return fail('our menus still up on the options screen: ' .. stack())
		end
		go('world again')
		e:GetActiveEngine():SelectEngine(worldScreen)
	elseif step == 'world again' and since == 40 then
		if not on('PSTF_BAR') or not on('PSTF_STATUS') or not on('PSTF_TBC') then return fail('menus not back on the world screen: ' .. stack()) end
		PSTF_Note('tbc_screens: ok')
		pstfTestTick = nil
	end
end
