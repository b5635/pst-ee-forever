// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/tbc/hud.h"

#include <cmath>
#include <cstdio>

namespace pstf::tbc {
namespace {

// Lua helpers, run once when the menu text is parsed (inline `...` chunk, as UI.MENU does).
// Card sizes follow the bestiary art (316 x 492 in the journal).
const char* kLua = R"LUA(
pstfTbc = { on = false, cards = {}, dots = {}, rdots = {} }
pstfTbcOpen = false
pstfBest = nil
function pstfBestImage(idx)
	if idx == nil or idx < 0 then return nil end
	if not pstfBest then
		pstfBest = {}
		for _, v in pairs(bestiary or {}) do
			if v.index then pstfBest[v.index] = v end
		end
	end
	local c = pstfBest[idx]
	if not c then return nil end
	local ref = nil
	if c.iglobal then
		ref = c['imageKnown' .. Infinity_GetScriptVarInt(c.iglobal)]
	elseif c.global then
		ref = c['imageKnown' .. Infinity_GetScriptVarInt(c.global)]
	end
	return ref or c.imageKnown
end
-- Turn-based: a portrait click selects only the party member whose turn it is.
if Infinity_OnPortraitLClick and not pstfOrigPortraitL then
	pstfOrigPortraitL = Infinity_OnPortraitLClick
	Infinity_OnPortraitLClick = function(n)
		if pstfTbc.on and n ~= pstfTbc.slot then return end
		return pstfOrigPortraitL(n)
	end
end
-- Our menus are on the stack exactly when they belong there (called every frame by the DLL): the hotbar,
-- portrait column and status icons with the bottom bar, the fight overlay on the world screen. An
-- engine bar pushed again (back from the inventory...) goes over ours, so ours are pushed again on top.
function pstfMenuSync(name, want, top)
	local on = Infinity_IsMenuOnStack(name)
	if on and (not want or top) then Infinity_PopMenu(name) on = false end
	if want and not on then Infinity_PushMenu(name) end
end
function pstfHudGuard()
	if pstfHudRaising then pstfHudAfterOpen() end
	local bar = Infinity_IsMenuOnStack('WORLD_ACTIONBAR_LEFT') and true or false
	local world = e:GetActiveEngine() == worldScreen
	if optionsScreen and e:GetActiveEngine() == optionsScreen then bar = false end
	pstfMenuSync('PSTF_PORTRAITS', bar and pstfBg3)
	pstfMenuSync('PSTF_BAR', bar and (pstfBg3 or (pstfTbc.on and world)))
	pstfMenuSync('PSTF_STATUS', bar)
	pstfMenuSync('PSTF_TBC', pstfTbc.on and world)
end
-- WORLD_ACTIONBAR_LEFT's onOpen: it was just pushed, so ours go on top of it again.
function pstfHudRaise()
	pstfHudRaising = true
end
function pstfHudAfterOpen()
	if not pstfHudRaising then return end
	pstfHudRaising = false
	pstfMenuSync('PSTF_PORTRAITS', pstfBg3, true)
	pstfMenuSync('PSTF_BAR', pstfBg3 or pstfTbc.on, true)
	pstfMenuSync('PSTF_STATUS', true, true)
end
function pstfCard(i) return pstfTbc.on and pstfTbc.cards[i] or nil end
function pstfCardOn(i) return pstfCard(i) ~= nil end
function pstfCardImage(i)
	local c = pstfCard(i)
	if not c then return '' end
	if c.img == nil then c.img = pstfBestImage(c.best) or false end
	return c.img or ''
end
function pstfCardHasImage(i) return pstfCardImage(i) ~= '' end
function pstfCardName(i)
	local c = pstfCard(i)
	if not c then return '' end
	if c.name and c.name >= 0 then return Infinity_FetchString(c.name) end
	return c.str or ''
end
function pstfCardActive(i) local c = pstfCard(i) return c ~= nil and c.active end
function pstfCardFoe(i) local c = pstfCard(i) return c ~= nil and c.foe end
function pstfCardFriend(i) local c = pstfCard(i) return c ~= nil and not c.foe end
function pstfCardDone(i) local c = pstfCard(i) return c ~= nil and c.done and not c.active end
function pstfActiveName()
	for i, c in ipairs(pstfTbc.cards) do
		if c.active then return pstfCardName(i) end
	end
	return ''
end
function pstfTitle()
	if not pstfTbc.on then return '' end
	local who = pstfActiveName()
	if who == '' then return 'Round ' .. pstfTbc.round end
	return 'Round ' .. pstfTbc.round .. ':  ' .. who
end
function pstfParty() return pstfTbc.on and pstfTbc.party end
function pstfMoveText()
	return string.format('%d / %d ft', math.floor(pstfTbc.move + 0.5), math.floor(pstfTbc.maxMove + 0.5))
end
function pstfActionText()
	if pstfTbc.acting then return 'Action in progress' end
	if pstfTbc.used then return 'No actions left' end
	return 'Action available'
end
function pstfActionFree() return pstfParty() and not pstfTbc.used and not pstfTbc.acting end
function pstfActionGone() return pstfParty() and (pstfTbc.used or pstfTbc.acting) end
function pstfAttacksOn() return pstfParty() and (pstfTbc.atk or -1) >= 0 and (pstfTbc.acting or not pstfTbc.used) end
function pstfAttacksText() return 'Attacks: ' .. tostring(pstfTbc.atk or 0) end
function pstfFloat(i) return pstfTbc.on and pstfTbc.floats and pstfTbc.floats[i] or nil end
function pstfFloatOn(i) return pstfFloat(i) ~= nil end
function pstfFloatText(i) local f = pstfFloat(i) return f and f.t or '' end
function pstfCardClick(i) local c = pstfCard(i) if c and PSTF_CenterOn then PSTF_CenterOn(c.id) end end
function pstfActionReady() return pstfParty() and not pstfTbc.used end
function pstfActionUsed() return pstfParty() and pstfTbc.used end
function pstfDotOn(i) return pstfTbc.on and pstfTbc.dots[2 * i] ~= nil end
function pstfRDotOn(i) return pstfTbc.on and pstfTbc.rdots[2 * i] ~= nil end
function pstfPathLabelOn() return pstfTbc.on and (pstfTbc.pathFt or 0) > 0 and (pstfTbc.lx or -1) >= 0 end
function pstfPathText() return tostring(pstfTbc.pathFt or 0) .. ' ft' end
function pstfMsgOn() return pstfTbc.on and (pstfTbc.msg or '') ~= '' end
function pstfMsg() return pstfTbc.msg or '' end
local function area(name, x, y, w, h) Infinity_SetArea(name, x, y, w, h) end
function pstfTbcLayout()
	if not pstfTbc.on then return end
	local sw, sh = Infinity_GetScreenSize()
	local n = #pstfTbc.cards
	if n > PSTF_CARDS then n = PSTF_CARDS end
	local cw, ch, aw, ah, gap = 54, 84, 70, 109, 6
	local total = 0
	for i = 1, n do total = total + (pstfTbc.cards[i].active and aw or cw) + gap end
	local x = math.floor((sw - total + gap) / 2)
	for i = 1, n do
		local c = pstfTbc.cards[i]
		local w, h = cw, ch
		if c.active then w, h = aw, ah end
		local y = 8
		area('pstfCardFrame' .. i, x - 3, y - 3, w + 6, h + 6)
		area('pstfCardBgF' .. i, x, y, w, h)
		area('pstfCardBgE' .. i, x, y, w, h)
		area('pstfCardImg' .. i, x, y, w, h)
		area('pstfCardTxt' .. i, x + 2, y + 2, w - 4, h - 12)
		area('pstfCardDim' .. i, x, y, w, h)
		area('pstfCardBtn' .. i, x, y, w, h)
		-- damage fills the portrait with red from the bottom (BG3), no HP bar
		local lost = 0
		if c.maxhp > 0 then lost = 1 - math.max(0, math.min(1, c.hp / c.maxhp)) end
		if c.hp <= 0 then lost = 1 end
		local rh = math.floor(h * lost + 0.5)
		if rh > 0 then area('pstfCardRed' .. i, x, y + h - rh, w, rh) else area('pstfCardRed' .. i, x, y + h, w, 0) end
		x = x + w + gap
	end
	area('pstfTitle', 0, 8 + ah + 6, sw, 22)
	for i = 1, PSTF_FLOATS do
		local f = pstfFloat(i)
		if f then area('pstfFloat' .. i, f.x - 80, f.y - 11, 160, 22) end
	end
	area('pstfMsg', 0, sh - 85 - (pstfBg3 and 32 or 160), sw, 22)
	if pstfPathLabelOn() then area('pstfPathLabel', pstfTbc.lx - 40, pstfTbc.ly - 30, 80, 20) end
	pstfBarLayout(sw, sh)
	-- dice panel on the right, above the bottom bar (the d20 itself is drawn by the engine in the world pass)
	local dx, dy = sw - 16 - 346, math.floor(sh * 0.30)
	area('pstfDiceBg', dx + 96, dy, 250, 120)
	area('pstfDiceTitle', dx + 100, dy + 6, 242, 18)
	area('pstfDiceFace', dx + 18, dy + 42, 64, 40)
	area('pstfDiceSum', dx + 104, dy + 36, 218, 22)
	area('pstfDiceResult', dx + 104, dy + 64, 218, 26)
	area('pstfDiceResultBad', dx + 104, dy + 64, 218, 26)
	if PSTF_DiceAt then PSTF_DiceAt(dx + 50, dy + 62) end
end
function pstfName(ref, str)
	if ref and ref >= 0 then return Infinity_FetchString(ref) end
	return str or ''
end
function pstfDiceTitle()
	local a = pstfName(pstfTbc.dact, pstfTbc.dastr)
	if pstfTbc.dkind == 1 then return a .. ':  saving throw' end
	return a .. '  attacks  ' .. pstfName(pstfTbc.dtgt, pstfTbc.dtstr)
end
function pstfDiceOn() return pstfTbc.on and pstfTbc.dice end
function pstfDiceGood() return pstfDiceOn() and pstfTbc.dverdict and pstfTbc.dgood end
function pstfDiceBad() return pstfDiceOn() and pstfTbc.dverdict and not pstfTbc.dgood end
)LUA";

const char* kLuaBar = R"LUA(
-- BG3 hotbar: the turn-based moves, then all weapons, spells, quick items and abilities of the active
-- party member (in a fight) or of the selection (out of one, with the BG3 layout), from the engine's
-- own action bar states. With the BG3 layout it shows a page (2 rows) at a time, paged with the arrows.
pstfBar = {}
pstfBarT = 0
pstfBarPage = 1
pstfBarOwner = nil
pstfBarRows = {{'Weapons', 'ACTIONBAR_STATE_WEAPONS'}, {'Spells', 'ACTIONBAR_STATE_SPELLS'},
	{'Items', 'ACTIONBAR_STATE_ITEMS'}, {'Abilities', 'ACTIONBAR_STATE_SPECIAL_ABILITES'}}
-- Turn-based maneuvers (maneuvers.h): usable states come from C++ as pstfMv every frame of a fight.
-- Block wears the icon of the engine's own Guard button, which the hotbar leaves out (Block replaces it).
pstfMv = pstfMv or {}
pstfGuardIcon = pstfGuardIcon or nil
-- Hover shows the name; right-click opens the description (pstfMoveDesc, PSTF_DESC), written as the game's
-- own spell and item descriptions are, by the 2nd Edition rules the maneuvers follow (maneuvers.h).
pstfMoves = {
	{id = 'run', icon = 'PFSPRINT', tip = 'Run', desc = 'Run\n\nAction: 1 (the round\'s action)\nRange: Personal\nDuration: Until the start of the next turn\n\nThe character gives up attacking and spellcasting for the round to cover ground at speed. The full movement allowance is added again to whatever remains this round (31 of 31 becomes 62 of 31), so the character may move up to twice the normal rate. As with any running character, no attack may be made and no spell cast in the same round.'},
	{id = 'withdraw', icon = 'PFWITHDR', tip = 'Withdraw', desc = 'Withdraw\n\nAction: 1 (the round\'s action)\nRange: Personal\nDuration: Until the start of the next turn\n\nThe character backs carefully out of melee, keeping a guard toward every foe in reach. Only one third of the movement left this round remains, and the character moves at a walk, but no opponent may make an attack of opportunity as the character leaves its reach. A character who simply turns and walks away from melee instead allows each foe in reach one free attack.'},
	{id = 'trip', icon = 'PFTRIP', tip = 'Trip', desc = 'Trip\n\nRequirements: Warrior (fighter, ranger, paladin or a multi-class warrior); not the Nameless One\nAction: The first melee attack of the round\nSaving Throw: Death negates\n\nRather than seeking to wound, the warrior strikes at an opponent\'s legs and balance. The first melee attack this round is made at a -2 penalty to the attack roll and inflicts no damage. If it hits, the target must save vs. death or be knocked down. A prone creature suffers a 4-point penalty to its Armor Class (+4 to hit it) until its next turn, when it must spend its action regaining its feet. Selecting Trip again before the attack gives up the attempt, and the blow is struck normally.'},
	{id = 'block', icon = 'PFTRIP', guard = true, tip = 'Block', desc = 'Block\n\nRequirements: Warrior (fighter, ranger, paladin or a multi-class warrior); not the Nameless One or Morte\nAction: 1 (the round\'s action)\nDuration: Until the start of the next turn\n\nThe warrior forgoes the attacks remaining this round to parry. Each such attack becomes one block. When a melee attack would hit the warrior, a block is spent: the warrior rolls 1d20 + 4 against its own THAC0, as an attack against Armor Class 4. If that roll succeeds and the die equals or exceeds the attacker\'s own roll, the blow is turned aside harmlessly. A natural 1 always fails and a natural 20 always succeeds.'},
}
-- the description panel of a maneuver (right-click on its hotbar button)
pstfDesc = {icon = '', name = '', text = ''}
function pstfMoveDesc(m)
	local name, rest = string.match(m.desc, '^([^\n]*)\n\n(.*)$')
	pstfDesc = {icon = m.guard and pstfGuardIcon or m.icon, name = name or m.tip, text = rest or m.desc}
	Infinity_PushMenu('PSTF_DESC')
end
local function isGuard(tip)
	return type(tip) == 'string' and (string.find(tip, 'Guard') ~= nil or string.find(tip, 'Protect') ~= nil)
end
local function iconSize(icon)
	if PSTF_IconSize then
		local w, h = PSTF_IconSize(icon)
		if w and w > 0 and h and h > 0 then return w, h end
	end
	return 32, 32
end
function pstfBarCollect()
	local bar, engineBar = {}, {}
	local ba = game:GetButtonArray()
	for r, row in ipairs(pstfBarRows) do
		local st = const and const[row[2]]
		if st then
			ba:SetState(st)
			-- the engine shows 10 of a list longer than 12 at a time (scroll arrows in slots 0 and 11): every
			-- part of it, scrolled there (PSTF_BarScroll), each entry remembering its scroll index and slot
			local n = PSTF_BarCount and PSTF_BarCount() or 0
			local scrolls = {{nil, 0, 11}}
			if n > 12 and PSTF_BarScroll then
				scrolls = {}
				local idx = 0
				while idx < n do
					local first, last = PSTF_BarScroll(idx)
					if not first or first < 0 then break end
					table.insert(scrolls, {idx, first, math.min(last, first + n - idx - 1)})
					idx = idx + (last - first + 1)
				end
			end
			for _, sc in ipairs(scrolls) do
				if sc[1] then PSTF_BarScroll(sc[1]) end
				local i = 0
				for icon, on in string.gmatch(PSTF_BarSlots(), '([^:;]*):(%d);') do
					if icon ~= '' and i >= sc[2] and i <= sc[3] then
						local tip = actionBarTooltip and actionBarTooltip[i]
						-- the engine's Guard button (weapons / abilities rows; a spell such as Protection from Evil is no Guard)
						if pstfMv.on and (r == 1 or r == 4) and isGuard(tip) then
							pstfGuardIcon = icon  -- Block takes its place and its icon
						else
							local w, h = iconSize(icon)
							table.insert(engineBar, {row = r, state = st, slot = i, scroll = sc[1], icon = icon, on = on == '1', tip = tip, w = w, h = h})
						end
					end
					i = i + 1
				end
			end
			if scrolls[1] and scrolls[1][1] then PSTF_BarScroll(0) end
		end
	end
	if pstfMv.on then
		for _, m in ipairs(pstfMoves) do
			-- who cannot trip or block (not a warrior, the Nameless One, Morte) does not get the button
			local hidden = (m.id == 'trip' and pstfMv.mayTrip == false) or (m.id == 'block' and pstfMv.mayBlock == false)
			if not hidden then
				local icon = m.guard and pstfGuardIcon or m.icon
				local w, h = iconSize(icon)
				table.insert(bar, {row = 0, move = true, id = m.id, m = m, icon = icon, on = true, tip = m.tip, w = w, h = h})
			end
		end
	end
	for _, e in ipairs(engineBar) do table.insert(bar, e) end
	-- a new owner (the next party member's turn) starts on the first page
	local owner = pstfTbc.on and pstfTbc.slot or -1
	if owner ~= pstfBarOwner then pstfBarPage = 1 pstfBarOwner = owner end
	pstfBar = bar
	pstfBarT = Infinity_GetClockTicks()
end
-- Out of a fight (BG3 layout) the bar follows the selection: collected once a second, never while a
-- spell, ability or item is being aimed (game state 0 = normal cursor) or off the world screen.
function pstfBarTick()
	if pstfTbc.on or not pstfBg3 then return false end
	local now = Infinity_GetClockTicks()
	if now - pstfBarT > 1000 and game:GetState() == 0 and e:GetActiveEngine() == e:GetEngineWorld() then
		pstfBarCollect()
	end
	return false
end
function pstfBarPerPage() return pstfBg3 and PSTF_BAR_COLS * PSTF_BAR_ROWS or PSTF_BAR end
function pstfBarPages() return math.max(1, math.ceil(#pstfBar / pstfBarPerPage())) end
function pstfBarShown() return (pstfTbc.on and pstfTbc.party) or (pstfBg3 and not pstfTbc.on) end
function pstfBarEntry(k)
	if not pstfBarShown() or k > pstfBarPerPage() then return nil end
	local off = pstfBg3 and (pstfBarPage - 1) * pstfBarPerPage() or 0
	return pstfBar[k + off]
end
function pstfBarOn(k) return pstfBarEntry(k) ~= nil end
function pstfBarIcon(k) local e = pstfBarEntry(k) return e and e.icon or '' end
function pstfBarTip(k)
	local e = pstfBarEntry(k)
	if not e then return nil end
	if e.move and e.id == 'trip' and pstfMv.armed then return 'Trip armed: attack a foe (click again to cancel)' end
	if e.move and e.id == 'block' and (pstfMv.stacks or 0) > 0 then return 'Blocking: ' .. pstfMv.stacks .. ' block(s) left' end
	return e.tip
end
function pstfBarUsable(k)
	local e = pstfBarEntry(k)
	if e == nil or not e.on then return false end
	if e.move then return pstfTbc.on and pstfMv[e.id] == true end
	if pstfTbc.on then return not pstfTbc.used end
	return true
end
function pstfBarClick(k)
	local e = pstfBarEntry(k)
	if not e or not e.on then return end
	if e.move then
		if pstfTbc.on and pstfMv[e.id] then PSTF_Maneuver(e.id) end
		return
	end
	local ba = game:GetButtonArray()
	ba:SetState(e.state)
	if e.scroll then PSTF_BarScroll(e.scroll) end
	ba:OnLButtonPressed(e.slot)
end
-- Right-click: the description, as the engine's own bar shows it (spell / item description), or ours (maneuvers).
function pstfBarRClick(k)
	local e = pstfBarEntry(k)
	if not e then return end
	if e.move then
		if e.m then pstfMoveDesc(e.m) end
		return
	end
	local ba = game:GetButtonArray()
	ba:SetState(e.state)
	if e.scroll then PSTF_BarScroll(e.scroll) end
	ba:OnRButtonPressed(e.slot)
end
function pstfBarPagesOn() return pstfBg3 and pstfBarShown() and pstfBarPages() > 1 end
function pstfBarCanPrev() return pstfBarPage > 1 end
function pstfBarCanNext() return pstfBarPage < pstfBarPages() end
function pstfBarPrev() if pstfBarPage > 1 then pstfBarPage = pstfBarPage - 1 end end
function pstfBarNext() if pstfBarPage < pstfBarPages() then pstfBarPage = pstfBarPage + 1 end end
function pstfBarPageTip() return 'Page ' .. pstfBarPage .. ' / ' .. pstfBarPages() end
-- the icon keeps its own aspect inside its square (item icons are not square)
local function iconArea(k, e, x, y, size)
	local w, h = e and e.w or size, e and e.h or size
	local s = math.min(size / w, size / h)
	local fw, fh = math.max(1, math.floor(w * s + 0.5)), math.max(1, math.floor(h * s + 0.5))
	Infinity_SetArea('pstfBarBtn' .. k, x + math.floor((size - fw) / 2), y + math.floor((size - fh) / 2), fw, fh)
	Infinity_SetArea('pstfBarHit' .. k, x, y, size, size)
end
local function hide(name) Infinity_SetArea(name, -100, -100, 1, 1) end
function pstfBarLayout(sw, sh)
	local placed = {}
	if pstfBg3 then
		if pstfBarPage > pstfBarPages() then pstfBarPage = pstfBarPages() end
		-- where the portraits were in the bottom bar: a page of 2 rows x PSTF_BAR_COLS, the page arrows
		-- right of it, End Turn at the right end; the turn strip (action, attacks, movement) in the
		-- bar's left part where the weapon/spell/item/ability buttons were
		local x0, y0 = math.floor((sw - 1024) / 2), sh - 85
		local size, step = 38, 41
		for k = 1, math.min(pstfBarPerPage(), PSTF_BAR) do
			local e = pstfBarEntry(k)
			if e then
				local col, row = (k - 1) % PSTF_BAR_COLS, math.floor((k - 1) / PSTF_BAR_COLS)
				local x, y = x0 + 342 + col * 42, y0 + 3 + row * step
				Infinity_SetArea('pstfBarBg' .. k, x - 1, y - 1, size + 2, size + 2)
				iconArea(k, e, x, y, size)
				placed[k] = true
			end
		end
		local px = x0 + 342 + PSTF_BAR_COLS * 42
		area('pstfPageUp', px, y0 + 5, 22, 36)
		area('pstfPageDn', px, y0 + 44, 22, 36)
		area('pstfEndTurn', x0 + 788, y0 + 13, 60, 60)
		local sx, sy = x0 + 112, y0
		area('pstfPanel', sx - 2, sy + 3, 182, 79)
		area('pstfActionPip', sx + 6, sy + 13, 10, 10)
		area('pstfActionPipUsed', sx + 6, sy + 13, 10, 10)
		area('pstfActionText', sx + 22, sy + 6, 154, 24)
		area('pstfActionTextUsed', sx + 22, sy + 6, 154, 24)
		area('pstfAttacksText', sx + 6, sy + 30, 170, 22)
		area('pstfMoveBack', sx + 4, sy + 56, 170, 20)
		local share = 0
		if pstfTbc.maxMove and pstfTbc.maxMove > 0 then share = math.max(0, math.min(1, pstfTbc.move / pstfTbc.maxMove)) end
		area('pstfMoveFill', sx + 4, sy + 56, math.max(1, math.floor(170 * share)), 20)
		area('pstfMoveText', sx + 4, sy + 56, 170, 20)
	else
		-- one row (BG3) above the turn strip, a wider gap between the groups; wraps past 18
		local n = math.min(#pstfBar, PSTF_BAR)
		local size, gap, sep = 40, 4, 14
		local perRow = 18
		local rows = math.max(1, math.ceil(n / perRow))
		local py = sh - 128
		for row = 0, rows - 1 do
			local first, last = row * perRow + 1, math.min(n, (row + 1) * perRow)
			local width = 0
			for k = first, last do
				width = width + size + gap
				if k > first and pstfBar[k].row ~= pstfBar[k - 1].row then width = width + sep end
			end
			local x = math.floor(sw / 2 - (width - gap) / 2)
			local y = py - 8 - (rows - row) * (size + gap)
			for k = first, last do
				if k > first and pstfBar[k].row ~= pstfBar[k - 1].row then x = x + sep end
				Infinity_SetArea('pstfBarBg' .. k, x - 2, y - 2, size + 4, size + 4)
				iconArea(k, pstfBar[k], x, y, size)
				placed[k] = true
				x = x + size + gap
			end
		end
		hide('pstfPageUp') hide('pstfPageDn')
		local stripX, stripY = math.floor(sw / 2) - 250, sh - 128
		area('pstfPanel', stripX - 6, stripY - 2, 512, 28)
		area('pstfActionPip', stripX + 2, stripY + 7, 10, 10)
		area('pstfActionPipUsed', stripX + 2, stripY + 7, 10, 10)
		area('pstfActionText', stripX + 18, stripY, 150, 24)
		area('pstfActionTextUsed', stripX + 18, stripY, 150, 24)
		area('pstfAttacksText', stripX + 170, stripY, 100, 24)
		area('pstfMoveBack', stripX + 276, stripY + 4, 200, 18)
		local share = 0
		if pstfTbc.maxMove and pstfTbc.maxMove > 0 then share = math.max(0, math.min(1, pstfTbc.move / pstfTbc.maxMove)) end
		area('pstfMoveFill', stripX + 276, stripY + 4, math.max(1, math.floor(200 * share)), 18)
		area('pstfMoveText', stripX + 276, stripY + 4, 200, 18)
		area('pstfEndTurn', stripX + 508, stripY - 22, 64, 64)
	end
	for k = 1, PSTF_BAR do
		if not placed[k] then hide('pstfBarBtn' .. k) hide('pstfBarBg' .. k) hide('pstfBarHit' .. k) end
	end
end
-- PSTF_BAR's own frame hook: out of a fight nobody else lays the bar out.
function pstfBarMenuTick()
	pstfBarTick()
	if not pstfTbc.on then
		local sw, sh = Infinity_GetScreenSize()
		pstfBarLayout(sw, sh)
	end
	return false
end
)LUA";

const char* kLuaStatus = R"LUA(
-- Status icons (buffs red-framed debuffs gold-framed... as in the GemRB fork): next to each party
-- portrait and on the hover card. The DLL fills pstfStat[slot] / pstfHov.icons with
-- {b = bam, n = name strref, r = rounds left, d = debuff, fit = {x,y,w,h}, f = {x,y,w,h, ...}} where fit is
-- the icon inside its box and f the drained frame (offsets in the box), then calls pstfStatusLayout().
pstfStat = {}
pstfHov = {on = false, icons = {}}
function pstfSt(p, i)
	if p < 0 then return pstfHov.on and pstfHov.icons and pstfHov.icons[i] or nil end
	local s = pstfStat[p]
	return s and s[i] or nil
end
function pstfStOn(p, i) return pstfSt(p, i) ~= nil end
function pstfStIcon(p, i) local s = pstfSt(p, i) return s and s.b or '' end
function pstfStTip(p, i)
	local s = pstfSt(p, i)
	if not s then return nil end
	local name = (s.n and s.n >= 0) and Infinity_FetchString(s.n) or s.b
	return name .. '\n' .. (s.d and 'Debuff' or 'Buff') .. ': ' .. s.r .. (s.r == 1 and ' round' or ' rounds') .. ' left'
end
function pstfStSeg(p, i, k, debuff)
	local s = pstfSt(p, i)
	return s ~= nil and (s.d and true or false) == debuff and s.f and s.f[4 * k] ~= nil
end
local function stName(p, i) return 'pstfSt' .. (p < 0 and 'H' or tostring(p)) .. '_' .. i end
local function placeIcon(p, i, x, y, size)
	local s = pstfSt(p, i)
	local n = stName(p, i)
	if not s then
		Infinity_SetArea(n .. 'O', -100, -100, 1, 1)
		return
	end
	Infinity_SetArea(n .. 'O', x, y, size, size)
	Infinity_SetArea(n .. 'B', x + 2, y + 2, size - 4, size - 4)
	local f = s.fit or {0, 0, size, size}
	Infinity_SetArea(n .. 'I', x + f[1], y + f[2], f[3], f[4])
	for k = 1, 5 do
		local q = s.f and s.f[4 * k] and k or nil
		if q then
			local a, b, c, d = s.f[4 * k - 3], s.f[4 * k - 2], s.f[4 * k - 1], s.f[4 * k]
			Infinity_SetArea(n .. 'G' .. k, x + a, y + b, c, d)
			Infinity_SetArea(n .. 'R' .. k, x + a, y + b, c, d)
		end
	end
end
-- where the portrait of party slot p sits: BG3 column (pstfPortRect) or the engine bar
function pstfPortOrigin(p)
	if pstfBg3 and pstfPortRect then return pstfPortRect(p) end
	local sw, sh = Infinity_GetScreenSize()
	return math.floor((sw - 1024) / 2) + 338 + p * 85, sh - 85, 85
end
function pstfStatusLayout()
	local size, gap, per = PSTF_ST_PX, 3, 4
	local sw, sh = Infinity_GetScreenSize()
	for p = 0, PSTF_ST_SLOTS - 1 do
		local x, y, g = pstfPortOrigin(p)
		for i = 1, PSTF_ST_ICONS do
			local col, row = (i - 1) % per, math.floor((i - 1) / per)
			local ix, iy
			if pstfBg3 then
				ix, iy = x + g + 4 + col * (size + gap), y + 4 + row * (size + gap)
			else
				-- above the engine bar's portrait
				ix, iy = x + 4 + col * (size + gap), y - 2 * (size + gap) - 2 + row * (size + gap)
			end
			placeIcon(p, i, ix, iy, size)
		end
	end
	pstfHovLayout(sw, sh)
end
function pstfHovOn() return pstfHov.on end
function pstfHovName()
	if (pstfHov.name or -1) >= 0 then return Infinity_FetchString(pstfHov.name) end
	return pstfHov.str or ''
end
function pstfHovHpOn() return pstfHov.on and (pstfHov.max or 0) > 0 end
function pstfHovHp() return tostring(pstfHov.hp or 0) .. ' / ' .. tostring(pstfHov.max or 0) end
function pstfHovHitOn() return pstfHov.on and pstfHov.foe and (pstfHov.chance or -1) >= 0 end
function pstfHovHit()
	return string.format('Hit chance %d%%     AC %d     needs %d', pstfHov.chance, pstfHov.ac, pstfHov.needs)
end
-- hover card: top centre (under the initiative bar in a fight): name, HP bar, hit chance, status icons
function pstfHovLayout(sw, sh)
	if not pstfHov.on then return end
	local hw = 300
	local hx, hy = math.floor(sw / 2) - 150, pstfHov.fight and (8 + 109 + 32) or 8
	local y = hy + 3
	area('pstfHoverName', hx + 8, y, hw - 16, 18)
	y = y + 20
	area('pstfHoverBarBack', hx + 12, y, hw - 24, 12)
	local hs = 0
	if (pstfHov.max or 0) > 0 then hs = math.max(0, math.min(1, pstfHov.hp / pstfHov.max)) end
	area('pstfHoverBar', hx + 12, y, math.max(1, math.floor((hw - 24) * hs)), 12)
	area('pstfHoverHp', hx + 12, y - 2, hw - 24, 16)
	y = y + 15
	if pstfHovHitOn() then
		area('pstfHoverHit', hx + 8, y, hw - 16, 18)
		y = y + 20
	end
	local n = #(pstfHov.icons or {})
	if n > PSTF_HOV_ICONS then n = PSTF_HOV_ICONS end
	if n > 0 then
		local size, gap = PSTF_HOV_PX, 4
		local ix = hx + math.floor((hw - (n * (size + gap) - gap)) / 2)
		for i = 1, PSTF_HOV_ICONS do placeIcon(-1, i, ix + (i - 1) * (size + gap), y + 2, size) end
		y = y + size + 6
	end
	area('pstfHoverBg', hx, hy, hw, y - hy + 3)
end
)LUA";

std::string Label(const std::string& name, const std::string& enabled, const std::string& body) {
  return "\tlabel\n\t{\n\t\tname \"" + name + "\"\n\t\tarea 0 0 1 1\n\t\tenabled \"" + enabled +
         "\"\n\t\tignoreEvents\n" + body + "\t}\n";
}

// One status icon: outline, dark box, the spell/item icon (tooltip: name, buff/debuff, rounds left), the
// drained frame in gold (buff) or red (debuff).
std::string StatusIconItems(const std::string& p, int i) {
  const std::string n = "pstfSt" + (p == "-1" ? std::string("H") : p) + "_" + std::to_string(i);
  const std::string args = p + ", " + std::to_string(i);
  std::string m;
  m += Label(n + "O", "pstfStOn(" + args + ")", "\t\tfill 70 56 34 255\n");
  m += Label(n + "B", "pstfStOn(" + args + ")", "\t\tfill 14 11 8 240\n");
  m += "\tbutton\n\t{\n\t\tname \"" + n + "I\"\n\t\tarea 0 0 1 1\n\t\tenabled \"pstfStOn(" + args +
       ")\"\n\t\tbam lua \"pstfStIcon(" + args + ")\"\n\t\ttooltip lua \"pstfStTip(" + args + ")\"\n\t}\n";
  for (int k = 1; k <= 5; ++k) {
    const std::string ks = std::to_string(k);
    m += Label(n + "G" + ks, "pstfStSeg(" + args + ", " + ks + ", false)", "\t\tfill 232 192 96 255\n");
    m += Label(n + "R" + ks, "pstfStSeg(" + args + ", " + ks + ", true)", "\t\tfill 226 84 60 255\n");
  }
  return m;
}

}  // namespace

StatusIconView MakeIconView(const StatusIcon& s, const std::string& bam, int32_t name_ref, int bam_w, int bam_h, int box) {
  StatusIconView v;
  v.bam = bam;
  v.name_ref = name_ref;
  v.rounds = s.rounds;
  v.debuff = s.debuff;
  const int inner = box - 2 * kStatusFramePx - 2;
  v.fit = FitIcon(bam_w, bam_h, inner);
  v.fit.x += kStatusFramePx + 1;
  v.fit.y += kStatusFramePx + 1;
  v.frame = DrainFrame(0, 0, box, kStatusFramePx, s.share);
  return v;
}

std::string BuildHudMenu(bool bg3, int party_max) {
  const int slots = party_max > kStatusSlots ? kStatusSlots : party_max;
  std::string m = "`\nPSTF_BAR = " + std::to_string(kHudBar) + "\nPSTF_BAR_COLS = " + std::to_string(kBarCols) +
                  "\nPSTF_BAR_ROWS = " + std::to_string(kBarRows) + "\nPSTF_CARDS = " + std::to_string(kHudCards) +
                  "\nPSTF_DOTS = " + std::to_string(kHudDots) + "\nPSTF_FLOATS = " + std::to_string(kHudFloats) +
                  "\nPSTF_PARTY_MAX = " + std::to_string(party_max) + "\nPSTF_ST_SLOTS = " + std::to_string(slots) +
                  "\nPSTF_ST_ICONS = " + std::to_string(kStatusIcons) + "\nPSTF_ST_PX = " + std::to_string(kStatusPortraitPx) +
                  "\nPSTF_HOV_ICONS = " + std::to_string(kHoverIcons) + "\nPSTF_HOV_PX = " + std::to_string(kStatusHoverPx) +
                  "\npstfBg3 = " + (bg3 ? "pstfBg3Ok == true" : "false") + "\n" + kLua + kLuaBar + kLuaStatus + "`\n";
  // --- the hotbar and the turn strip: their own menu, so the bar can stay up out of fights (BG3) ---
  m += "menu\n{\n\tname 'PSTF_BAR'\n\talign left top\n\tignoreEsc\n";
  m += "\tonopen \"pstfBarOpen = true pstfBarT = 0\"\n\tonclose \"pstfBarOpen = false\"\n";
  m += Label("pstfBarTick", "pstfBarMenuTick()", "");
  // The turn strip: "Action available" / "No actions left", "Attacks: N", the movement bar with
  // "23 / 31 ft" in it; End Turn.
  m += Label("pstfPanel", "pstfParty()", "\t\tfill 12 10 8 200\n");
  m += Label("pstfActionPip", "pstfActionFree()", "\t\tfill 90 200 90 255\n");
  m += Label("pstfActionPipUsed", "pstfActionGone()", "\t\tfill 70 60 50 255\n");
  m += Label("pstfActionText", "pstfActionFree()",
             "\t\ttext lua \"pstfActionText()\"\n\t\ttext style \"normal\"\n\t\ttext color lua \"0x78E08C\"\n\t\talign left center\n");
  m += Label("pstfActionTextUsed", "pstfActionGone()",
             "\t\ttext lua \"pstfActionText()\"\n\t\ttext style \"normal\"\n\t\ttext color lua \"0x8C9AA0\"\n\t\talign left center\n");
  m += Label("pstfAttacksText", "pstfAttacksOn()",
             "\t\ttext lua \"pstfAttacksText()\"\n\t\ttext style \"normal\"\n\t\talign left center\n");
  m += Label("pstfMoveBack", "pstfParty()", "\t\tfill 0 0 0 220\n");
  m += Label("pstfMoveFill", "pstfParty()", "\t\tfill 150 108 34 240\n");
  m += Label("pstfMoveText", "pstfParty()",
             "\t\ttext lua \"pstfMoveText()\"\n\t\ttext style \"normal\"\n\t\ttext color lua \"0xFFFFFF\"\n\t\talign center center\n");
  m += "\tbutton\n\t{\n\t\tname \"pstfEndTurn\"\n\t\tarea 0 0 1 1\n\t\tenabled \"pstfParty()\"\n"
       "\t\tbam \"PFENDTRN\"\n\t\ttooltip \"End Turn (End key)\"\n\t\taction \"PSTF_EndTurn()\"\n\t}\n";
  m += "\tbutton\n\t{\n\t\tname \"pstfPageUp\"\n\t\tarea 0 0 1 1\n\t\tenabled \"pstfBarPagesOn()\"\n"
       "\t\tbam \"PFPAGEUP\"\n\t\tgreyscale lua \"not pstfBarCanPrev()\"\n\t\ttooltip lua \"pstfBarPageTip()\"\n"
       "\t\taction \"pstfBarPrev()\"\n\t}\n";
  m += "\tbutton\n\t{\n\t\tname \"pstfPageDn\"\n\t\tarea 0 0 1 1\n\t\tenabled \"pstfBarPagesOn()\"\n"
       "\t\tbam \"PFPAGEDN\"\n\t\tgreyscale lua \"not pstfBarCanNext()\"\n\t\ttooltip lua \"pstfBarPageTip()\"\n"
       "\t\taction \"pstfBarNext()\"\n\t}\n";
  for (int i = 1; i <= kHudBar; ++i) {
    const std::string n = std::to_string(i);
    m += Label("pstfBarBg" + n, "pstfBarOn(" + n + ")", "\t\tfill 30 24 16 220\n");
    // the icon at its own aspect (pstfBarBtn), the click and tooltip on the whole square (pstfBarHit)
    m += "\tbutton\n\t{\n\t\tname \"pstfBarBtn" + n + "\"\n\t\tarea 0 0 1 1\n\t\tenabled \"pstfBarOn(" + n +
         ")\"\n\t\tbam lua \"pstfBarIcon(" + n + ")\"\n\t\tgreyscale lua \"not pstfBarUsable(" + n + ")\"\n\t\tignoreEvents\n\t}\n";
    m += "\tbutton\n\t{\n\t\tname \"pstfBarHit" + n + "\"\n\t\tarea 0 0 1 1\n\t\tenabled \"pstfBarOn(" + n +
         ")\"\n\t\ttooltip lua \"pstfBarTip(" + n + ")\"\n\t\tclickable lua \"fullActionbarClickable == nil or fullActionbarClickable()\"\n"
         "\t\taction \"pstfBarClick(" + n + ")\"\n\t\tactionAlt \"pstfBarRClick(" + n + ")\"\n\t}\n";
  }
  m += "}\n";
  // --- status icons next to the portraits and the hover card ---
  m += "menu\n{\n\tname 'PSTF_STATUS'\n\talign left top\n\tignoreEsc\n\tignoreFocus 1\n";
  for (int p = 0; p < slots; ++p)
    for (int i = 1; i <= kStatusIcons; ++i) m += StatusIconItems(std::to_string(p), i);
  m += Label("pstfHoverBg", "pstfHovOn()", "\t\tfill 14 11 9 215\n");
  m += Label("pstfHoverName", "pstfHovOn()", "\t\ttext lua \"pstfHovName()\"\n\t\ttext style \"label\"\n\t\talign center center\n");
  m += Label("pstfHoverBarBack", "pstfHovHpOn()", "\t\tfill 40 8 6 240\n");
  m += Label("pstfHoverBar", "pstfHovHpOn()", "\t\tfill 176 24 18 250\n");
  m += Label("pstfHoverHp", "pstfHovHpOn()", "\t\ttext lua \"pstfHovHp()\"\n\t\ttext style \"normal\"\n\t\talign center center\n");
  m += Label("pstfHoverHit", "pstfHovHitOn()", "\t\ttext lua \"pstfHovHit()\"\n\t\ttext style \"normal\"\n\t\talign center center\n");
  for (int i = 1; i <= kHoverIcons; ++i) m += StatusIconItems("-1", i);
  m += "}\n";
  // --- the fight overlay ---
  m += "menu\n{\n\tname 'PSTF_TBC'\n\talign left top\n\tignoreEsc\n";
  m += "\tonopen \"pstfTbcOpen = true\"\n\tonclose \"pstfTbcOpen = false\"\n";
  m += Label("pstfPathLabel", "pstfPathLabelOn()",
             "\t\ttext lua \"pstfPathText()\"\n\t\ttext style \"normal\"\n\t\talign center center\n");
  for (int i = 1; i <= kHudFloats; ++i) {
    const std::string n = std::to_string(i);
    m += Label("pstfFloat" + n, "pstfFloatOn(" + n + ")",
               "\t\ttext lua \"pstfFloatText(" + n + ")\"\n\t\ttext style \"label\"\n\t\ttext color lua \"0xFFFFFF\"\n\t\talign center center\n");
  }
  for (int i = 1; i <= kHudCards; ++i) {
    const std::string n = std::to_string(i), on = "pstfCardOn(" + n + ")";
    m += Label("pstfCardFrame" + n, "pstfCardActive(" + n + ")", "\t\tfill 236 196 80 255\n");
    m += Label("pstfCardBgF" + n, "pstfCardFriend(" + n + ")", "\t\tfill 20 32 64 235\n");
    m += Label("pstfCardBgE" + n, "pstfCardFoe(" + n + ")", "\t\tfill 70 14 12 235\n");
    m += Label("pstfCardImg" + n, "pstfCardHasImage(" + n + ")",
               "\t\tmosaic lua \"pstfCardImage(" + n + ")\"\n\t\talign center center\n");
    m += Label("pstfCardTxt" + n, on + " and not pstfCardHasImage(" + n + ")",
               "\t\ttext lua \"pstfCardName(" + n + ")\"\n\t\ttext style \"normal\"\n\t\talign center center\n");
    m += Label("pstfCardRed" + n, on, "\t\tfill 190 16 12 130\n");
    m += Label("pstfCardDim" + n, "pstfCardDone(" + n + ")", "\t\tfill 0 0 0 140\n");
    // a click on the card centres the camera on that combatant
    m += "\tbutton\n\t{\n\t\tname \"pstfCardBtn" + n + "\"\n\t\tarea 0 0 1 1\n\t\tenabled \"" + on +
         "\"\n\t\ttooltip lua \"pstfCardName(" + n + ")\"\n\t\taction \"pstfCardClick(" + n + ")\"\n\t}\n";
  }
  // Dice panel.
  m += Label("pstfDiceBg", "pstfDiceOn()", "\t\tfill 14 11 9 200\n");
  m += Label("pstfDiceTitle", "pstfDiceOn()", "\t\ttext lua \"pstfDiceTitle()\"\n\t\ttext style \"normal\"\n\t\talign center center\n");
  m += Label("pstfDiceFace", "pstfDiceOn()", "\t\ttext lua \"tostring(pstfTbc.dface or '')\"\n\t\ttext style \"title\"\n\t\talign center center\n");
  m += Label("pstfDiceSum", "pstfDiceOn() and pstfTbc.dlanded", "\t\ttext lua \"pstfTbc.dsum or ''\"\n\t\ttext style \"normal\"\n\t\talign left center\n");
  m += Label("pstfDiceResult", "pstfDiceGood()", "\t\ttext lua \"pstfTbc.dresult or ''\"\n\t\ttext style \"label\"\n\t\ttext color lua \"0x60DC78\"\n\t\talign left center\n");
  m += Label("pstfDiceResultBad", "pstfDiceBad()", "\t\ttext lua \"pstfTbc.dresult or ''\"\n\t\ttext style \"label\"\n\t\ttext color lua \"0x3C52E2\"\n\t\talign left center\n");
  m += Label("pstfTitle", "pstfTbc.on",
             "\t\ttext lua \"pstfTitle()\"\n\t\ttext style \"label\"\n\t\talign center center\n");
  m += Label("pstfMsg", "pstfMsgOn()",
             "\t\ttext lua \"pstfMsg()\"\n\t\ttext style \"label\"\n\t\talign center center\n");
  m += "}\n";
  // --- a maneuver's description (right-click on its hotbar button): the game's SPELL_DESCRIPTION panel ---
  m += "menu\n{\n\tname 'PSTF_DESC'\n\talign center center\n\tmodal\n\tpopupSound 1\n"
       "\tlabel\n\t{\n\t\tarea 0 0 750 580\n\t\tmosaic popup1\n\t}\n"
       "\tlabel\n\t{\n\t\tarea 140 136 88 88\n\t\ticon lua \"pstfDesc.icon\"\n\t}\n"
       "\tlabel\n\t{\n\t\tarea 137 132 94 98\n\t\tmosaic \"ITMVIG\"\n\t}\n"
       "\tlabel\n\t{\n\t\tarea 606 74 51 50\n\t\tmosaic \"ITMVIGSM\"\n\t}\n"
       "\tlabel\n\t{\n\t\tarea 609 77 46 46\n\t\ticon lua \"pstfDesc.icon\"\n\t}\n"
       "\tlabel\n\t{\n\t\tarea 284 79 266 42\n\t\ttext lua \"pstfDesc.name\"\n\t\ttext style \"label\"\n\t}\n"
       "\ttext\n\t{\n\t\tarea 294 140 412 336\n\t\ttext lua \"pstfDesc.text\"\n\t\ttext style \"normal_parchment\"\n"
       "\t\tscrollbar 'cgscrl1'\n\t}\n"
       "\tbutton\n\t{\n\t\ton escape\n\t\tarea 577 497 138 56\n\t\ttext \"DONE_BUTTON\"\n\t\ttext style \"button\"\n"
       "\t\tbam normbutt\n\t\taction \"Infinity_PopMenu('PSTF_DESC')\"\n\t}\n}\n";
  return m;
}

namespace {
void AppendEscaped(std::string* s, const std::string& val) {
  for (char c : val)
    if (c != '"' && c != '\\' && c != '\n') *s += c;
}
void AppendIcons(std::string* s, const std::vector<StatusIconView>& icons) {
  char b[160];
  *s += "{";
  for (const StatusIconView& v : icons) {
    *s += "{b=\"";
    AppendEscaped(s, v.bam);
    std::snprintf(b, sizeof b, "\",n=%d,r=%d,d=%s,fit={%d,%d,%d,%d},f={", v.name_ref, v.rounds, v.debuff ? "true" : "false",
                  v.fit.x, v.fit.y, v.fit.w, v.fit.h);
    *s += b;
    for (const Rect& r : v.frame) {
      std::snprintf(b, sizeof b, "%d,%d,%d,%d,", r.x, r.y, r.w, r.h);
      *s += b;
    }
    *s += "}},";
  }
  *s += "}";
}
}  // namespace

std::string BuildStatusLua(const std::vector<std::vector<StatusIconView>>& party, const HoverView& h) {
  std::string s = "pstfStat = {";
  for (size_t p = 0; p < party.size(); ++p) {
    s += "[" + std::to_string(p) + "]=";
    AppendIcons(&s, party[p]);
    s += ",";
  }
  char b[256];
  std::snprintf(b, sizeof b, "} pstfHov = {on=%s,fight=%s,name=%d,hp=%d,max=%d,foe=%s,chance=%d,ac=%d,needs=%d,str=\"",
                h.on ? "true" : "false", h.fight ? "true" : "false", h.name_ref, h.hp, h.max_hp, h.foe ? "true" : "false",
                h.chance, h.ac, h.needs);
  s += b;
  AppendEscaped(&s, h.name);
  s += "\",icons=";
  AppendIcons(&s, h.icons);
  s += "}";
  return s;
}

std::string BuildHudLua(const HudView& v) {
  std::string s = "pstfTbc = {on=";
  s += v.on ? "true" : "false";
  char b[256];
  std::snprintf(b, sizeof b, ",round=%u,slot=%d,party=%s,move=%.2f,maxMove=%.2f,used=%s,acting=%s,pathFt=%d,lx=%d,ly=%d",
                v.round, v.slot, v.party_turn ? "true" : "false", v.move_ft, v.max_ft,
                v.action_used ? "true" : "false", v.acting ? "true" : "false", v.path_ft, v.label_x,
                v.label_y);
  s += b;
  auto str = [&](const char* key, const std::string& val) {
    s += ",";
    s += key;
    s += "=\"";
    AppendEscaped(&s, val);
    s += "\"";
  };
  std::snprintf(b, sizeof b, ",atk=%d,atkRound=%d", v.attacks_left, v.attacks_round);
  s += b;
  s += ",floats={";
  for (const HudFloat& f : v.floats) {
    std::snprintf(b, sizeof b, "{x=%d,y=%d,t=\"", f.x, f.y);
    s += b;
    AppendEscaped(&s, f.text);
    s += "\"},";
  }
  s += "}";
  std::snprintf(b, sizeof b, ",dice=%s,dface=%d,dlanded=%s,dverdict=%s,dgood=%s,dalpha=%.2f,dkind=%d,dact=%d,dtgt=%d",
                v.dice ? "true" : "false", v.dice_face, v.dice_landed ? "true" : "false",
                v.dice_verdict ? "true" : "false", v.dice_good ? "true" : "false", v.dice_alpha, v.dice_kind,
                v.dice_actor_ref, v.dice_target_ref);
  s += b;
  str("dastr", v.dice_actor);
  str("dtstr", v.dice_target);
  str("dsum", v.dice_sum);
  str("dresult", v.dice_result);
  str("msg", v.msg);
  s += ",cards={";
  for (const HudCard& c : v.cards) {
    std::snprintf(b, sizeof b, "{id=%d,best=%d,name=%d,hp=%d,maxhp=%d,foe=%s,active=%s,done=%s,str=\"", c.id,
                  c.bestiary, c.name_ref, c.hp, c.max_hp, c.foe ? "true" : "false",
                  c.active ? "true" : "false", c.done ? "true" : "false");
    s += b;
    AppendEscaped(&s, c.name);
    s += "\"},";
  }
  auto list = [&](const char* key, const std::vector<int>& xs) {
    s += ",";
    s += key;
    s += "={";
    for (size_t i = 0; i < xs.size(); ++i) s += (i ? "," : "") + std::to_string(xs[i]);
    s += "}";
  };
  s += "}";
  list("dots", v.dots);
  list("rdots", v.red_dots);
  s += "}";
  return s;
}

}  // namespace pstf::tbc
