-- SPDX-License-Identifier: GPL-2.0-or-later
-- Every player spell in a turn-based fight (all-spells save 000000990, tools\make_spell_save.ps1): the Nameless One
-- casts each mage spell, Fall-from-Grace each priest spell, in their own turns (SpellNoDec, as a real cast with
-- the caster's own level), at the Hive thug when the spell is hostile, at a point next to it for area spells, at
-- the caster otherwise. Per spell the suite logs one result line
--   tbc_spells_all: SPWI119 ok used=1 ticks=37 fx=+6 hp=0 name=Submerge the Will
-- A party turn waits for orders, so the suite ends it as a player would (End Turn) once the cast is over (the
-- action used, nothing acting for 30 frames; or 240 frames with the action unused). ok = the action was spent and
-- the turn ended within kStall frames of End Turn (the turn may wait for the spell's projectile, up to 300 ticks);
-- fizzle = the action stayed unused; stall = End Turn did not end it. Stalls and crashes fail the suite.
-- python -I tools/spell_results.py turns the log lines into tests/unit/spell_results.h (unit test: every spell has one).
--   tools\launch.ps1 -Save 000000990 -Lua build\tmp_tbc_spells_all.lua -Env @{PSTF_TEST_TICK='1'} -Until 'tbc_spells_all: ok|TEST FAIL' -Close -Timeout 1500
--   (tools\spell_suite.ps1 builds build\tmp_tbc_spells_all.lua = spell_list.lua + this file and runs it)
if not pstfSpellList then error('pstfSpellList missing: run through tools/spell_suite.ps1') end
local kStall = 450
local frames, cur, turnFrame = 0, -1, 0
local tno, grace, foe = nil, nil, nil
local nextMage, nextPriest = 1, 1
local pending = nil      -- {spell, caster, frame, fx0, hp0, used}
local results, stalls, noFight = {}, 0, 0
local mages, priests = {}, {}
for _, s in ipairs(pstfSpellList) do
	if s[1]:sub(1, 4) == 'SPWI' then mages[#mages + 1] = s else priests[#priests + 1] = s end
end
local only = pstfSpellsOnly  -- e.g. "SPWI119,SPPR101": just these (tools\spell_suite.ps1 -Only)
if only and only ~= '' then
	local keep = {}
	for r in only:gmatch('[^,]+') do keep[r:upper()] = true end
	local m, p = {}, {}
	for _, s in ipairs(mages) do if keep[s[1]] then m[#m + 1] = s end end
	for _, s in ipairs(priests) do if keep[s[1]] then p[#p + 1] = s end end
	mages, priests = m, p
end
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function slotOf(name)
	local i = 0
	for v in PSTF_PartyNames():gmatch('[^,]+') do
		if v == name then return i end
		i = i + 1
	end
	return -1
end
local function party(id)
	for i = 0, 7 do if PSTF_PartyId(i) == id then return i end end
	return -1
end
local function nfx(id)
	local n = 0
	for _ in string.gmatch(PSTF_Effects(id) or '', ';') do n = n + 1 end
	return n
end
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h or -1 end
local function finish(turnOver)
	local p = pending
	local s = p.spell
	local who = s[5] == 1 and foe or p.caster
	local ticks = (p.ended or frames) - p.frame
	local verdict = p.used and 'ok' or 'fizzle'
	if not turnOver then verdict = 'stall' stalls = stalls + 1 end
	local line = string.format('tbc_spells_all: %s %s used=%d ticks=%d fx=%+d hp=%d name=%s', s[1], verdict, p.used and 1 or 0, ticks,
		nfx(who) - p.fx0, hp(who) - p.hp0, s[3])
	PSTF_Note(line)
	results[#results + 1] = verdict
	pending = nil
end
local function cast(caster, s)
	local hostile, target = s[5] == 1, s[6]
	local bcs
	if hostile and (target == 4) then
		local x, y = PSTF_SpritePos(foe, 1)
		bcs = string.format('SpellPointNoDec([%d.%d],%d)', x, y, s[2])
	elseif hostile then
		bcs = 'SpellNoDec(NearestEnemyOf(Myself),' .. s[2] .. ')'
	elseif target == 4 then
		local x, y = PSTF_SpritePos(caster, 1)
		bcs = string.format('SpellPointNoDec([%d.%d],%d)', x + 20, y + 10, s[2])
	else
		bcs = 'SpellNoDec(Myself,' .. s[2] .. ')'
	end
	local who = hostile and foe or caster
	pending = {spell = s, caster = caster, frame = frames, fx0 = nfx(who), hp0 = hp(who), used = false, idle = 0, ended = nil}
	PSTF_EvalAs(caster, bcs, 1)
end
local function pickFoe()
	local f = 0x180018
	local x = PSTF_SpritePos(f, 1)
	if x and x >= 0 and party(f) < 0 then PSTF_MakeEnemy(f) return f end
	return PSTF_MakeEnemyNearest()
end

function pstfTestTick()
	frames = frames + 1
	if frames == 3 then
		tno, grace = PSTF_PartyId(0), PSTF_PartyId(slotOf('Grace'))
		if not grace or grace == -1 then return fail('no Grace in the party: use the all-spells save (000000990)') end
		PSTF_Note(string.format('tbc_spells_all: %d mage, %d priest spells; casters %X, %X', #mages, #priests, tno, grace))
	end
	if frames == 6 then foe = pickFoe() end
	if frames < 8 then return end
	-- keep everyone standing: the suite measures the spells, not who wins
	if foe and hp(foe) > 0 and hp(foe) < 100 then PSTF_SetHp(foe, 200) end
	for i = 0, 5 do
		local p = PSTF_PartyId(i)
		if p and p ~= -1 and hp(p) > 0 and hp(p) < 40 then PSTF_SetHp(p, 80) end
	end
	local id, _, _, used = PSTF_TbcCurrent()
	if id == -1 then
		-- the fight ended (the foe charmed, fled or dead): a new foe starts the next one
		noFight = noFight + 1
		if pending then finish(true) end
		if noFight == 40 then foe = pickFoe() noFight = 0 end
		cur = -1
	else
		noFight = 0
	end
	if id ~= cur and id ~= -1 then
		if pending and cur == pending.caster then finish(true) end
		cur = id
		turnFrame = frames
	end
	if pending and id == pending.caster then
		local _, _, _, u, acting = PSTF_TbcCurrent()
		if u then pending.used = true end
		pending.idle = acting and 0 or pending.idle + 1
		if not pending.ended and ((pending.used and pending.idle > 30) or frames - pending.frame > 240) then
			pending.ended = frames
			PSTF_EndTurn()
		end
	end
	if id ~= -1 and frames == turnFrame + 3 then
		if id == tno and nextMage <= #mages then
			cast(tno, mages[nextMage])
			nextMage = nextMage + 1
		elseif id == grace and nextPriest <= #priests then
			cast(grace, priests[nextPriest])
			nextPriest = nextPriest + 1
		elseif party(id) >= 0 then
			PSTF_EndTurn()
		end
	end
	-- the caster's turn ends on its own once the spell resolved; a hung one is ended here (a stall)
	if pending and id == pending.caster and pending.ended and frames - pending.ended > kStall then
		finish(false)
		PSTF_EndTurn()
	end
	-- party members whose order is done (a summoned ally's turn, a caster after its spell) end after a while
	if id ~= -1 and party(id) >= 0 and not pending and frames - turnFrame > 40 then PSTF_EndTurn() end
	if nextMage > #mages and nextPriest > #priests and not pending then
		local n = {ok = 0, fizzle = 0, stall = 0}
		for _, r in ipairs(results) do n[r] = n[r] + 1 end
		PSTF_Note(string.format('tbc_spells_all: %d spells: %d ok, %d fizzle, %d stall', #results, n.ok, n.fizzle, n.stall))
		if stalls > 0 then return fail(stalls .. ' spells stalled their caster\'s turn') end
		PSTF_Note('tbc_spells_all: ok')
		pstfTestTick = nil
	end
end
