// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/dlgroll/dlgroll_module.h"

#include <cstdio>
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
#include "modules/rolls/roll_panel.h"

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
  const rolls::D20 d = rolls::RollD20(r->mod, r->dc);
  r->rolled = true;
  r->natural = d.natural;
  r->total = d.total;
  r->outcome = d.outcome;
  const Ability* a = FindAbility(r->stat);
  Log("dlgroll: roll %s reply %d: d20 %d %+d = %d vs DC %d -> %s", a ? a->ids : "?", i, d.natural, r->mod, r->total,
      r->dc, rolls::OutcomeName(r->outcome));
  if (g.sounds) rolls::PlayRollSound(Succeeded(r->outcome), "dlgroll");
  engine::LuaPushNumber(L, d.natural);
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
local function area(name, x, y, w, h) Infinity_SetArea(name, math.floor(x), math.floor(y), math.floor(w), math.floor(h)) end

-- every reply click of the dialogue list goes through here (UI.MENU's calls are rewritten to this)
function pstfDlgReplyClick(marker)
	local r = marker ~= nil and pstfDlg.rows[marker]
	if r then
		if r.state == 'locked' then pstfDrOpenRow(marker) end
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

-- the roll panel (modules/rolls/roll_panel.cpp) for one reply row --------------------------------------
function pstfDrOpenRow(i)
	local r = pstfDlg.rows[i]
	if not r then return end
	local node = pstfDlg.node
	pstfDrOpen{ title = r.ab .. ' Check', ab = r.ab, card = r.card, mod = r.mod, dc = r.dc,
		roll = function() return PSTF_DlgRoll(i) end,
		done = function() PSTF_DlgDone(i) end,
		alive = function() return pstfDlg.node == node and hasDialogResponse ~= nil and hasDialogResponse ~= false end }
end
)LUA";

std::string BuildLua() { return "`\n" + std::string(kLua) + "\n`\n"; }

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
  if (!rolls::InstallRollPanel()) {
    Log("dlgroll: no roll panel; dialogue rolls off");
    return;
  }
  if (!engine::InstallDialogHooks(&OnShown)) {
    Log("dlgroll: dialogue functions differ from the manifest; dialogue rolls off");
    return;
  }
  engine::AddMenuText(BuildLua());
  engine::AddMenuTransform(&Transform);
  dev::AddLuaFunction("PSTF_DlgRoll", &LuaRoll);
  dev::AddLuaFunction("PSTF_DlgDone", &LuaDone);
  dev::AddLuaFunction("PSTF_DlgObscure", &LuaObscure);
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
