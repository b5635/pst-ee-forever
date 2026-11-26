-- SPDX-License-Identifier: GPL-2.0-or-later
-- Every player spell in a turn-based fight (all-spells save 000000990, tools\make_spell_save.ps1): the Nameless One
-- casts each mage spell, Fall-from-Grace each priest spell, in their own turns (SpellNoDec, as a real cast with
-- the caster's own level), at the Hive thug when the spell is hostile (area spells too, as a player clicks the
-- creature), at the caster otherwise. Their levels are raised first (ChangeStat: Nameless LEVEL/LEVEL2/LEVEL3 20, Grace LEVEL 14): the
-- save leaves them at 6 and 7, below most spells' first ability. Per spell the suite logs one result line
--   tbc_spells_all: SPWI119 ok used=1 cast=1 ticks=37 fx=+6 hp=0 name=Submerge the Will
-- A party turn waits for orders, so the suite ends it as a player would (End Turn) once the cast is over (the
-- action used, nothing acting for 30 frames; or 240 frames with the action unused). ok = the engine cast the spell
-- (its "casts <spell>" feedback, PSTF_Casts; env PSTF_FEEDBACK_LOG=1) and the turn ended within kStall frames of
-- End Turn (the turn may wait for the spell's projectile, up to 300 ticks); fizzle = no cast; stall = End Turn did
-- not end it. A fizzle is cast once more on the caster's next turn (log line 'retry'): a dead target or a
-- restarted fight is the situation, not the spell. A hostile spell's caster first jumps beside a foe more than
-- 20 ft off. Stalls and crashes fail the suite. After each spell its effects leave the caster and the target
-- (PSTF_Unapply: CGameSprite::UnapplySpell + attached spell objects end), so every spell is cast by a clean caster.
-- python -I tools/spell_results.py turns the log lines into tests/unit/spell_results.h (unit test: every spell has one).
--   tools\launch.ps1 -Save 000000990 -Lua build\tmp_tbc_spells_all.lua -Env @{PSTF_TEST_TICK='1'; PSTF_FEEDBACK_LOG='1'} -Until 'tbc_spells_all: ok|TEST FAIL' -Close -Timeout 1500
--   (tools\spell_suite.ps1 builds build\tmp_tbc_spells_all.lua = spell_list.lua + this file and runs it)
if not pstfSpellList then error('pstfSpellList missing: run through tools/spell_suite.ps1') end
local kStall = 750  -- frames after End Turn: the turn may wait kFlightCapTicks (300 turn ticks = 600 frames) for its projectiles
local kPriestLevel = 14  -- cleric level for the priest spells (6th level needs 11)
local kMageLevel = 20    -- mage level for the mage spells (9th level abilities start at 18)
local frames, cur, turnFrame = 0, -1, 0
local tno, grace, foe = nil, nil, nil
local nextMage, nextPriest = 1, 1
local pending = nil      -- {spell, caster, frame, fx0, hp0, used}
local results, stalls, noFight = {}, 0, 0
local retryNext = nil  -- {spell, caster}: a fizzled spell cast again on its caster's next turn
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
-- Outcome measurement (spell_expect.h compares it): derived stats (STATS.IDS) read through the engine's own
-- GetAtOffset, the STATE bits, the status icon of the spell, HP; snapshotted every frame of the caster's turn, so the
-- result is the state at the end of that turn (before the next creature's turn start steps its round clock).
local kStats = {1, 2, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 24, 32, 36, 37, 38, 39, 40, 41, 42, 50, 51, 52, 73, 117, 145, 146, 151}
local function snap(id, res)
	local v = {}
	for x in (PSTF_Stat(id, (table.unpack or unpack)(kStats)) or ''):gmatch('-?%d+') do v[#v + 1] = tonumber(x) end
	local icon = 'none'
	local _, list = PSTF_Status(id, 1)
	for e in (list or ''):gmatch('[^;]+') do
		local r, rounds, kind = e:match('^(%S+) %S+ %S+ r(%d+) (%a+)')
		if r == res then icon = 'r' .. rounds .. ':' .. kind end
	end
	return {stats = v, state = PSTF_State(id, 1) or 0, hp = hp(id), icon = icon}
end
local function statDelta(a, b)
	local out = {}
	for i, st in ipairs(kStats) do
		if a.stats[i] and b.stats[i] and a.stats[i] ~= b.stats[i] then out[#out + 1] = st .. ':' .. string.format('%+d', b.stats[i] - a.stats[i]) end
	end
	return #out > 0 and table.concat(out, ',') or '-'
end
local function newBits(a, b)
	-- STATE bits the spell set (as unsigned 32-bit)
	local x, y = math.floor(a.state) % 4294967296, math.floor(b.state) % 4294967296
	local bits, v = 0, 1
	for _ = 0, 31 do
		if math.floor(y / v) % 2 == 1 and math.floor(x / v) % 2 == 0 then bits = bits + v end
		v = v * 2
	end
	return string.format('%X', bits)
end
local function finish(turnOver)
	local p = pending
	local s = p.spell
	local who = s[5] == 1 and foe or p.caster
	local ticks = (p.ended or frames) - p.frame
	-- ok = the engine cast it (its "casts <spell>" feedback): a spent action alone is not proof, a refused cast
	-- (no ability for the caster level) still ends the action
	local cast = PSTF_Casts(p.caster) - p.casts0
	local verdict = (p.used and cast > 0) and 'ok' or 'fizzle'
	if verdict == 'fizzle' and turnOver and not p.retry then
		-- one more try on the caster's next turn: a target that died or a fight that restarted is the situation's,
		-- not the spell's (the line says retry; a second miss is the result)
		PSTF_Note(string.format('tbc_spells_all: %s retry (used=%d cast=%d)', s[1], p.used and 1 or 0, cast))
		PSTF_Unapply(p.caster, s[1])
		retryNext = {spell = s, caster = p.caster}
		pending = nil
		return
	end
	if not turnOver then verdict = 'stall' stalls = stalls + 1 end
	local t1 = p.last or snap(who, s[1])
	local c1 = p.lastc or snap(p.caster, s[1])
	local line = string.format('tbc_spells_all: %s %s used=%d cast=%d ticks=%d fx=%+d hp=%d st=%s state=%s icon=%s chp=%d cst=%s name=%s', s[1], verdict,
		p.used and 1 or 0, cast, ticks, nfx(who) - p.fx0, t1.hp - p.t0.hp, statDelta(p.t0, t1), newBits(p.t0, t1), t1.icon,
		who ~= p.caster and c1.hp - p.c0.hp or 0, who ~= p.caster and statDelta(p.c0, c1) or '-', s[3])
	PSTF_Note(line)
	if pstfSpellsTrace then PSTF_Action(p.caster) end
	if verdict == 'fizzle' then
		-- why: the caster's effects (a disabling one?), the target's state, what the round clock says
		PSTF_Note('tbc_spells_all: ' .. s[1] .. ' caster fx ' .. (PSTF_Effects(p.caster) or '?'))
		PSTF_Note('tbc_spells_all: ' .. s[1] .. ' foe ' .. string.format('%X hp %d fx %s', foe or -1, hp(foe or -1), PSTF_Effects(foe or -1) or '?'))
	end
	results[#results + 1] = verdict
	-- a clean caster (and target) for the next spell: Fist of Iron's spell failure (opcode 60), a mantle or a
	-- shield would decide the next cast. The engine's own UnapplySpell, then the attached spell objects end.
	PSTF_Unapply(p.caster, s[1])
	if who ~= p.caster then PSTF_Unapply(who, s[1]) end
	pending = nil
end
local function cast(caster, s, retryOf)
	-- Area spells (target 4) go at the creature, as a player clicking it: the scripted point cast
	-- (SpellPointNoDec at [x.y]) is dropped by the engine for most of them, in real time too (probe: Blacksphere at
	-- the thug's point never casts, at the thug it does).
	local hostile = s[5] == 1
	local bcs
	if hostile then
		bcs = 'SpellNoDec(NearestEnemyOf(Myself),' .. s[2] .. ')'
	else
		bcs = 'SpellNoDec(Myself,' .. s[2] .. ')'
	end
	local who = hostile and foe or caster
	-- a friendly spell's caster (its own target) starts hurt, so a heal shows in its HP
	if not hostile then PSTF_SetHp(caster, 30) end
	local cx, cy = PSTF_SpritePos(caster, 1)
	local fx, fy = PSTF_SpritePos(foe, 1)
	PSTF_Note(string.format('tbc_spells_all: %s cast by %X at %d,%d, foe %X at %d,%d (%.0f ft): %s', s[1], caster, cx, cy, foe, fx, fy,
		math.sqrt(((fx - cx) / 16) ^ 2 + ((fy - cy) / 12) ^ 2), bcs))
	pending = {spell = s, caster = caster, who = who, frame = frames, fx0 = nfx(who), hp0 = hp(who), used = false, idle = 0, ended = nil,
		casts0 = PSTF_Casts(caster), retry = retryOf, t0 = snap(who, s[1]), c0 = snap(caster, s[1])}
	PSTF_EvalAs(caster, bcs, 1)
end
-- The measured foe fails every save (saves 30 vs a d20) and has no magic resistance, so each spell's full effect
-- shows (pstfSpellsSaves = 'pass': saves 1, it always saves; tools\spell_suite.ps1 -Saves pass).
local function rig(f)
	local v = (pstfSpellsSaves == 'pass') and 1 or 30
	-- one take-over (it clears the queue), the rest queued behind it
	local first = 1
	for _, st in ipairs({'SAVEVSDEATH', 'SAVEVSWANDS', 'SAVEVSPOLY', 'SAVEVSBREATH', 'SAVEVSSPELL'}) do
		PSTF_EvalAs(f, 'ChangeStat(Myself,' .. st .. ',' .. v .. ',SET)', first)
		first = 0
	end
	PSTF_EvalAs(f, 'ChangeStat(Myself,RESISTMAGIC,0,SET)', 0)
	PSTF_SetHp(f, 1000, 1000)
end
local function pickFoe()
	-- the Hive thug of the save, else the nearest living creature made hostile (a spell may have killed it)
	local f = 0x180018
	local x = PSTF_SpritePos(f, 1)
	if x and x >= 0 and party(f) < 0 and hp(f) > 0 then PSTF_MakeEnemy(f) rig(f) return f end
	local n = PSTF_MakeEnemyNearest()
	if n and n ~= -1 then rig(n) end
	return n
end

function pstfTestTick()
	frames = frames + 1
	if frames == 3 then
		tno, grace = PSTF_PartyId(0), PSTF_PartyId(slotOf('Grace'))
		if not grace or grace == -1 then return fail('no Grace in the party: use the all-spells save (000000990)') end
		PSTF_Note(string.format('tbc_spells_all: %d mage, %d priest spells; casters %X, %X', #mages, #priests, tno, grace))
		-- Grace stays at cleric level 7 on the save (3M XP, but no level-up is pending: Infinity_CanLevelUp 0), one short
		-- of 5th-level priest spells (cleric 9) and 6th (cleric 11): raise her level so every priest spell can be cast
		C:Eval('ChangeStat(Myself,LEVEL,' .. kPriestLevel .. ',SET)', slotOf('Grace'))
		-- the Nameless One (a mage on this save) is level 6: the engine picks the SPL ability for the caster level
		-- (CGameSprite::SpellPST) and refuses, without a word, a spell whose first ability needs more. His class
		-- levels are kept apart (fighter, mage, thief: LEVEL, LEVEL2, LEVEL3), so all three are raised
		for _, st in ipairs({'LEVEL', 'LEVEL2', 'LEVEL3'}) do C:Eval('ChangeStat(Myself,' .. st .. ',' .. kMageLevel .. ',SET)', 0) end
	end
	if frames == 5 then
		PSTF_Note(string.format('tbc_spells_all: levels Grace > %d = %d, Nameless > %d = %d, casts probe %d', kPriestLevel - 1,
			PSTF_Trigger(grace, 0x4095, kPriestLevel - 1, 'Grace', '', 1), kMageLevel - 1,
			PSTF_Trigger(tno, 0x4095, kMageLevel - 1, 'Nameless', '', 1), PSTF_Casts(tno)))
		if PSTF_Casts(tno) < 0 then return fail('PSTF_Casts needs env PSTF_FEEDBACK_LOG=1 (tools/spell_suite.ps1 sets it)') end
	end
	if frames == 6 then foe = pickFoe() end
	if frames < 8 then return end
	-- keep everyone standing: the suite measures the spells, not who wins
	-- (not while a spell is measured: its HP change is the result)
	if foe and not pending and hp(foe) > 0 and hp(foe) < 500 then PSTF_SetHp(foe, 1000, 1000) end  -- Elemental Strike takes 200+
	-- the foe died anyway (a death spell, or between two frames) while others keep the fight going: a living one
	if foe and hp(foe) <= 0 and not pending and frames % 30 == 0 then foe = pickFoe() end
	for i = 0, 5 do
		local p = PSTF_PartyId(i)
		if p and p ~= -1 and hp(p) > 0 and hp(p) < 40 and not (pending and pending.caster == p) then PSTF_SetHp(p, 80) end
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
		pending.last = snap(pending.who, pending.spell[1])
		if pending.who ~= pending.caster then pending.lastc = snap(pending.caster, pending.spell[1]) end
		local _, _, _, u, acting = PSTF_TbcCurrent()
		if u then pending.used = true end
		local _, busy, path = PSTF_Action(pending.caster, 1)
		pending.idle = (acting or busy == 1 or path == 1) and 0 or pending.idle + 1  -- walking into range is not idle
		if pstfSpellsTrace and not pending.used and (frames - pending.frame) % 20 == 1 then
			PSTF_Action(pending.caster) PSTF_Attached(pending.caster) PSTF_Note("tbc_spells_all: trace anim " .. PSTF_AnimProbe(pending.caster))
		end
		if not pending.ended and ((pending.used and pending.idle > 30) or frames - pending.frame > 240) then
			pending.ended = frames
			PSTF_EndTurn()
		end
	end
	if id ~= -1 and frames == turnFrame + 1 then
		-- a hostile spell at a foe more than 20 ft off (beyond a touch spell's walk, maybe out of sight for
		-- NearestEnemyOf): the caster jumps to a point beside it first
		local nxt = (retryNext and id == retryNext.caster and retryNext.spell) or (id == tno and mages[nextMage]) or
			(id == grace and priests[nextPriest]) or nil
		if nxt and nxt[5] == 1 and foe and hp(foe) > 0 then
			local cx, cy = PSTF_SpritePos(id, 1)
			local fx, fy = PSTF_SpritePos(foe, 1)
			if math.sqrt(((fx - cx) / 16) ^ 2 + ((fy - cy) / 12) ^ 2) > 20 then
				PSTF_EvalAs(id, string.format('JumpToPoint([%d.%d])', fx - 40, fy + 20), 1)
			end
		end
	end
	if id ~= -1 and frames == turnFrame + 3 then
		if retryNext and id == retryNext.caster then
			local r = retryNext
			retryNext = nil
			cast(r.caster, r.spell, true)
		elseif id == tno and nextMage <= #mages then
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
	if nextMage > #mages and nextPriest > #priests and not pending and not retryNext then
		local n = {ok = 0, fizzle = 0, stall = 0}
		for _, r in ipairs(results) do n[r] = n[r] + 1 end
		PSTF_Note(string.format('tbc_spells_all: %d spells: %d ok, %d fizzle, %d stall', #results, n.ok, n.fizzle, n.stall))
		if stalls > 0 then return fail(stalls .. ' spells stalled their caster\'s turn') end
		PSTF_Note('tbc_spells_all: ok')
		pstfTestTick = nil
	end
end
