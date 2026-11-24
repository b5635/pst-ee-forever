// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/rolls/roll_panel.h"

#include <windows.h>
#include <mmsystem.h>

#include <cstdio>
#include <random>
#include <string>

#include "core/config.h"
#include "core/ini.h"
#include "core/log.h"
#include "engine/die_draw.h"
#include "engine/lua.h"
#include "engine/menu.h"
#include "modules/dev/lua_bridge.h"
#include "modules/rolls/die3d.h"

namespace pstf::rolls {
namespace {

struct G {
  bool installed = false, ok = false, use3d = false;
  int force = 0;  // test: next natural roll (0 = random)
  double throw_t0 = -1;
  die3d::Throw th = die3d::MakeThrow(1);
  std::wstring sound_dir;
  std::mt19937 rng{std::random_device{}()};
} g;

// Test probe: PSTF_RollForce(n): the next roll's natural d20 is n (any panel).
int __cdecl LuaForce(engine::LuaState* L) {
  g.force = static_cast<int>(engine::LuaNumberArg(L, 1, 0));
  Log("lua: PSTF_RollForce(%d)", g.force);
  return 0;
}

// PSTF_DieDraw(cx, cy, box, ms, now, nat, total, ok, ui_w, ui_h, t0) -> 1 drawn, 0 (show the BAM).
// Called from the die item's frame expression, so it draws in the menu's own draw order.
int __cdecl LuaDieDraw(engine::LuaState* L) {
  auto n = [L](int i) { return engine::LuaNumberArg(L, i, 0); };
  const double ms = n(4), t0 = n(11);
  if (t0 != g.throw_t0) {  // a new throw: its own spin axes
    g.throw_t0 = t0;
    g.th = die3d::MakeThrow(static_cast<uint32_t>(static_cast<int64_t>(t0) * 2654435761u + 0x51ED));
  }
  const die3d::Frame fr = die3d::At(ms, n(5), static_cast<int>(n(6)), static_cast<int>(n(7)), n(8) != 0, g.th);
  const bool drawn = g.use3d && engine::DrawDie(fr, float(n(1)), float(n(2)), float(n(3)), float(n(9)), float(n(10)));
  engine::LuaPushNumber(L, drawn ? 1 : 0);
  return 1;
}

// Probe: PSTF_DieState([arm]) -> "on|off drawn failed r,g,b why" (r,g,b = the die's centre pixel read
// back by the last armed draw, -1,-1,-1 before one; arm = 1 arms the next draw).
int __cdecl LuaDieState(engine::LuaState* L) {
  if (engine::LuaNumberArg(L, 1, 0) != 0) engine::ArmDieProbe();
  int drawn = 0, failed = 0;
  std::string why;
  engine::DieDrawStats(&drawn, &failed, &why);
  uint8_t rgb[3];
  int c[3] = {-1, -1, -1};
  if (engine::DieProbePixel(rgb)) c[0] = rgb[0], c[1] = rgb[1], c[2] = rgb[2];
  char buf[256];
  std::snprintf(buf, sizeof buf, "%s %d %d %d,%d,%d %s", g.use3d ? "on" : "off", drawn, failed, c[0], c[1], c[2],
                why.c_str());
  Log("lua: PSTF_DieState -> %s", buf);
  engine::LuaPushString(L, buf);
  return 1;
}

const char* kLua = R"LUA(
pstfDr = pstfDr or { open = false }
local function area(name, x, y, w, h) Infinity_SetArea(name, math.floor(x), math.floor(y), math.floor(w), math.floor(h)) end

function pstfDrOpen(c)
	if pstfDr.open then return false end
	pstfDr = { open = true, r = c }
	worldPlayerDialogSelection = 0
	if c.pause and worldScreen and e:GetActiveEngine() == worldScreen and worldScreen:CheckIfPaused() == false then
		worldScreen:TogglePauseGame(true)
		pstfDr.paused = true
	end
	Infinity_PushMenu('PSTF_ROLL')
	return true
end
function pstfDrMs() return pstfDr.t0 and (Infinity_GetClockTicks() - pstfDr.t0) or -1 end
function pstfDrRolled() return pstfDr.t0 ~= nil end
function pstfDrCanRoll() return pstfDr.open and not pstfDr.t0 and not pstfDr.r.note end
function pstfDrRollClick()
	if pstfDr.t0 or pstfDr.r.note then return end
	local nat, total, out = pstfDr.r.roll()
	if not nat or nat < 0 then return end
	pstfDr.nat, pstfDr.total, pstfDr.out = nat, total, out
	pstfDr.ok = (out == 1 or out == 3)
	pstfDr.t0 = Infinity_GetClockTicks()
end
function pstfDrClose()
	if Infinity_IsMenuOnStack('PSTF_ROLL') then Infinity_PopMenu('PSTF_ROLL') end
	if pstfDr.paused and worldScreen and worldScreen:CheckIfPaused() then worldScreen:TogglePauseGame(true) end
	pstfDr.paused = false
	pstfDr.open = false
end
function pstfDrContinue()
	local c = pstfDr.r
	pstfDrClose()
	if c and c.done then c.done() end
end
function pstfDrBack()
	if pstfDr.t0 then return end
	local c = pstfDr.r
	pstfDrClose()
	if c and c.back then c.back() end
end
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
	if PSTF_DIE3D and pstfDr.dx then
		local sw, sh = Infinity_GetScreenSize()
		local nat, total = pstfDr.nat or 20, pstfDr.total or 20
		if PSTF_DieDraw(pstfDr.dx, pstfDr.dy, 150, ms, Infinity_GetClockTicks(), nat, total, pstfDr.ok and 1 or 0,
			sw, sh, pstfDr.t0 or -1) == 1 then return 174 end
	end
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
function pstfDrTitle() return pstfDr.r and pstfDr.r.title or '' end
function pstfDrNote() return pstfDr.r and pstfDr.r.note or '' end
function pstfDrBonusText() return 'Total Bonus  ' .. pstfDrSigned(pstfDr.r.mod) end
function pstfDrTick()
	if not pstfDr.open then return false end
	if pstfDr.r.alive and not pstfDr.r.alive() then pstfDrClose() return false end
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
	pstfDr.dx, pstfDr.dy = dx, dy
	area('pstfDrDie', dx - 75, dy - 75, 150, 150)
	area('pstfDrSpark', dx - 110, dy - 110, 220, 220)
	area('pstfDrSmoke', dx - 110, dy - 110, 220, 220)
	area('pstfDrRoll', cx - 100, barY + 72, 200, 34)
	area('pstfDrNote', cx - 140, barY + 66, 280, 48)
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
  std::string m = "`\n" + std::string(consts) + (g.use3d ? "PSTF_DIE3D = true\n" : "PSTF_DIE3D = false\n") + kLua +
                  "\n`\n";
  m += "menu\n{\n\tname 'PSTF_ROLL'\n\talign left top\n\tignoreEsc\n\tmodal\n";
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
  m += Item("button", "pstfDrRoll", "pstfDrCanRoll()",
            "\t\tbam \"PFDBTN\"\n\t\tframe 0\n\t\ttext \"Roll Dice\"\n\t\ttext style \"button\"\n\t\ttext point 16\n\t\taction \"pstfDrRollClick()\"\n");
  m += Item("label", "pstfDrNote", "pstfDr.open and pstfDr.r.note ~= nil",
            "\t\ttext lua \"pstfDrNote()\"\n\t\ttext style \"normal\"\n\t\ttext point 14\n\t\ttext align center center\n\t\ttext color lua \"0x8C8CFF\"\n" + ign);
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

}  // namespace

bool InstallRollPanel() {
  if (g.installed) return g.ok;
  g.installed = true;
  if (!engine::LuaApiAvailable() || !engine::InstallMenuHook()) {
    Log("rolls: Lua/menu functions differ from the manifest; roll panel off");
    return false;
  }
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const std::wstring dir(exe);
  const std::wstring game = dir.substr(0, dir.find_last_of(L"\\/") + 1);
  g.sound_dir = game + L"pst_forever\\sounds\\";
  g.use3d = IniBool(Config(), "rolls.die_3d", true);
  engine::SetDieAtlasPath(game + L"pst_forever\\assets\\dice\\d20_numbers.bin");
  engine::AddMenuText(BuildMenu());
  dev::AddLuaFunction("PSTF_RollForce", &LuaForce);
  dev::AddLuaFunction("PSTF_DieDraw", &LuaDieDraw);
  dev::AddLuaFunction("PSTF_DieState", &LuaDieState);
  g.ok = true;
  Log("rolls: roll panel installed (die %s)", g.use3d ? "3D" : "BAM");
  return true;
}

D20 RollD20(int modifier, int dc) {
  D20 r;
  r.natural = g.force ? g.force : std::uniform_int_distribution<int>(1, 20)(g.rng);
  g.force = 0;
  r.total = r.natural + modifier;
  r.outcome = Resolve(r.natural, modifier, dc);
  return r;
}

void PlayRollSound(bool ok, const char* what) {
  if (g.sound_dir.empty()) return;
  const std::wstring f = g.sound_dir + (ok ? L"check_success.wav" : L"check_failed.wav");
  const BOOL played = PlaySoundW(f.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
  Log("%s: sound %s (%s)", what, ok ? "check_success" : "check_failed", played ? "playing" : "not played");
}

}  // namespace pstf::rolls
