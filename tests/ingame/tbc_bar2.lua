-- SPDX-License-Identifier: GPL-2.0-or-later
-- Hotbar with many spells (all-spells save 000000990), in a turn-based fight:
--   Pages      the Nameless One's memorised spells are all on the bar: the engine shows 10 of a list longer than 12
--              (scroll arrows), the bar scrolls the engine's list (PSTF_BarScroll) and takes every entry, once;
--   Maneuvers  Trip and Block are not on the bar of someone who may not use them (the Nameless One unless he is a fighter), and are on a
--              warrior's bar (Dak'kon);
--   Right-click a spell (a later page) opens the game's SPELL_DESCRIPTION, an item ITEM_DESCRIPTION, a maneuver
--              our PSTF_DESC with its name; a left click on a later-page spell starts that cast.
--   tools\launch.ps1 -Save 000000990 -Lua tests\ingame\tbc_bar2.lua -Env @{PSTF_TEST_TICK='1'} -Until 'tbc_bar2: ok|TEST FAIL' -Close -Timeout 300
local frames, cur, turnStart = 0, -1, 0
local foe = 0x180018
local tno, warrior = nil, nil
local done = {}
local function fail(m) PSTF_Note('TEST FAIL: ' .. m) pstfTestTick = nil end
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h or -1 end
local function hasMove(id)
	for _, e in ipairs(pstfBar or {}) do if e.move and e.id == id then return true end end
	return false
end
local function popAll()
	for _, m in ipairs({'SPELL_DESCRIPTION', 'ITEM_DESCRIPTION', 'PSTF_DESC'}) do
		if Infinity_IsMenuOnStack(m) then Infinity_PopMenu(m) end
	end
end
local function nameless()
	for i = 0, 7 do
		local id = PSTF_PartyId(i)
		if id and id ~= -1 then
			local c, sp = PSTF_Class(id)
			if sp == 2 then tno = id end
			if (c == 7 or c == 2) and sp ~= 2 and sp ~= 9 and not warrior then warrior = id end
		end
	end
end
local function tnoTurn(t)
	if t == 30 then
		local st = const.ACTIONBAR_STATE_SPELLS
		game:GetButtonArray():SetState(st)
		local n = PSTF_BarCount()
		pstfBarCollect()
		local spells, seen, dup = 0, {}, 0
		for _, e in ipairs(pstfBar) do
			if e.row == 2 then
				spells = spells + 1
				local key = tostring(e.scroll) .. ':' .. e.slot
				if seen[key] then dup = dup + 1 end
				seen[key] = true
			end
		end
		PSTF_Note(string.format('tbc_bar2: engine spell list %d, bar spells %d, buttons %d, pages %d, duplicates %d', n, spells, #pstfBar,
			pstfBarPages(), dup))
		if n <= 12 then return fail('the save has only ' .. n .. ' spells for the Nameless One: no scrolling to test') end
		if spells ~= n then return fail(string.format('the bar shows %d of %d spells', spells, n)) end
		if dup > 0 then return fail('a spell is on the bar twice') end
		if pstfBarPages() <= 2 then return fail('still only ' .. pstfBarPages() .. ' pages') end
		local tc = PSTF_Class(tno)
		if tc ~= 2 and (hasMove('trip') or hasMove('block')) then return fail('Trip / Block on the bar of a Nameless One who is no fighter (class ' .. tc .. ')') end
		if tc == 2 and not (hasMove('trip') and hasMove('block')) then return fail('Trip / Block missing for the Nameless One as a fighter') end
		if not hasMove('run') or not hasMove('withdraw') then return fail('Run / Withdraw missing') end
	elseif t == 40 then
		-- right-click the last spell (a later scroll of the engine's list)
		local k = nil
		for i, e in ipairs(pstfBar) do if e.row == 2 and e.scroll and e.scroll > 0 then k = i end end
		if not k then return fail('no spell from a later scroll') end
		pstfBarPage = math.floor((k - 1) / pstfBarPerPage()) + 1
		local kk = k - (pstfBarPage - 1) * pstfBarPerPage()
		pstfBarRClick(kk)
		local on = Infinity_IsMenuOnStack('SPELL_DESCRIPTION')
		PSTF_Note(string.format('tbc_bar2: right-click spell %s (scroll %d slot %d, page %d): SPELL_DESCRIPTION %s, %s', pstfBar[k].icon,
			pstfBar[k].scroll, pstfBar[k].slot, pstfBarPage, tostring(on), tostring(currentSpell and Infinity_FetchString(currentSpell.name))))
		if not on then return fail('right-click on a spell opened no description') end
		done.spellDesc = kk
		popAll()
	elseif t == 46 then
		for i, e in ipairs(pstfBar) do
			if e.move and e.id == 'run' then
				pstfBarPage = 1
				pstfBarRClick(i)
				PSTF_Note(string.format('tbc_bar2: right-click Run: PSTF_DESC %s, name %s, tip %s', tostring(Infinity_IsMenuOnStack('PSTF_DESC')),
					tostring(pstfDesc.name), tostring(pstfBarTip(i))))
				if not Infinity_IsMenuOnStack('PSTF_DESC') or pstfDesc.name ~= 'Run' then return fail('maneuver description missing') end
				if pstfBarTip(i) ~= 'Run' then return fail('Run hover is not just its name') end
				if not string.find(pstfDesc.text, 'twice the normal rate', 1, true) then return fail('Run description text') end
				break
			end
		end
	elseif t == 49 then
		PSTF_LightShot('pst_forever/logs/bar2_desc.bmp')  -- the Run description panel
	elseif t == 51 then
		popAll()
	elseif t == 54 then
		for i, e in ipairs(pstfBar) do
			if e.row == 3 then
				pstfBarPage = math.floor((i - 1) / pstfBarPerPage()) + 1
				pstfBarRClick(i - (pstfBarPage - 1) * pstfBarPerPage())
				PSTF_Note(string.format('tbc_bar2: right-click item %s: ITEM_DESCRIPTION %s', e.icon, tostring(Infinity_IsMenuOnStack('ITEM_DESCRIPTION'))))
				if not Infinity_IsMenuOnStack('ITEM_DESCRIPTION') then return fail('right-click on an item opened no description') end
				done.item = true
				break
			end
		end
		if not done.item then PSTF_Note('tbc_bar2: no quick item to right-click') end
		popAll()
	elseif t == 60 then
		local before = game:GetState()
		pstfBarPage = math.floor((#pstfBar - 1) / pstfBarPerPage()) + 1
		local k = nil
		for i, e in ipairs(pstfBar) do if e.row == 2 and e.scroll and e.scroll > 0 and e.on then k = i end end
		if not k then return fail('no usable later-scroll spell') end
		pstfBarPage = math.floor((k - 1) / pstfBarPerPage()) + 1
		pstfBarClick(k - (pstfBarPage - 1) * pstfBarPerPage())
		PSTF_Note(string.format('tbc_bar2: left-click %s: game state %d -> %d', pstfBar[k].icon, before, game:GetState()))
		if game:GetState() == before then return fail('a later-page spell click started no cast') end
		game:SetState(0)
		done.tno = true
		PSTF_EndTurn()
	end
end
function pstfTestTick()
	frames = frames + 1
	if frames == 3 then
		nameless()
		PSTF_Note(string.format('tbc_bar2: Nameless One %s, warrior %s', tostring(tno), tostring(warrior)))
		if not tno then return fail('no Nameless One') end
		PSTF_MakeEnemy(foe)
		return
	end
	if frames < 5 then return end
	if hp(foe) > 0 and hp(foe) < 500 then PSTF_SetHp(foe, 1000) end
	local id, _, party = PSTF_TbcCurrent()
	if id ~= cur then cur, turnStart = id, frames end
	local t = frames - turnStart
	if id == -1 then return end
	if id == tno and not done.tno then
		tnoTurn(t)
	elseif id == warrior and not done.warrior then
		if t ~= 30 then return end
		local a, b = hasMove('trip'), hasMove('block')
		PSTF_Note(string.format('tbc_bar2: warrior %X bar: trip %s, block %s', id, tostring(a), tostring(b)))
		if not a or not b then return fail('a warrior lacks Trip / Block') end
		done.warrior = true
		PSTF_EndTurn()
	elseif t == 20 then
		PSTF_EndTurn()
	end
	if done.tno and (done.warrior or not warrior) then
		PSTF_Note('tbc_bar2: ok')
		pstfTestTick = nil
	end
	if frames > 3000 then return fail('timeout') end
end
