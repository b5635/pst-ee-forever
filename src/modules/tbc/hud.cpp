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
	return string.format('Movement  %d / %d ft', math.floor(pstfTbc.move + 0.5), math.floor(pstfTbc.maxMove + 0.5))
end
function pstfActionText()
	if pstfTbc.acting then return 'Action: in progress' end
	if pstfTbc.used then return 'Action: used' end
	return 'Action: ready  (attack, cast or use)'
end
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
		-- damage fills the portrait with red from the bottom (BG3), no HP bar
		local lost = 0
		if c.maxhp > 0 then lost = 1 - math.max(0, math.min(1, c.hp / c.maxhp)) end
		if c.hp <= 0 then lost = 1 end
		local rh = math.floor(h * lost + 0.5)
		if rh > 0 then area('pstfCardRed' .. i, x, y + h - rh, w, rh) else area('pstfCardRed' .. i, x, y + h, w, 0) end
		x = x + w + gap
	end
	area('pstfTitle', 0, 8 + ah + 6, sw, 22)
	local px, py = math.floor(sw / 2) - 210, sh - 158
	area('pstfPanel', px, py, 420, 70)
	area('pstfActionPip', px + 12, py + 12, 16, 16)
	area('pstfActionPipUsed', px + 12, py + 12, 16, 16)
	area('pstfActionText', px + 36, py + 8, 230, 22)
	area('pstfMoveBack', px + 12, py + 42, 250, 14)
	local share = 0
	if pstfTbc.maxMove > 0 then share = math.max(0, math.min(1, pstfTbc.move / pstfTbc.maxMove)) end
	area('pstfMoveFill', px + 12, py + 42, math.max(1, math.floor(250 * share)), 14)
	area('pstfMoveText', px + 12, py + 26, 250, 16)
	area('pstfEndTurnBg', px + 284, py + 14, 124, 42)
	area('pstfEndTurn', px + 284, py + 14, 124, 42)
	area('pstfMsg', 0, py - 30, sw, 22)
	for i = 1, PSTF_DOTS do
		local d = pstfTbc.dots
		if d[2 * i] then area('pstfDot' .. i, d[2 * i - 1] - 3, d[2 * i] - 3, 7, 7) end
		d = pstfTbc.rdots
		if d[2 * i] then area('pstfRDot' .. i, d[2 * i - 1] - 3, d[2 * i] - 3, 7, 7) end
	end
	if pstfPathLabelOn() then area('pstfPathLabel', pstfTbc.lx - 40, pstfTbc.ly - 30, 80, 20) end
	pstfBarLayout(sw, py)
	-- hover card under the title (BG3: name, HP bar, hit chance)
	local hw, hx, hy = 300, math.floor(sw / 2) - 150, 8 + ah + 32
	area('pstfHoverBg', hx, hy, hw, 58)
	area('pstfHoverName', hx + 8, hy + 3, hw - 16, 18)
	area('pstfHoverBarBack', hx + 12, hy + 23, hw - 24, 12)
	local hs = 0
	if (pstfTbc.hmax or 0) > 0 then hs = math.max(0, math.min(1, pstfTbc.hhp / pstfTbc.hmax)) end
	area('pstfHoverBar', hx + 12, hy + 23, math.max(1, math.floor((hw - 24) * hs)), 12)
	area('pstfHoverHp', hx + 12, hy + 21, hw - 24, 16)
	area('pstfHoverHit', hx + 8, hy + 38, hw - 16, 18)
	-- dice panel on the left (the d20 itself is drawn by the engine in the world pass)
	local dx, dy = 16, math.floor(sh * 0.30)
	area('pstfDiceBg', dx + 96, dy, 250, 120)
	area('pstfDiceTitle', dx + 100, dy + 6, 242, 18)
	area('pstfDiceFace', dx + 18, dy + 42, 64, 40)
	area('pstfDiceSum', dx + 104, dy + 36, 218, 22)
	area('pstfDiceResult', dx + 104, dy + 64, 218, 26)
	area('pstfDiceResultBad', dx + 104, dy + 64, 218, 26)
	if PSTF_DiceAt then PSTF_DiceAt(dx + 50, dy + 62) end
end
-- BG3 hotbar: all weapons, spells, quick items and abilities of the active party member at once.
pstfBar = {}
pstfBarRows = {{'Weapons', 'ACTIONBAR_STATE_WEAPONS'}, {'Spells', 'ACTIONBAR_STATE_SPELLS'},
	{'Items', 'ACTIONBAR_STATE_ITEMS'}, {'Abilities', 'ACTIONBAR_STATE_SPECIAL_ABILITES'}}
function pstfBarCollect()
	local bar = {}
	local ba = game:GetButtonArray()
	for r, row in ipairs(pstfBarRows) do
		local st = const and const[row[2]]
		if st then
			ba:SetState(st)
			local i = 0
			for icon, on in string.gmatch(PSTF_BarSlots(), '([^:;]*):(%d);') do
				if icon ~= '' then
					table.insert(bar, {row = r, state = st, slot = i, icon = icon, on = on == '1', tip = actionBarTooltip and actionBarTooltip[i]})
				end
				i = i + 1
			end
		end
	end
	pstfBar = bar
end
function pstfBarEntry(k) return pstfTbc.on and pstfTbc.party and pstfBar[k] or nil end
function pstfBarOn(k) return pstfBarEntry(k) ~= nil end
function pstfBarIcon(k) local e = pstfBarEntry(k) return e and e.icon or '' end
function pstfBarTip(k) local e = pstfBarEntry(k) return e and e.tip or nil end
function pstfBarUsable(k) local e = pstfBarEntry(k) return e ~= nil and e.on and not pstfTbc.used end
function pstfBarClick(k)
	local e = pstfBarEntry(k)
	if not e or not e.on then return end
	game:GetButtonArray():SetState(e.state)
	game:GetButtonArray():OnLButtonPressed(e.slot)
end
function pstfBarLayout(sw, py)
	-- one row (BG3), a wider gap between weapons, spells, items and abilities; wraps past 18
	local size, gap, sep = 40, 4, 14
	local n = #pstfBar
	local perRow = 18
	local rows = math.max(1, math.ceil(n / perRow))
	local placed = {}
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
			Infinity_SetArea('pstfBarBtn' .. k, x, y, size, size)
			placed[k] = true
			x = x + size + gap
		end
	end
	for k = 1, PSTF_BAR do
		if not placed[k] then Infinity_SetArea('pstfBarBtn' .. k, -100, -100, 1, 1) Infinity_SetArea('pstfBarBg' .. k, -100, -100, 1, 1) end
	end
end
function pstfHoverOn() return pstfTbc.on and pstfTbc.hover end
function pstfHoverName()
	if (pstfTbc.hname or -1) >= 0 then return Infinity_FetchString(pstfTbc.hname) end
	return pstfTbc.hstr or ''
end
function pstfHoverHp() return tostring(pstfTbc.hhp or 0) .. ' / ' .. tostring(pstfTbc.hmax or 0) end
function pstfHoverHitOn() return pstfHoverOn() and pstfTbc.hfoe and (pstfTbc.hchance or -1) >= 0 end
function pstfHoverHit()
	return string.format('Hit chance %d%%     AC %d     needs %d', pstfTbc.hchance, pstfTbc.hac, pstfTbc.hneeds)
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

std::string Label(const std::string& name, const std::string& enabled, const std::string& body) {
  return "\tlabel\n\t{\n\t\tname \"" + name + "\"\n\t\tarea 0 0 1 1\n\t\tenabled \"" + enabled +
         "\"\n\t\tignoreEvents\n" + body + "\t}\n";
}

}  // namespace

std::string BuildHudMenu() {
  std::string m = "`\nPSTF_BAR = " + std::to_string(kHudBar) + "\nPSTF_CARDS = " + std::to_string(kHudCards) + "\nPSTF_DOTS = " +
                  std::to_string(kHudDots) + "\n" + kLua + "`\n";
  m += "menu\n{\n\tname 'PSTF_TBC'\n\talign left top\n\tignoreEsc\n";
  m += "\tonopen \"pstfTbcOpen = true\"\n\tonclose \"pstfTbcOpen = false\"\n";
  // World overlay first, so the bars draw over it.
  for (int i = 1; i <= kHudDots; ++i) {
    const std::string n = std::to_string(i);
    m += Label("pstfDot" + n, "pstfDotOn(" + n + ")", "\t\tfill 255 214 90 230\n");
    m += Label("pstfRDot" + n, "pstfRDotOn(" + n + ")", "\t\tfill 200 40 30 200\n");
  }
  m += Label("pstfPathLabel", "pstfPathLabelOn()",
             "\t\ttext lua \"pstfPathText()\"\n\t\ttext style \"normal\"\n\t\talign center center\n");
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
  }
  // Hover card.
  m += Label("pstfHoverBg", "pstfHoverOn()", "\t\tfill 14 11 9 215\n");
  m += Label("pstfHoverName", "pstfHoverOn()", "\t\ttext lua \"pstfHoverName()\"\n\t\ttext style \"label\"\n\t\talign center center\n");
  m += Label("pstfHoverBarBack", "pstfHoverOn()", "\t\tfill 40 8 6 240\n");
  m += Label("pstfHoverBar", "pstfHoverOn()", "\t\tfill 176 24 18 250\n");
  m += Label("pstfHoverHp", "pstfHoverOn()", "\t\ttext lua \"pstfHoverHp()\"\n\t\ttext style \"normal\"\n\t\talign center center\n");
  m += Label("pstfHoverHit", "pstfHoverHitOn()", "\t\ttext lua \"pstfHoverHit()\"\n\t\ttext style \"normal\"\n\t\talign center center\n");
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
  m += Label("pstfPanel", "pstfParty()", "\t\tfill 12 10 8 210\n");
  for (int i = 1; i <= kHudBar; ++i) {
    const std::string n = std::to_string(i);
    m += Label("pstfBarBg" + n, "pstfBarOn(" + n + ")", "\t\tfill 60 44 24 230\n");
    m += "\tbutton\n\t{\n\t\tname \"pstfBarBtn" + n + "\"\n\t\tarea 0 0 1 1\n\t\tenabled \"pstfBarOn(" + n +
         ")\"\n\t\tbam lua \"pstfBarIcon(" + n + ")\"\n\t\tgreyscale lua \"not pstfBarUsable(" + n +
         ")\"\n\t\ttooltip lua \"pstfBarTip(" + n + ")\"\n\t\taction \"pstfBarClick(" + n + ")\"\n\t}\n";
  }
  m += Label("pstfActionPip", "pstfActionReady()", "\t\tfill 90 200 90 255\n");
  m += Label("pstfActionPipUsed", "pstfActionUsed()", "\t\tfill 70 60 50 255\n");
  m += Label("pstfActionText", "pstfParty()",
             "\t\ttext lua \"pstfActionText()\"\n\t\ttext style \"normal\"\n\t\talign left center\n");
  m += Label("pstfMoveText", "pstfParty()",
             "\t\ttext lua \"pstfMoveText()\"\n\t\ttext style \"normal\"\n\t\talign left center\n");
  m += Label("pstfMoveBack", "pstfParty()", "\t\tfill 0 0 0 220\n");
  m += Label("pstfMoveFill", "pstfParty()", "\t\tfill 236 196 80 240\n");
  m += Label("pstfEndTurnBg", "pstfParty()", "\t\tfill 60 44 24 240\n");
  m += "\tbutton\n\t{\n\t\tname \"pstfEndTurn\"\n\t\tarea 0 0 1 1\n\t\tenabled \"pstfParty()\"\n"
       "\t\ttext lua \"'End Turn'\"\n\t\ttext style \"label\"\n"
       "\t\talign center center\n\t\taction \"PSTF_EndTurn()\"\n\t}\n";
  m += "}\n";
  return m;
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
    for (char c : val)
      if (c != '"' && c != '\\' && c != '\n') s += c;
    s += "\"";
  };
  std::snprintf(b, sizeof b, ",hover=%s,hname=%d,hhp=%d,hmax=%d,hfoe=%s,hchance=%d,hac=%d,hneeds=%d",
                v.hover ? "true" : "false", v.hover_name_ref, v.hover_hp, v.hover_max, v.hover_foe ? "true" : "false",
                v.hover_chance, v.hover_ac, v.hover_needs);
  s += b;
  str("hstr", v.hover_name);
  std::snprintf(b, sizeof b, ",dice=%s,dface=%d,dlanded=%s,dverdict=%s,dgood=%s,dalpha=%.2f,dkind=%d,dact=%d,dtgt=%d",
                v.dice ? "true" : "false", v.dice_face, v.dice_landed ? "true" : "false",
                v.dice_verdict ? "true" : "false", v.dice_good ? "true" : "false", v.dice_alpha, v.dice_kind,
                v.dice_actor_ref, v.dice_target_ref);
  s += b;
  str("dastr", v.dice_actor);
  str("dtstr", v.dice_target);
  str("dsum", v.dice_sum);
  str("dresult", v.dice_result);
  s += ",msg=\"";
  for (char c : v.msg)
    if (c != '"' && c != '\\' && c != '\n') s += c;
  s += "\",cards={";
  for (const HudCard& c : v.cards) {
    std::snprintf(b, sizeof b, "{id=%d,best=%d,name=%d,hp=%d,maxhp=%d,foe=%s,active=%s,done=%s,str=\"", c.id,
                  c.bestiary, c.name_ref, c.hp, c.max_hp, c.foe ? "true" : "false",
                  c.active ? "true" : "false", c.done ? "true" : "false");
    s += b;
    for (char ch : c.name)
      if (ch != '"' && ch != '\\' && ch != '\n') s += ch;
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
