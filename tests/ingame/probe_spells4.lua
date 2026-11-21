-- SPDX-License-Identifier: GPL-2.0-or-later
-- Real time (run with PSTF_TBC_OFF=1): party member 2 casts at a hostile; which spells go off?
local enemy = 0x320032
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h end
local n, last = 0, nil
local steps = {
	{2, function() C:Eval('ChangeStat(Myself,34,12,1)ChangeStat(Myself,68,12,1)ChangeStat(Myself,69,12,1)', 1) end, 'level 12'},
	{3, function() PSTF_MakeEnemy(enemy) end, 'enemy'},
	{20, function() C:Eval('ReallyForceSpellLevel(NearestEnemyOf(Myself),2502,12)', 1) end, 'CoC'},
	{220, function() C:Eval('ReallyForceSpellLevel(NearestEnemyOf(Myself),2310,12)', 1) end, 'Ax'},
	{420, function() C:Eval('ReallyForceSpellLevel(NearestEnemyOf(Myself),2501,12)', 1) end, 'Cloudkill'},
	{620, function() C:Eval('ForceSpell(NearestEnemyOf(Myself),2207)', 1) end, 'IceKnife'},
	{820, function() C:Eval('ForceSpell(NearestEnemyOf(Myself),2201)', 1) end, 'Adder'},
}
function pstfTestTick()
	n = n + 1
	for _, s in ipairs(steps) do if s[1] == n then s[2]() PSTF_Note('step ' .. s[3]) end end
	local h = hp(enemy)
	if last and h ~= last then PSTF_Note(string.format('enemy %d -> %d at frame %d', last, h, n)) end
	if h < 20 then PSTF_SetHp(enemy, 64) h = 64 end
	last = h
	if n == 1000 then PSTF_Note('probe done') pstfTestTick = nil end
end
