-- SPDX-License-Identifier: GPL-2.0-or-later
-- Buffs and debuffs start in the cast turn and wear off at the right turn start (all-spells save 000000990).
-- Each tracked effect is read at the end of its caster's turn and at every turn start of its owner (the creature the
-- round clock steps it on), 3 frames in: the spell's status icon (rounds) and the real effect (a derived stat or a
-- STATE bit). It must:
--   * be on at the end of the cast turn, with the icon showing ceil(duration / 5 s) rounds;
--   * lose exactly one icon round per owner turn start, the effect on while the icon shows;
--   * be gone (icon and effect) at the owner turn start where the icon would reach 0, not a turn early or late.
-- Phase A: Blessing (Grace, 30 s: stat 151 HITBONUS +1), Vilquar's Eye (Nameless at the foe, 30 s: STATE_BLIND),
-- Curse (Grace at the foe, 30 s: HITBONUS -1). Phase B: Prayer (Grace at cleric level 5: 25 s; a persistent
-- aura that pulses a 6 s bless each of her turns: her bonus outlives the icon by two turns at most), Submerge the Will
-- (Nameless, an attached spell object: three turns of its countdown, it lasts 72 s).
-- Run: tools\launch.ps1 -Save 000000990 -Lua tests\ingame\tbc_decay.lua -Env @{PSTF_TEST_TICK='1'} -Until 'tbc_decay: ok|TEST FAIL' -Close -Timeout 500
local frames, cur, turnStart = 0, -1, 0
local tno, grace, foe = nil, nil, 0x180018
local turns = {}       -- turns started per creature
local tracks = {}
local pending = nil    -- {track(s), caster}
local phase = 'A'
local casts = {}       -- per caster: list of {track names}, issued in order on its turns
local function fail(msg) PSTF_Note('TEST FAIL: tbc_decay: ' .. msg) pstfTestTick = nil end
local function note(msg) PSTF_Note('tbc_decay: ' .. msg) end
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h or -1 end
local function slotOf(name)
	local i = 0
	for v in PSTF_PartyNames():gmatch('[^,]+') do
		if v == name then return i end
		i = i + 1
	end
	return -1
end
local function stat(id, st) return tonumber((PSTF_Stat(id, st) or ''):match('-?%d+')) or -32768 end
local function icon(id, res)
	local _, list = PSTF_Status(id, 1)
	for e in (list or ''):gmatch('[^;]+') do
		local r, rounds = e:match('^(%S+) %S+ %S+ r(%d+)')
		if r == res then return tonumber(rounds) end
	end
	return 0
end
local function on(t)
	if t.bit then return math.floor((PSTF_State(t.owner, 1) or 0) / t.bit) % 2 == 1 end
	local v = stat(t.owner, t.stat)
	return (t.sign > 0 and v > t.base) or (t.sign < 0 and v < t.base)
end
local function track(name, owner, res, expect, how)
	local t = {name = name, owner = owner, res = res, expect = expect, seen = {}, done = false, lag = how.lag or 0,
		stat = how.stat, sign = how.sign, bit = how.bit, steps = how.steps}
	tracks[name] = t
	return t
end
-- At an owner turn start: one reading; returns an error text or nil.
local function read(t)
	local r, o = icon(t.owner, t.res), on(t)
	local prev = t.seen[#t.seen]
	t.seen[#t.seen + 1] = {r = r, on = o}
	local i = #t.seen
	if not prev then return nil end
	if prev.r > 0 then
		local want = prev.r - 1
		if r ~= want then return string.format('%s: icon %d -> %d at owner turn start %d (want %d)', t.name, prev.r, r, i, want) end
	end
	if r > 0 and not o then return t.name .. ': icon shows ' .. r .. ' rounds but the effect is off' end
	if r == 0 then
		t.off = t.off or i
		if o and i - t.off >= t.lag then return t.name .. ': the effect is still on ' .. (i - t.off) .. ' turns after its icon went' end
		if not o or i - t.off >= t.lag then t.done = true end
	end
	if t.steps and i > t.steps then t.done = true end
	return nil
end
local function rig(f)
	local first = 1
	for _, st in ipairs({'SAVEVSDEATH', 'SAVEVSWANDS', 'SAVEVSPOLY', 'SAVEVSBREATH', 'SAVEVSSPELL'}) do
		PSTF_EvalAs(f, 'ChangeStat(Myself,' .. st .. ',30,SET)', first)
		first = 0
	end
	PSTF_EvalAs(f, 'ChangeStat(Myself,RESISTMAGIC,0,SET)', 0)
	PSTF_SetHp(f, 1000, 1000)
end

function pstfTestTick()
	frames = frames + 1
	if frames == 3 then
		tno, grace = PSTF_PartyId(0), PSTF_PartyId(slotOf('Grace'))
		if not grace or grace == -1 then return fail('no Grace: use the all-spells save (000000990)') end
		-- cleric level 5: Prayer lasts 25 s (5 rounds); Blessing and Curse do not depend on it
		C:Eval('ChangeStat(Myself,LEVEL,5,SET)', slotOf('Grace'))
		casts[grace] = {{'Blessing', 'SpellNoDec(Myself,1101)'}, {'Curse', 'SpellNoDec(NearestEnemyOf(Myself),1103)'}}
		casts[tno] = {{"Vilquar's Eye", 'SpellNoDec(NearestEnemyOf(Myself),2120)'}}
		track('Blessing', grace, 'SPPR101', 6, {stat = 151, sign = 1})
		track("Vilquar's Eye", foe, 'SPWI120', 6, {bit = 0x40000})
		track('Curse', foe, 'SPPR103', 6, {stat = 151, sign = -1})  -- to hit -1 (its save +1 sits at the rigged foe's cap)
	end
	-- ChangeStat is queued: it must run before the fight (a turn start clears a party member's queue)
	if frames == 30 then
		if stat(grace, 34) ~= 5 then return fail('Grace level ' .. stat(grace, 34) .. ', want 5') end
		PSTF_MakeEnemy(foe)
		rig(foe)
	end
	if frames < 33 then return end
	if hp(foe) > 0 and hp(foe) < 500 then PSTF_SetHp(foe, 1000, 1000) end
	for i = 0, 5 do
		local p = PSTF_PartyId(i)
		if p and p ~= -1 and hp(p) > 0 and hp(p) < 40 then PSTF_SetHp(p, 80) end
	end
	local id, round, _, used, acting = PSTF_TbcCurrent()
	if id == -1 then
		if frames > 600 then return fail('no fight') end
		return
	end
	if round > 40 then return fail('40 rounds and not every effect has run out') end
	if id ~= cur then
		-- the cast turn ended: the effect must be on, its icon full
		if pending and cur == pending.caster then
			for _, t in ipairs(pending.tracks) do
				local r = t.last and t.last.r or 0
				local o = t.last and t.last.on or false
				note(string.format('%s: end of the cast turn: icon %d, on %s', t.name, r, tostring(o)))
				if not o then return fail(t.name .. ' is not on at the end of its cast turn') end
				if t.expect and r ~= t.expect then return fail(string.format('%s: icon %d rounds after the cast, want %d', t.name, r, t.expect)) end
				t.seen[1] = {r = r, on = o}
			end
			pending = nil
		end
		cur, turnStart = id, frames
		turns[id] = (turns[id] or 0) + 1
	end
	-- readings at owner turn starts
	if frames == turnStart + 3 then
		for _, t in pairs(tracks) do
			if t.owner == id and not t.done and #t.seen > 0 then
				local err = read(t)
				local s = t.seen[#t.seen]
				note(string.format('%s: owner turn start %d: icon %d, on %s', t.name, #t.seen, s.r, tostring(s.on)))
				if err then return fail(err) end
				if t.done then note(t.name .. ': ok, ' .. (#t.seen - 1) .. ' owner turn starts') end
			end
		end
	end
	-- the caster's turn: every frame after the cast, the last reading counts
	if pending and id == pending.caster then
		for _, t in ipairs(pending.tracks) do t.last = {r = icon(t.owner, t.res), on = on(t)} end
		if used then pending.used = true end
		if not acting and pending.used then pending.idle = pending.idle + 1 end
		if pending.idle > 30 or frames - pending.frame > 300 then
			if not pending.ended then pending.ended = true PSTF_EndTurn() end
		end
	end
	-- party turns: cast the next queued spell, else end the turn
	if frames == turnStart + 4 and casts[id] ~= nil then
		local q = casts[id]
		if #q > 0 and not pending then
			local c = table.remove(q, 1)
			local t = tracks[c[1]]
			if t.stat then t.base = stat(t.owner, t.stat) end  -- just before the cast (the foe's rigged saves are in)
			pending = {caster = id, tracks = {t}, frame = frames, idle = 0}
			note(c[1] .. ': cast by ' .. string.format('%X', id) .. ' (LEVEL ' .. stat(id, 34) .. ', LEVEL2 ' .. stat(id, 68) .. ', LEVEL3 ' .. stat(id, 69) .. '): ' .. c[2])
			PSTF_EvalAs(id, c[2], 1)
		else
			PSTF_EndTurn()
		end
	elseif frames == turnStart + 10 and not pending and PSTF_PartyId(0) ~= -1 then
		for i = 0, 7 do if PSTF_PartyId(i) == id and id ~= tno and id ~= grace then PSTF_EndTurn() end end
	end
	-- phase A done: Prayer and Submerge the Will
	local all = true
	for _, t in pairs(tracks) do if not t.done then all = false end end
	if phase == 'A' and all and tracks.Curse and #(casts[grace]) == 0 and not pending then
		phase = 'B'
		note('phase A ok: ' .. turns[foe] .. ' foe turns')
		-- its last pulse (at the turn start its icon goes) gives a 6 s bless: 90 ticks = 2 more owner turns, as in real time
		-- the bless outlives the aura by up to 6 s
		track('Prayer', grace, 'SPPR303', 5, {stat = 151, sign = 1, lag = 2})
		track('Submerge the Will', tno, 'SPWI119', nil, {stat = 2, sign = -1, steps = 3})
		casts[grace] = {{'Prayer', 'SpellNoDec(Myself,1303)'}}
		casts[tno] = {{'Submerge the Will', 'SpellNoDec(Myself,2119)'}}
	elseif phase == 'B' and all and #(casts[grace]) == 0 and #(casts[tno]) == 0 and not pending then
		note('ok')
		pstfTestTick = nil
	end
end
