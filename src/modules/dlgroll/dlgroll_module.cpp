// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/dlgroll/dlgroll_module.h"

#include <windows.h>
#include <mmsystem.h>

#include <cstdio>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "core/log.h"
#include "core/tick.h"
#include "engine/dialog.h"
#include "engine/lua.h"
#include "engine/menu.h"
#include "modules/dev/lua_bridge.h"
#include "modules/dlgroll/roll_logic.h"

namespace pstf::dlgroll {
namespace {

enum class State { Locked, Failed, Won };

struct Roll {
  int index = 0;
  int32_t strref = -1;
  int stat = 0;
  int ability = 0;  // the Protagonist's value when the node was shown
  int need = 0;
  int dc = 0;
  int mod = 0;
  State state = State::Locked;
  bool rolled = false;
  int natural = 0, total = 0;
  Outcome outcome = Outcome::Fail;
};

struct G {
  bool on = false;
  bool sounds = true;
  uint32_t node = 0;
  std::vector<Roll> rolls;
  std::vector<std::pair<int, int>> wins;  // abilities this node now counts as {stat, value}
  int force = 0;                          // test: next natural roll (0 = random)
  std::wstring sound_dir;
  std::mt19937 rng{std::random_device{}()};
} g;

Roll* FindRoll(int index) {
  for (auto& r : g.rolls)
    if (r.index == index) return &r;
  return nullptr;
}

const char* StateName(State s) { return s == State::Locked ? "locked" : s == State::Failed ? "failed" : "won"; }

std::string RollLua(const Roll& r) {
  const Ability* a = FindAbility(r.stat);
  char b[512];
  std::snprintf(b, sizeof b,
                "{i=%d,strref=%d,ab=%s,ids=%s,card=%d,cur=%d,mod=%d,need=%d,dc=%d,state='%s'}", r.index, r.strref,
                engine::LuaQuote(a ? a->name : "?").c_str(), engine::LuaQuote(a ? a->ids : "?").c_str(),
                a ? a->card : 0, r.ability, r.mod, r.need, r.dc, StateName(r.state));
  return b;
}

void RunLua(const std::string& code, const char* what) {
  const std::string err = engine::LuaDoString(code.c_str());
  if (!err.empty()) Log("dlgroll: %s failed: %s", what, err.c_str());
}

// After every node: the replies the engine hid only because of an ability get a roll row.
void OnShown() {
  g.node = engine::DialogNodeSerial();
  g.rolls.clear();
  g.wins.clear();
  if (!g.node) return;
  for (const auto& r : engine::DialogReplies()) {
    if (!(r.flags & 1) || !(r.flags & 2) || r.number != -1) continue;
    for (int stat : CheckedAbilities(r.cond)) {
      const int cur = engine::ProtagonistAbility(stat);
      if (cur < 0) continue;
      const int need = NeededValue(cur, [&](int v) { return engine::DialogReplyHolds(r.index, stat, v) == 1; });
      if (need < 0) continue;
      Roll x;
      x.index = r.index;
      x.strref = r.strref;
      x.stat = stat;
      x.ability = cur;
      x.need = need;
      x.dc = Dc(need);
      x.mod = Modifier(cur);
      g.rolls.push_back(x);
      const Ability* a = FindAbility(stat);
      Log("dlgroll: node %u reply %d: %s %d needed (has %d): DC %d, modifier %+d", g.node, r.index, a ? a->ids : "?",
          need, cur, x.dc, x.mod);
      break;
    }
  }
  std::string list;
  for (const auto& r : g.rolls) list += RollLua(r) + ",";
  RunLua("pstfDlgShown(" + std::to_string(g.node) + ", {" + list + "})", "row insert");
}

// The node's replies again, with the abilities won set: numbers for the engine, rows for the list.
void Rebuild() {
  std::string list;
  int n = 0;
  for (const auto& r : engine::DialogReplies()) {
    const int holds = engine::DialogReplyHoldsWith(r.index, g.wins);
    Roll* roll = FindRoll(r.index);
    if (holds == 1 && (r.flags & 1)) {
      ++n;
      engine::SetDialogReplyNumber(r.index, n);
      const Ability* a = roll && roll->state == State::Won ? FindAbility(roll->stat) : nullptr;
      list += "{i=" + std::to_string(r.index) + ",n=" + std::to_string(n) + ",strref=" + std::to_string(r.strref) +
              (a ? ",won=" + engine::LuaQuote(a->name) : std::string()) + "},";
      continue;
    }
    engine::SetDialogReplyNumber(r.index, -1);
    if (roll && roll->state != State::Won) list += "{i=" + std::to_string(r.index) + ",roll=true},";
  }
  char cols[64];
  std::snprintf(cols, sizeof cols, "%u, %u", engine::DialogNumberColor(), engine::DialogTextColor());
  RunLua("pstfDlgRebuild(" + std::to_string(g.node) + ", {" + list + "}, " + cols + ")", "rebuild");
  Log("dlgroll: node %u rebuilt with %zu ability override(s): %d replies", g.node, g.wins.size(), n);
}

void PlayRollSound(bool ok) {
  if (!g.sounds || g.sound_dir.empty()) return;
  const std::wstring f = g.sound_dir + (ok ? L"check_success.wav" : L"check_failed.wav");
  const BOOL played = PlaySoundW(f.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
  Log("dlgroll: sound %s (%s)", ok ? "check_success" : "check_failed", played ? "playing" : "not played");
}

// --- Lua ------------------------------------------------------------------------------------------------
// PSTF_DlgRoll(index) -> natural, total, outcome (0 fail, 1 success, 2 critical fail, 3 critical success);
// -1 when the reply cannot be rolled. Plays the result sound (it carries the whole roll's cues).
int __cdecl LuaRoll(engine::LuaState* L) {
  const int i = static_cast<int>(engine::LuaNumberArg(L, 1, -1));
  Roll* r = engine::DialogNodeSerial() == g.node ? FindRoll(i) : nullptr;
  if (!r || r->state != State::Locked || r->rolled) {
    engine::LuaPushNumber(L, -1);
    return 1;
  }
  const int nat = g.force ? g.force : std::uniform_int_distribution<int>(1, 20)(g.rng);
  g.force = 0;
  r->rolled = true;
  r->natural = nat;
  r->total = nat + r->mod;
  r->outcome = Resolve(nat, r->mod, r->dc);
  const Ability* a = FindAbility(r->stat);
  Log("dlgroll: roll %s reply %d: d20 %d %+d = %d vs DC %d -> %s", a ? a->ids : "?", i, nat, r->mod, r->total, r->dc,
      r->outcome == Outcome::CritSuccess ? "critical success"
      : r->outcome == Outcome::Success   ? "success"
      : r->outcome == Outcome::CritFail  ? "critical failure"
                                         : "failure");
  PlayRollSound(Succeeded(r->outcome));
  engine::LuaPushNumber(L, nat);
  engine::LuaPushNumber(L, r->total);
  engine::LuaPushNumber(L, static_cast<int>(r->outcome));
  return 3;
}

// PSTF_DlgDone(index): the panel closed after a roll; a win reopens the node's replies with the ability.
int __cdecl LuaDone(engine::LuaState* L) {
  const int i = static_cast<int>(engine::LuaNumberArg(L, 1, -1));
  Roll* r = engine::DialogNodeSerial() == g.node ? FindRoll(i) : nullptr;
  if (!r || !r->rolled || r->state != State::Locked) {
    engine::LuaPushBool(L, false);
    return 1;
  }
  if (Succeeded(r->outcome)) {
    r->state = State::Won;
    bool have = false;
    for (auto& w : g.wins)
      if (w.first == r->stat) {
        if (w.second < r->need) w.second = r->need;
        have = true;
      }
    if (!have) g.wins.push_back({r->stat, r->need});
    Rebuild();
  } else {
    r->state = State::Failed;
    RunLua("pstfDlgFailed(" + std::to_string(i) + ")", "failed row");
  }
  engine::LuaPushBool(L, true);
  return 1;
}

// PSTF_DlgNode() -> the node on screen (0 = no dialogue), quiet (for test loops).
int __cdecl LuaNode(engine::LuaState* L) {
  engine::LuaPushNumber(L, engine::DialogNodeSerial());
  return 1;
}

int __cdecl LuaObscure(engine::LuaState* L) {
  engine::LuaPushString(L, Obscure(engine::LuaStringArg(L, 1)).c_str());
  return 1;
}

// Test probe: PSTF_DlgForce(n): the next roll's natural d20 is n.
int __cdecl LuaForce(engine::LuaState* L) {
  g.force = static_cast<int>(engine::LuaNumberArg(L, 1, 0));
  Log("lua: PSTF_DlgForce(%d)", g.force);
  return 0;
}

// Probe: PSTF_DlgState() -> "node=N rolls=i:STR:need:dc:mod:state,... wins=STR=13,... ability=STR 9 ..."
int __cdecl LuaState(engine::LuaState* L) {
  std::string s = "node=" + std::to_string(engine::DialogNodeSerial()) + " rolls=";
  for (const auto& r : g.rolls) {
    const Ability* a = FindAbility(r.stat);
    char b[96];
    std::snprintf(b, sizeof b, "%d:%s:%d:%d:%+d:%s,", r.index, a ? a->ids : "?", r.need, r.dc, r.mod, StateName(r.state));
    s += b;
  }
  s += " wins=";
  for (const auto& w : g.wins) {
    const Ability* a = FindAbility(w.first);
    s += std::string(a ? a->ids : "?") + "=" + std::to_string(w.second) + ",";
  }
  s += " numbers=";
  for (const auto& r : engine::DialogReplies())
    s += std::to_string(r.index) + ":" + std::to_string(r.number) + ",";
  s += " replies=";
  for (const auto& r : engine::DialogReplies()) {
    char b[96];
    std::snprintf(b, sizeof b, "%d/%X/%d/%d[%.24s];", r.index, r.flags, r.number, r.strref, r.cond.c_str());
    s += b;
  }
  s += " abilities=";
  for (const auto& a : kAbilities) s += std::string(a.ids) + std::to_string(engine::ProtagonistAbility(a.stat)) + ",";
  Log("lua: PSTF_DlgState -> %s", s.c_str());
  engine::LuaPushString(L, s.c_str());
  return 1;
}

// --- menus ----------------------------------------------------------------------------------------------
const char* kLua = R"LUA(
pstfDlg = pstfDlg or { rows = {}, node = 0 }
pstfDr = pstfDr or { open = false }
local function area(name, x, y, w, h) Infinity_SetArea(name, math.floor(x), math.floor(y), math.floor(w), math.floor(h)) end

-- every reply click of the dialogue list goes through here (UI.MENU's calls are rewritten to this)
function pstfDlgReplyClick(marker)
	local r = marker ~= nil and pstfDlg.rows[marker]
	if r then
		if r.state == 'locked' then pstfDrOpen(marker) end
		return
	end
	worldScreen:OnDialogReplyClick(marker)
end
pstfDlgOrigText = pstfDlgOrigText or getDialogEntryText
function getDialogEntryText(row)
	local t = dialogTable[row]
	if t and t.pstfRoll then return pstfDlgRowText(t.marker, row == worldPlayerDialogSelection) end
	return pstfDlgOrigText(row)
end
pstfDlgOrigClickable = pstfDlgOrigClickable or getDialogRowClickable
function getDialogRowClickable(row)
	local t = dialogTable[row]
	if t and t.pstfRoll then
		local r = pstfDlg.rows[t.marker]
		return r ~= nil and r.state == 'locked'
	end
	return pstfDlgOrigClickable(row)
end
-- colour codes are ^0xAABBGGRR
function pstfDlgRowText(i, hover)
	local r = pstfDlg.rows[i]
	if not r then return '' end
	if r.state == 'failed' then
		return '    ^0xff3c3c8c[' .. r.ab .. ' - failed]^- ^0xff3a2a40' .. r.obscured .. '^-'
	end
	if hover then
		return '    ^0xff7ad2ff[' .. r.ab .. ']^- ^0xffe0c8d8' .. r.obscured .. '^-'
	end
	return '    ^0xff4f9fcf[' .. r.ab .. ']^- ^0xff8a6a8a' .. r.obscured .. '^-'
end
local function pstfDlgInsert(row)
	local function place(tab)
		for k, v in ipairs(tab) do
			if v.marker ~= nil and v.marker > row.marker then table.insert(tab, k, row) return true end
		end
		return false
	end
	if place(dialogTable) then return end
	if #dialogOverflowTable > 0 then
		if not place(dialogOverflowTable) then table.insert(dialogOverflowTable, row) end
		return
	end
	table.insert(dialogTable, row)
end
function pstfDlgShown(node, list)
	pstfDlg.node = node
	pstfDlg.rows = {}
	for _, r in ipairs(list) do
		r.obscured = PSTF_DlgObscure(Infinity_FetchString(r.strref))
		pstfDlg.rows[r.i] = r
		pstfDlgInsert({ text = ' ', marker = r.i, pstfRoll = true })
	end
	if #list > 0 then
		hasDialogResponse = 1
		buildResponsesList()
		triggerChatboxScroll()
	end
end
function pstfDlgFailed(i)
	local r = pstfDlg.rows[i]
	if r then r.state = 'failed' end
	worldPlayerDialogSelection = 0
end
-- after a won roll: this node's rows again (engine numbering), won replies tagged with their ability
function pstfDlgRebuild(node, list, numc, txtc)
	if node ~= pstfDlg.node then return end
	for k = #dialogTable, 1, -1 do
		if dialogTable[k].marker ~= nil then table.remove(dialogTable, k) end
	end
	dialogOverflowTable = {}
	for _, e in ipairs(list) do
		if e.n then
			pstfDlg.rows[e.i] = nil  -- the engine's reply again: clicks go to the engine
			local tag = e.won and ('^0xff7ad2ff[' .. e.won .. ']^- ') or ''
			local text = string.format('    ^0xff%06x%d.^- %s^0xff%06x%s^-', numc, e.n, tag, txtc, Infinity_FetchString(e.strref))
			table.insert(dialogTable, { text = text, marker = e.i })
		else
			table.insert(dialogTable, { text = ' ', marker = e.i, pstfRoll = true })
		end
	end
	hasDialogResponse = 1
	worldPlayerDialogSelection = 0
	buildResponsesList()
	triggerChatboxScroll()
end
-- the glowing die beside the reply under the mouse
function pstfDlgIconOn()
	local sel = worldPlayerDialogSelection
	local t = sel and dialogTable[sel]
	if not (t and t.pstfRoll) then return false end
	local r = pstfDlg.rows[t.marker]
	if not r or r.state ~= 'locked' then return false end
	local mx, my = Infinity_GetMousePosition()
	if pstfDlgFakeMouse then mx, my = pstfDlgFakeMouse[1], pstfDlgFakeMouse[2] end  -- tests
	local x, y = Infinity_GetMenuArea('WORLD_DIALOG')
	area('pstfDlgIcon', 226 - 38, my - y - 18, 36, 36)
	return true
end
function pstfDlgIconFrame() return math.floor(Infinity_GetClockTicks() / 90) % 8 end

-- the roll panel ---------------------------------------------------------------------------------------
function pstfDrOpen(i)
	local r = pstfDlg.rows[i]
	if not r then return end
	pstfDr = { open = true, i = i, r = r, node = pstfDlg.node }
	worldPlayerDialogSelection = 0
	Infinity_PushMenu('PSTF_DLGROLL')
end
function pstfDrMs() return pstfDr.t0 and (Infinity_GetClockTicks() - pstfDr.t0) or -1 end
function pstfDrRolled() return pstfDr.t0 ~= nil end
function pstfDrRollClick()
	if pstfDr.t0 then return end
	local nat, total, out = PSTF_DlgRoll(pstfDr.i)
	if not nat or nat < 0 then return end
	pstfDr.nat, pstfDr.total, pstfDr.out = nat, total, out
	pstfDr.ok = (out == 1 or out == 3)
	pstfDr.t0 = Infinity_GetClockTicks()
end
function pstfDrClose()
	if Infinity_IsMenuOnStack('PSTF_DLGROLL') then Infinity_PopMenu('PSTF_DLGROLL') end
	pstfDr.open = false
end
function pstfDrContinue()
	local i = pstfDr.i
	pstfDrClose()
	PSTF_DlgDone(i)
end
function pstfDrBack() if not pstfDr.t0 then pstfDrClose() end end
function pstfDrContinueOn()
	local ms = pstfDrMs()
	return ms >= 0 and ms >= (pstfDr.ok and PSTF_DR.continueOk or PSTF_DR.continueFail)
end
local DIE_PATH = { {0, -70, 150}, {40, -54, 118}, {240, 0, 70}, {440, 75, 0}, {640, 43, -75}, {840, -11, -122},
	{1040, -32, -122}, {1240, -43, -100}, {1440, -32, -64}, {1640, -21, -43}, {1850, 0, 0} }
local function diePos(ms)
	if ms < 0 or ms >= DIE_PATH[#DIE_PATH][1] then return 0, 0 end
	for k = 2, #DIE_PATH do
		local a, b = DIE_PATH[k - 1], DIE_PATH[k]
		if ms <= b[1] then
			local u = (ms - a[1]) / (b[1] - a[1])
			u = u * u * (3 - 2 * u)
			return a[2] + (b[2] - a[2]) * u, a[3] + (b[3] - a[3]) * u
		end
	end
	return 0, 0
end
local function rest(n, s)
	if n < -9 then n = -9 end
	if n > 40 then n = 40 end
	return 24 + s * 50 + n + 9
end
function pstfDrPopAt() return pstfDr.ok and PSTF_DR.popOk or PSTF_DR.popFail end
function pstfDrBannerAt() return pstfDr.ok and PSTF_DR.bannerOk or PSTF_DR.bannerFail end
function pstfDrDieFrame()
	local ms = pstfDrMs()
	if ms < 0 then return rest(20, 0) end
	if ms < PSTF_DR.land then
		local u = ms / PSTF_DR.land
		return math.floor(ms * 0.020 * (1.25 - 0.55 * u)) % 24
	end
	local n = (ms < PSTF_DR.total) and pstfDr.nat or pstfDr.total
	local s = 0
	if ms < PSTF_DR.land + 90 then s = 1 end
	if ms >= PSTF_DR.total and ms < PSTF_DR.total + 140 then s = 1 end
	local pop = pstfDrPopAt()
	if ms >= pop and ms < pop + PSTF_DR.pop then
		local u = (ms - pop) / PSTF_DR.pop
		s = (u < 0.4) and 2 or 1
	end
	return rest(n, s)
end
-- burst animations: {start, length} windows
local function burst(ms, start, len)
	if ms < start or ms >= start + len then return nil end
	return math.floor((ms - start) / len * 12)
end
local function sparkWin(ms)
	if ms < 0 then return nil end
	local f = burst(ms, PSTF_DR.land, 600) or burst(ms, PSTF_DR.total, 420)
	if f == nil and pstfDr.ok then f = burst(ms, pstfDrPopAt(), 700) end
	return f
end
function pstfDrSparkFrame() return sparkWin(pstfDrMs()) or 0 end
function pstfDrSparkOn() return sparkWin(pstfDrMs()) ~= nil end
function pstfDrSmokeOn() local ms = pstfDrMs() return ms >= 0 and (not pstfDr.ok) and burst(ms, pstfDrPopAt(), 900) ~= nil end
function pstfDrSmokeFrame() return burst(pstfDrMs(), pstfDrPopAt(), 900) or 0 end
function pstfDrCardOn() local ms = pstfDrMs() return ms < PSTF_DR.bonusFade end
function pstfDrCardLit() local ms = pstfDrMs() return ms >= PSTF_DR.bonusLit and ms < PSTF_DR.bonusFade end
function pstfDrCardFrame() return pstfDr.r.card * 2 + (pstfDrCardLit() and 1 or 0) end
function pstfDrFlyOn() local ms = pstfDrMs() return ms >= PSTF_DR.bonusFly and ms < PSTF_DR.total end
function pstfDrBannerOn() local ms = pstfDrMs() return ms >= 0 and ms >= pstfDrBannerAt() end
function pstfDrBannerFrame()
	local v = ({ [0] = 2, [1] = 0, [2] = 3, [3] = 1 })[pstfDr.out or 0]
	return v * 8 + math.min(7, math.floor((pstfDrMs() - pstfDrBannerAt()) / 55))
end
function pstfDrSigned(n) return (n >= 0 and '+' or '') .. n end
function pstfDrTitle() return pstfDr.r and (pstfDr.r.ab .. ' Check') or '' end
function pstfDrBonusText() return 'Total Bonus  ' .. pstfDrSigned(pstfDr.r.mod) end
function pstfDrTick()
	if not pstfDr.open then return false end
	if pstfDlg.node ~= pstfDr.node or not hasDialogResponse then pstfDrClose() return false end
	local sw, sh = Infinity_GetScreenSize()
	local W, H = 300, 440
	local cx = math.floor(sw / 2)
	local top = math.max(44, math.floor((sh - (H + 150)) / 2) + 30)
	area('pstfDrDim', 0, 0, sw, sh)
	area('pstfDrTitle', cx - 300, top - 40, 600, 30)
	area('pstfDrPanel', cx - W / 2, top, W, H)
	area('pstfDrDcLbl', cx - 90, top + 50, 180, 20)
	area('pstfDrDcLbl2', cx - 90, top + 66, 180, 20)
	area('pstfDrDc', cx - 60, top + 88, 120, 56)
	local barY = top + math.floor(H * 0.43)
	local ms = pstfDrMs()
	local ox, oy = diePos(ms)
	local dx, dy = cx + ox, barY + oy
	area('pstfDrDie', dx - 75, dy - 75, 150, 150)
	area('pstfDrSpark', dx - 110, dy - 110, 220, 220)
	area('pstfDrSmoke', dx - 110, dy - 110, 220, 220)
	area('pstfDrRoll', cx - 100, barY + 72, 200, 34)
	area('pstfDrBack', cx - W / 2 - 4, top + H + 92, 120, 30)
	local by = top + H + 4
	area('pstfDrBonus', cx - 120, by, 240, 22)
	local cardX, cardY = cx - 31, by + 26
	area('pstfDrCard', cardX, cardY, 62, 70)
	area('pstfDrCardMod', cardX, cardY + 4, 62, 22)
	area('pstfDrCardModLit', cardX - 10, cardY - 2, 82, 30)
	area('pstfDrCardName', cardX - 40, cardY + 70, 142, 18)
	if ms >= PSTF_DR.bonusFly and ms < PSTF_DR.total then
		local u = (ms - PSTF_DR.bonusFly) / (PSTF_DR.total - PSTF_DR.bonusFly)
		u = u * u * (3 - 2 * u)
		local fx = cardX + 31 + (dx - cardX - 31) * u
		local fy = cardY + 14 + (dy - cardY - 14) * u
		area('pstfDrFly', fx - 40, fy - 18, 80, 36)
	end
	area('pstfDrBanner', cx - 230, barY + 40, 460, 120)
	area('pstfDrContinue', cx - 100, top + H + 30, 200, 34)
	return true
end
)LUA";

std::string Item(const std::string& kind, const std::string& name, const std::string& enabled, const std::string& body) {
  return "\t" + kind + "\n\t{\n\t\tname \"" + name + "\"\n\t\tarea 0 0 1 1\n\t\tenabled \"" + enabled + "\"\n" + body +
         "\t}\n";
}

std::string BuildMenu() {
  char consts[512];
  std::snprintf(consts, sizeof consts,
                "PSTF_DR = { land = %u, natural = %u, bonusLit = %u, bonusFly = %u, total = %u, bonusFade = %u, "
                "popOk = %u, popFail = %u, bannerOk = %u, bannerFail = %u, continueOk = %u, continueFail = %u, pop = %u }\n",
                RollTiming::kLand, RollTiming::kNatural, RollTiming::kBonusLit, RollTiming::kBonusFly, RollTiming::kTotal,
                RollTiming::kBonusFade, RollTiming::kPopOk, RollTiming::kPopFail, RollTiming::kBannerOk,
                RollTiming::kBannerFail, RollTiming::Continue(true), RollTiming::Continue(false), RollTiming::kPop);
  std::string m = "`\n" + std::string(consts) + kLua + "\n`\n";
  m += "menu\n{\n\tname 'PSTF_DLGROLL'\n\talign left top\n\tignoreEsc\n\tmodal\n";
  const std::string ign = "\t\tignoreEvents\n";
  m += Item("label", "pstfDrDim", "pstfDrTick()", "\t\tfill 0 0 0 175\n");
  m += Item("label", "pstfDrTitle", "pstfDr.open",
            "\t\ttext lua \"pstfDrTitle()\"\n\t\ttext style \"title\"\n\t\ttext point 22\n\t\ttext align center center\n" + ign);
  m += Item("button", "pstfDrPanel", "pstfDr.open", "\t\tbam \"PFDRPNL\"\n\t\tframe 0\n" + ign);
  m += Item("label", "pstfDrDcLbl", "pstfDr.open",
            "\t\ttext \"DIFFICULTY\"\n\t\ttext style \"normal\"\n\t\ttext point 13\n\t\ttext align center center\n\t\ttext color lua \"0xD8E4EC\"\n" + ign);
  m += Item("label", "pstfDrDcLbl2", "pstfDr.open",
            "\t\ttext \"CLASS\"\n\t\ttext style \"normal\"\n\t\ttext point 13\n\t\ttext align center center\n\t\ttext color lua \"0xD8E4EC\"\n" + ign);
  m += Item("label", "pstfDrDc", "pstfDr.open",
            "\t\ttext lua \"tostring(pstfDr.r and pstfDr.r.dc or '')\"\n\t\ttext style \"title\"\n\t\ttext point 40\n\t\ttext align center center\n\t\ttext color lua \"0xECF2F4\"\n" + ign);
  m += Item("label", "pstfDrBonus", "pstfDr.open and pstfDrCardOn()",
            "\t\ttext lua \"pstfDrBonusText()\"\n\t\ttext style \"normal\"\n\t\ttext point 15\n\t\ttext align center center\n\t\ttext color lua \"0xD0E0E8\"\n" + ign);
  m += Item("button", "pstfDrCard", "pstfDr.open and pstfDrCardOn()",
            "\t\tbam \"PFDCARD\"\n\t\tframe lua \"pstfDrCardFrame()\"\n" + ign);
  m += Item("label", "pstfDrCardMod", "pstfDr.open and pstfDrCardOn() and not pstfDrCardLit()",
            "\t\ttext lua \"pstfDrSigned(pstfDr.r.mod)\"\n\t\ttext style \"normal\"\n\t\ttext point 15\n\t\ttext align center center\n\t\ttext color lua \"0xE8F0F4\"\n" + ign);
  m += Item("label", "pstfDrCardModLit", "pstfDr.open and pstfDrCardLit() and not pstfDrFlyOn()",
            "\t\ttext lua \"pstfDrSigned(pstfDr.r.mod)\"\n\t\ttext style \"title\"\n\t\ttext point 24\n\t\ttext align center center\n\t\ttext color lua \"0x8CDCFF\"\n" + ign);
  m += Item("label", "pstfDrCardName", "pstfDr.open and pstfDrCardOn()",
            "\t\ttext lua \"pstfDr.r.ab\"\n\t\ttext style \"normal\"\n\t\ttext point 13\n\t\ttext align center center\n\t\ttext color lua \"0xB0C4D0\"\n" + ign);
  m += Item("button", "pstfDrRoll", "pstfDr.open and not pstfDrRolled()",
            "\t\tbam \"PFDBTN\"\n\t\tframe 0\n\t\ttext \"Roll Dice\"\n\t\ttext style \"button\"\n\t\ttext point 16\n\t\taction \"pstfDrRollClick()\"\n");
  m += Item("button", "pstfDrBack", "pstfDr.open and not pstfDrRolled()",
            "\t\ttext \"Back\"\n\t\ttext style \"button\"\n\t\ttext point 15\n\t\taction \"pstfDrBack()\"\n");
  m += Item("button", "pstfDrDie", "pstfDr.open", "\t\tbam \"PFD20\"\n\t\tframe lua \"pstfDrDieFrame()\"\n" + ign);
  m += Item("button", "pstfDrSpark", "pstfDr.open and pstfDrSparkOn()",
            "\t\tbam \"PFDSPRK\"\n\t\tframe lua \"pstfDrSparkFrame()\"\n" + ign);
  m += Item("button", "pstfDrSmoke", "pstfDr.open and pstfDrSmokeOn()",
            "\t\tbam \"PFDSMOK\"\n\t\tframe lua \"pstfDrSmokeFrame()\"\n" + ign);
  m += Item("label", "pstfDrFly", "pstfDr.open and pstfDrFlyOn()",
            "\t\ttext lua \"pstfDrSigned(pstfDr.r.mod)\"\n\t\ttext style \"title\"\n\t\ttext point 26\n\t\ttext align center center\n\t\ttext color lua \"0x8CDCFF\"\n" + ign);
  m += Item("button", "pstfDrBanner", "pstfDr.open and pstfDrBannerOn()",
            "\t\tbam \"PFDBANR\"\n\t\tframe lua \"pstfDrBannerFrame()\"\n" + ign);
  m += Item("button", "pstfDrContinue", "pstfDr.open and pstfDrContinueOn()",
            "\t\tbam \"PFDBTN\"\n\t\tframe 0\n\t\ttext \"Continue\"\n\t\ttext style \"button\"\n\t\ttext point 16\n\t\taction \"pstfDrContinue()\"\n");
  m += "}\n";
  return m;
}

// UI.MENU: every reply click goes through pstfDlgReplyClick; the dialogue menu gets the hover die.
std::string Transform(const std::string& ui) {
  std::string s = ui;
  const std::string from = "worldScreen:OnDialogReplyClick(", to = "pstfDlgReplyClick(";
  int n = 0;
  for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size(), ++n) s.replace(p, from.size(), to);
  const std::string icon =
      "\tbutton\n\t{\n\t\tname \"pstfDlgIcon\"\n\t\tarea 0 0 36 36\n\t\tenabled \"pstfDlgIconOn()\"\n"
      "\t\tbam \"PFDICON\"\n\t\tframe lua \"pstfDlgIconFrame()\"\n\t\tignoreEvents\n\t}\n";
  const std::string out = InsertMenuItem(s, "WORLD_DIALOG", icon);
  const bool inserted = out.size() != s.size();
  s = out;
  Log("dlgroll: UI.MENU: %d reply click call(s) rerouted, hover die %s", n, inserted ? "added" : "NOT added");
  return s;
}

// Test driver (env PSTF_DLG_TEST=1): Lua pstfDlgTest() once per main-loop iteration, also while a dialogue
// holds the world still (pstfTestTick runs only on iterations that advance the world).
void TestTick(uint64_t) {
  const std::string e = engine::LuaDoString("if pstfDlgTest then pstfDlgTest() end");
  if (!e.empty()) {
    Log("dlgroll: test error: %s", e.c_str());
    engine::LuaDoString("pstfDlgTest = nil");
  }
}

}  // namespace

void InstallDialogueRolls(const IniMap& ini) {
  if (!IniBool(ini, "dialogue.rolls", false)) {
    Log("dlgroll: dialogue rolls disabled in ini");
    return;
  }
  g.sounds = IniBool(ini, "dialogue.roll_sounds", true);
  {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    g.sound_dir = dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"pst_forever\\sounds\\";
  }
  if (!engine::LuaApiAvailable() || !engine::InstallMenuHook()) {
    Log("dlgroll: Lua/menu functions differ from the manifest; dialogue rolls off");
    return;
  }
  if (!engine::InstallDialogHooks(&OnShown)) {
    Log("dlgroll: dialogue functions differ from the manifest; dialogue rolls off");
    return;
  }
  engine::AddMenuText(BuildMenu());
  engine::AddMenuTransform(&Transform);
  dev::AddLuaFunction("PSTF_DlgRoll", &LuaRoll);
  dev::AddLuaFunction("PSTF_DlgDone", &LuaDone);
  dev::AddLuaFunction("PSTF_DlgObscure", &LuaObscure);
  dev::AddLuaFunction("PSTF_DlgForce", &LuaForce);
  dev::AddLuaFunction("PSTF_DlgState", &LuaState);
  dev::AddLuaFunction("PSTF_DlgNode", &LuaNode);
  {
    char* v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, "PSTF_DLG_TEST") == 0 && v && v[0] == '1') AddTickHandler(&TestTick);
    free(v);
  }
  g.on = true;
  Log("dlgroll: installed (sounds %s)", g.sounds ? "on" : "off");
}

}  // namespace pstf::dlgroll
